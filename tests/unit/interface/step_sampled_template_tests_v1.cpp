#include <pcv/workpiece/template_package_v1.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cstring>
#include <cmath>
#include <iostream>
using namespace pcv::workpiece;
bool run(){
#define CHECK(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;}}while(false)
    StepModelResult model;model.success=true;CadCloudResult cloud;cloud.success=true;
    QJsonArray selected;
    for(int faceId=1;faceId<=3;++faceId){
        CadFaceInfo face;face.id=faceId;face.planar=true;face.surfaceType="plane";face.areaMm2=400;face.planeNormal={0,0,1};face.planeOrigin={0,0,float(faceId)};model.faces.append(face);
        int v=int(model.mesh.vertices.size());const float z=float(faceId);
        model.mesh.vertices.append(QVector<QVector3D>{{0,0,z},{20,0,z},{20,20,z},{0,20,z}});
        model.mesh.triangles.append({v,v+1,v+2,faceId});model.mesh.triangles.append({v,v+2,v+3,faceId});
        cloud.faceRanges.append({faceId,cloud.points.size(),400});
        for(int y=0;y<20;++y)for(int x=0;x<20;++x)cloud.points.append({float(x)+.25f,float(y)+.25f,z,0,0,1});
        selected.append(QJsonObject{{"id",faceId},{"area_mm2",400.0},{"surface_type","plane"}});
    }
    cloud.source={{"entry","step_cad"},{"processing_steps",QJsonArray{}},{"cad",QJsonObject{
        {"original_step","missing.step"},{"sha256",QString(64,'a')},{"original_units","millimetre"},{"unit_scale_to_mm",1.0},
        {"occt_version","7.9.3"},{"algorithm_version","cad_surface_area_v1"},{"selected_faces",selected},
        {"spacing_mm",1.0},{"geometry_tolerance_mm",.01},{"generated_point_count",1200},{"edge_sampling",false}}}};
    const auto shared=cloud.points;QSet<int> removed;
    for(int i=0;i<400;++i)if(cloud.points.at(i).x>9)removed.insert(i);
    // Entirely removed interference face disappears, its old geometry must not survive.
    for(int i=800;i<1200;++i)removed.insert(i);
    TemplateFeatureOptionsV1 options;options.budget=std::make_shared<pcv::ProcessingBudget>(512ull*1024*1024);
    auto package=generateStepTemplatePackageFromSampledV1(model,cloud,{1,2},{3},removed,options);
    if(!package.ok)std::cerr<<package.error.toStdString()<<'\n';CHECK(package.ok);
    CHECK(package.points.size()==580&&package.interferencePoints.isEmpty()&&package.planes.size()==2);
    CHECK(package.planes.at(0).first==0&&package.planes.at(0).count==180&&package.planes.at(1).first==180);
    for(const auto &v:package.planes.at(0).region.vertices)CHECK(std::isfinite(v.x())&&std::isfinite(v.y())&&std::isfinite(v.z()));
    CHECK(package.source.value("cad").toObject().value("generated_point_count").toInt()==1200);
    CHECK(parseTemplateFeaturesV1(serializeTemplateFeaturesV1(package.planes),580,0).ok);
    CHECK(std::memcmp(shared.constData(),cloud.points.constData(),size_t(shared.size())*sizeof(pointcloud::Point3D))==0);
    CHECK(package.identity==templatePointIdentityV1(package.points));
    QTemporaryDir tmp;CHECK(tmp.isValid());const auto saved=saveTemplatePackageV1(package,QStringLiteral("修剪模板"),tmp.path());CHECK(saved.success);
    const auto loaded=loadTemplatePackageV1(saved.outputDirectory);CHECK(loaded.ok&&loaded.identity==package.identity&&loaded.planes.size()==2);
    CHECK(saveTemplatePackageV1(package,QStringLiteral("修剪模板"),tmp.path()).outputDirectory!=saved.outputDirectory);
    CHECK(!generateStepTemplatePackageFromSampledV1(model,cloud,{1},{1},{},options).ok);
    CHECK(!generateStepTemplatePackageFromSampledV1(model,cloud,{1},{},{-1},options).ok);
    options.isCancelled=[]{return true;};auto cancelled=generateStepTemplatePackageFromSampledV1(model,cloud,{1},{},{},options);CHECK(cancelled.cancelled&&!cancelled.ok&&cancelled.points.isEmpty());
    return true;
#undef CHECK
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);return run()?0:1;}
