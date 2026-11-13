#include <pcv/matching/template_match_v1.h>
#include <pcv/registration/rigid_estimation_v1.h>
#include <pcv/workpiece/template_package_v1.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
using namespace pcv::workpiece;
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;}}while(false)
namespace {
TemplatePlaneV1 rectangle() {
    TemplatePlaneV1 p;p.faceId=1;p.count=40*24;p.center={19.5f,11.5f,0};p.normal={0,0,1};p.axis={1,0,0};
    p.region.vertices={{0,0,0},{39,0,0},{39,23,0},{0,23,0}};p.region.triangleIndices={0,1,2,0,2,3};
    p.region.boundaryVertices={{0,0,0},{39,0,0},{39,23,0},{0,23,0},{0,0,0}};p.region.boundaryRanges={{0,5}};return p;
}
pcv::matching::TemplateMatchRequestV1 fixture(QMatrix4x4 transform={}) {
    pcv::matching::TemplateMatchRequestV1 r;r.budget=std::make_shared<pcv::ProcessingBudget>(64u*1024u*1024u);r.templatePlanes={rectangle()};
    for(int y=0;y<24;++y)for(int x=0;x<40;++x){r.templatePoints.append({float(x),float(y),0});auto p=transform.map(QVector3D(float(x),float(y),0));r.targetPoints.append({p.x(),p.y(),p.z()});}
    auto p=rectangle();p.center=transform.map(p.center);p.normal=transform.mapVector(p.normal);p.axis=transform.mapVector(p.axis);p.model.a=p.normal.x();p.model.b=p.normal.y();p.model.c=p.normal.z();p.model.d=-QVector3D::dotProduct(p.normal,p.center);
    for(auto &v:p.region.vertices)v=transform.map(v);for(auto &v:p.region.boundaryVertices)v=transform.map(v);r.targetPlanes={p};return r;
}
bool rigidTests() {
    QVector<QVector3D> source{{0,0,0},{10,0,0},{0,7,0},{3,4,2}};QVector<QVector3D> target;QMatrix4x4 truth;truth.translate(75,-42,19);truth.rotate(83,1,2,3);
    for(auto p:source)target.append(truth.map(p));const auto retained=source;auto fit=pointcloud::estimateRigidV1(source,target);
    REQUIRE(fit.ok&&fit.rmsMm<1e-4);REQUIRE(pointcloud::rotationDifferenceDegreesV1(fit.targetFromSource,truth)<.05);
    for(int i=0;i<source.size();++i)REQUIRE((fit.targetFromSource.map(source.at(i))-target.at(i)).length()<1e-4);
    REQUIRE(source==retained);REQUIRE(!pointcloud::estimateRigidV1({{0,0,0},{1,0,0},{2,0,0}},{{0,0,0},{1,0,0},{2,0,0}}).ok);
    REQUIRE(!pointcloud::alignFramesV1({}, {1,0,0},{1,0,0},{},{1,0,0},{0,1,0}).ok);
    auto bad=source;bad[0].setX(std::numeric_limits<float>::quiet_NaN());REQUIRE(!pointcloud::estimateRigidV1(bad,target).ok);
    QVector<QVector3D> positions,normals;for(int i=0;i<12;++i){positions.append({float(i%4),float(i/4),0});normals.append({0,0,1});}
    REQUIRE(pointcloud::rigidConstraintRankV1(positions,normals)==3);return true;
}
bool matchingTests() {
    QMatrix4x4 truth;truth.translate(75,-42,19);truth.rotate(67,1,2,3);auto r=fixture(truth);const auto retained=r.templatePoints;
    auto result=pcv::matching::matchTemplateV1(r);if(!result.success)std::cerr<<result.error.toStdString()<<'\n';REQUIRE(result.success);REQUIRE(result.candidates.size()<=5);
    bool found=false;for(auto c:result.candidates){REQUIRE(c.acceptable&&c.matchedPoints>=100&&c.coverage>=.3&&c.observability>0);if((c.targetFromTemplate.map({0,0,0})-truth.map({0,0,0})).length()<1&&pointcloud::rotationDifferenceDegreesV1(c.targetFromTemplate,truth)<1)found=true;}
    REQUIRE(found);REQUIRE(r.templatePoints.constData()==retained.constData());REQUIRE(r.budget->used()==0);
    REQUIRE(result.candidates.size()>=2); // A rectangle admits distinct symmetric poses.
    r.targetPlanes[0].normal=-r.targetPlanes[0].normal;REQUIRE(pcv::matching::matchTemplateV1(r).success);
    auto red=rectangle();red.faceId=2;red.interference=true;red.center={1e6f,1e6f,1e6f};r.templatePlanes.append(red);REQUIRE(pcv::matching::matchTemplateV1(r).success);
    r=fixture();r.targetPlanes[0].region.boundaryVertices.clear();r.targetPlanes[0].region.boundaryRanges.clear();REQUIRE(!pcv::matching::matchTemplateV1(r).success);
    r=fixture();r.targetPoints.resize(99);REQUIRE(!pcv::matching::matchTemplateV1(r).success);
    r=fixture();r.isCancelled=[] {return true;};auto cancelled=pcv::matching::matchTemplateV1(r);REQUIRE(cancelled.cancelled&&!cancelled.success&&cancelled.candidates.isEmpty());REQUIRE(r.budget->used()==0);
    r=fixture();r.budget=std::make_shared<pcv::ProcessingBudget>(0);REQUIRE(!pcv::matching::matchTemplateV1(r).success);
    r=fixture();r.parameters.maximumHypotheses=1;for(auto &p:r.targetPoints)p.z+=1000;auto limited=pcv::matching::matchTemplateV1(r);REQUIRE(!limited.success&&limited.searchLimited&&limited.evaluatedHypotheses==1);
    r=fixture();r.parameters.minimumCoverage=std::numeric_limits<double>::quiet_NaN();REQUIRE(!pcv::matching::matchTemplateV1(r).success);return true;
}
bool featureTests() {
    QVector<pointcloud::Point3D> points;
    for(int y=0;y<28;++y)for(int x=0;x<36;++x){if(x>=12&&x<20&&y>=9&&y<17)continue;if(x>=28&&y>=20)continue;points.append({float(x),float(y),float(.2*x+.1*y)});}
    auto retained=points;TemplateFeatureOptionsV1 options;options.maximumSeeds=8;options.budget=std::make_shared<pcv::ProcessingBudget>(64u*1024u*1024u);
    auto features=prepareTemplateFeaturesV1(points,options);if(!features.ok)std::cerr<<features.error.toStdString()<<'\n';REQUIRE(features.ok);
    bool hole=false;for(const auto &p:features.planes){REQUIRE(!p.indices.isEmpty());if(p.region.boundaryRanges.size()>=2)hole=true;
        for(auto range:p.region.boundaryRanges)REQUIRE(p.region.boundaryVertices.at(range.first)==p.region.boundaryVertices.at(range.first+range.second-1));}
    REQUIRE(hole);REQUIRE(retained.constData()==points.constData());REQUIRE(options.budget->used()==0);
    options.isCancelled=[] {return true;};auto cancelled=prepareTemplateFeaturesV1(points,options);REQUIRE(cancelled.cancelled&&!cancelled.ok);
    // Partial observations with mild deterministic noise preserve original point order.
    auto r=fixture();r.targetPoints.clear();for(int y=0;y<24;++y)for(int x=0;x<40;++x)if(!(x>=14&&x<23&&y>=8&&y<16))r.targetPoints.append({float(x),float(y),float(((x*17+y*11)%7)-3)*.02f});
    REQUIRE(pcv::matching::matchTemplateV1(r).success);
    // A tiny observed patch must not pass the configured coverage requirement.
    r=fixture();for(int y=0;y<24;++y)for(int x=0;x<40;++x)if(x>8||y>12){auto &p=r.targetPoints[y*40+x];p.z+=1000;}
    r.parameters.minimumCoverage=.95;REQUIRE(!pcv::matching::matchTemplateV1(r).success);return true;
}
bool packageTests() {
    QTemporaryDir root;REQUIRE(root.isValid());auto r=fixture();TemplatePackageV1 package;package.ok=true;package.coordinateFrame="robot_base";package.points=r.templatePoints;package.planes=r.templatePlanes;
    auto interference=rectangle();interference.faceId=2;interference.interference=true;for(auto &p:interference.region.vertices)p.setZ(8);for(auto &p:interference.region.boundaryVertices)p.setZ(8);interference.model.d=-8;interference.center.setZ(8);
    for(auto p:package.points){p.z=8;package.interferencePoints.append(p);}package.planes.append(interference);
    auto saved=saveTemplatePackageV1(package,QString::fromUtf8("模板 中文"),root.path());REQUIRE(saved.success);
    auto loaded=loadTemplatePackageV1(saved.outputDirectory);if(!loaded.ok)std::cerr<<loaded.error.toStdString()<<'\n';REQUIRE(loaded.ok&&loaded.planes.size()==2);REQUIRE(loaded.interferencePoints.size()==package.interferencePoints.size());
    REQUIRE(loaded.identity==templatePointIdentityV1(package.points));REQUIRE(saveTemplatePackageV1(package,"second",root.path()).outputDirectory!=saved.outputDirectory);
    auto serialized=serializeTemplateFeaturesV1(package.planes);REQUIRE(parseTemplateFeaturesV1(serialized,package.points.size(),package.interferencePoints.size()).ok);
    auto faces=serialized.value("faces").toArray();auto face=faces.at(0).toObject();face.insert("triangles",QJsonArray{0,1,9999});faces.replace(0,face);serialized.insert("faces",faces);REQUIRE(!parseTemplateFeaturesV1(serialized,package.points.size(),package.interferencePoints.size()).ok);
    TemplateFeatureOptionsV1 options;options.isCancelled=[] {return true;};REQUIRE(loadTemplatePackageV1(saved.outputDirectory,options).cancelled);
    QFile sidecar(QDir(saved.outputDirectory).filePath("template_roles_v1.json"));REQUIRE(sidecar.open(QIODevice::ReadOnly));auto object=QJsonDocument::fromJson(sidecar.readAll()).object();sidecar.close();object.insert("workpiece_identity",QString(64,'0'));REQUIRE(sidecar.open(QIODevice::WriteOnly));sidecar.write(QJsonDocument(object).toJson());sidecar.close();REQUIRE(!loadTemplatePackageV1(saved.outputDirectory).ok);return true;
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);return rigidTests()&&matchingTests()&&featureTests()&&packageTests()?0:1;}
