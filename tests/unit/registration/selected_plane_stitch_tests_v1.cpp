#include <pcv/stitching/selected_plane_stitch.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/io/ply_reader.h>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtMath>
#include <iostream>
#include <limits>

using namespace pcv::stitching;
namespace {
bool check(bool value,const char *message) {if(!value)std::cerr<<message<<'\n';return value;}
SelectedPlaneFrame fixture(float degrees,int side=25,float spacing=0.7f) {
    SelectedPlaneFrame f;f.normal=QVector3D(0,-std::sin(qDegreesToRadians(degrees)),std::cos(qDegreesToRadians(degrees)));
    f.center=QVector3D(10,20,30);const QVector3D u(1,0,0),v=QVector3D::crossProduct(f.normal,u);
    f.cloud.sourceFile=QStringLiteral("synthetic.ply");
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const auto p=f.center+u*(float(x)-side/2)*spacing+v*(float(y)-side/2)*spacing;
        f.cloud.fullPoints.push_back({p.x(),p.y(),p.z()});f.cloud.sourceIndices.push_back((y*side+x)*2);f.cloud.scanRatios.push_back(float(y)/side);
    }
    f.cloud.declaredCount=side*side*2;f.originalValidPointCount=side*side+30;f.seedSourceIndices={0,2,qsizetype(side)*2};return f;
}
void transform(SelectedPlaneFrame *f,const QMatrix4x4 &m) {
    for(auto &p:f->cloud.fullPoints){const auto v=m.map(QVector3D(p.x,p.y,p.z));p.x=v.x();p.y=v.y();p.z=v.z();}
    f->center=m.map(f->center);f->normal=m.mapVector(f->normal);
}
SelectedPlaneStitchResult execute(QVector<SelectedPlaneFrame> frames,QString path,SelectedPlaneOptions options={}) {
    SelectedPlaneStitchRequest r;r.frames=std::move(frames);r.outputDirectory=path;r.options=options;return stitchSelectedPlanes(r);
}
bool identical(const SelectedPlaneStitchResult &a,const SelectedPlaneStitchResult &b) {
    if(a.points.size()!=b.points.size()||a.cloudIds!=b.cloudIds||a.sourceIndices!=b.sourceIndices||a.scanRatios!=b.scanRatios||a.corrections!=b.corrections||a.pairs.size()!=b.pairs.size())return false;
    for(qsizetype i=0;i<a.points.size();++i){const auto &p=a.points.at(i),&q=b.points.at(i);if(p.x!=q.x||p.y!=q.y||p.z!=q.z)return false;}
    for(int i=0;i<a.pairs.size();++i){const auto &p=a.pairs.at(i),&q=b.pairs.at(i);if(p.reason!=q.reason||p.iterations!=q.iterations||p.rmseMm!=q.rmseMm||p.correspondences!=q.correspondences||p.observableDof!=q.observableDof||p.fineApplied!=q.fineApplied||p.completedLevels!=q.completedLevels)return false;}
    return true;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir root;bool ok=root.isValid();
    for(float tilt:{0.f,30.f,60.f,75.f,90.f}) {
        auto target=fixture(tilt),source=target;
        QMatrix4x4 error;error.translate(target.center);error.rotate(0.2f,QVector3D(1,0,0));error.translate(-target.center);
        const auto n=target.normal;error(0,3)+=n.x()*0.5f;error(1,3)+=n.y()*0.5f;error(2,3)+=n.z()*0.5f;transform(&source,error);
        auto result=execute({target,source},root.filePath(QString::number(tilt)));
        ok&=check(result.success&&result.pairs.size()==1,"tilt output failed");if(!result.success) {std::cerr<<result.message.toStdString()<<'\n';continue;}
        const auto &pair=result.pairs.first();ok&=check(pair.fineApplied&&pair.completedLevels==2&&pair.observableDof==3&&pair.rmseMm<0.01f,"tilt normal/angle refinement failed");
        ok&=check(result.points.size()==target.cloud.fullPoints.size()+source.cloud.fullPoints.size(),"selected points lost or deduplicated");
        for(qsizetype i=0;i<result.points.size();++i) {
            const bool first=i<target.cloud.fullPoints.size();
            const auto &input=first?target.cloud.fullPoints.at(i):source.cloud.fullPoints.at(i-target.cloud.fullPoints.size());
            const QVector3D expected=first?QVector3D(input.x,input.y,input.z):result.corrections.at(1).map(QVector3D(input.x,input.y,input.z));
            const auto &actual=result.points.at(i);
            ok&=check((expected-QVector3D(actual.x,actual.y,actual.z)).length()<1e-6f,"output reordered/projected or applied correction twice");
        }
        const auto fixed=pair.pairCorrection.map(source.center);ok&=check(std::abs(QVector3D::dotProduct(n,fixed-target.center))<0.01f,"normal gap not corrected");
        const auto raw=pcv::detail::io::readPly(result.outputPly);ok&=check(raw.ok&&raw.points.size()==result.points.size(),"PLY readback mismatch");
        QFile manifest(result.reportFile);manifest.open(QIODevice::ReadOnly);auto json=QJsonDocument::fromJson(manifest.readAll()).object();
        const auto pjson=json.value("pairs").toArray().first().toObject();ok&=check(pjson.value("accepted").isNull()&&pjson.value("structural_coverage_3mm").isNull()&&json.value("strict_validation_status")=="not_run","strict status not honest");
        QFile map(result.mappingFile);map.open(QIODevice::ReadOnly);QDataStream stream(&map);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        quint32 magic,version;quint64 count;stream>>magic>>version>>count;ok&=check(magic==0x314D4350&&version==1&&count==quint64(result.points.size()),"map header mismatch");
        for(qsizetype i=0;i<result.points.size();++i){qint32 cloud;qint64 index;float ratio;stream>>cloud>>index>>ratio;ok&=check(cloud==result.cloudIds.at(i)&&index==result.sourceIndices.at(i)&&ratio==result.scanRatios.at(i),"map provenance mismatch");}
    }
    auto a=fixture(0),b=a,c=a;QMatrix4x4 offset;offset.translate(1.2f,0.8f,0.4f);transform(&b,offset);
    auto clicked=a;clicked.selectionMethod=PlaneSelectionMethod::SingleClick;clicked.seedSourceIndices={0};
    clicked.selectionParameters={{"surface_tolerance_mm",0.4},{"algorithm_version","single_click_plane_selection_v2"},
        {"maximum_gap_mm",3.0},{"merged_region_count",2},{"refinement_iterations",3}};
    const auto clickOutput=execute({a,clicked},root.filePath("single_click"));
    ok&=check(clickOutput.success,"single click / three point mixed request rejected");
    if(clickOutput.success) {
        const auto frames=clickOutput.report.value("frames").toArray();
        ok&=check(frames.at(0).toObject().value("selection_method")=="three_point"&&frames.at(1).toObject().value("selection_method")=="single_click"
            &&frames.at(1).toObject().value("seed_source_indices").toArray().size()==1
            &&frames.at(1).toObject().value("selection_parameters").toObject().value("surface_tolerance_mm").toDouble()==0.4,"selection metadata lost");
        QFile saved(clickOutput.reportFile);saved.open(QIODevice::ReadOnly);
        const auto savedFrames=QJsonDocument::fromJson(saved.readAll()).object().value("frames").toArray();
        ok&=check(savedFrames.at(1).toObject().value("selection_parameters").toObject()==clicked.selectionParameters,
            "v2 selection parameters lost in manifest roundtrip");
    }
    clicked.seedSourceIndices={0,2,50};ok&=check(!execute({a,clicked},root.filePath("click_wrong_count")).success,"single click accepted three fabricated seeds");
    clicked.seedSourceIndices={1};ok&=check(!execute({a,clicked},root.filePath("click_not_selected")).success,"click outside selected points accepted");
    auto tangent=execute({a,b},root.filePath("tangent"));
    ok&=check(tangent.success&&tangent.pairs.first().fineApplied,"pure plane refinement failed");
    if(tangent.success) {const auto m=tangent.corrections.at(1);ok&=check(std::abs(m(0,3))<0.0001f&&std::abs(m(1,3))<0.0001f,"unobservable tangent drift");}
    offset.setToIdentity();offset.translate(0,0,0.3);b=a;transform(&b,offset);offset.translate(0,0,0.3);transform(&c,offset);
    auto chain=execute({a,b,c},root.filePath("chain"));ok&=check(chain.success&&std::abs(chain.corrections.last()(2,3)+0.6f)<0.01f,"chain correction double-applied or wrong order");
    auto far=b;offset.setToIdentity();offset.translate(100,0,0);transform(&far,offset);
    auto reset=execute({a,far,c},root.filePath("reset"));ok&=check(reset.success&&reset.pairs.first().poseFallback&&reset.pairs.first().chainAnchorReset&&reset.corrections.at(1).isIdentity(),"fallback did not reset anchor");
    auto tooFar=a;offset.setToIdentity();offset.translate(0,0,5);transform(&tooFar,offset);
    auto noSolution=execute({a,tooFar},root.filePath("noSolution"));ok&=check(noSolution.success&&noSolution.pairs.first().poseFallback,"no solution must pose fallback");
    SelectedPlaneOptions tight;tight.maximumTranslationMm=0.01f;auto budget=execute({a,b},root.filePath("budget"),tight);ok&=check(budget.success&&budget.pairs.first().reason=="correction_budget_exceeded","budget violation not discarded");
    auto few=fixture(0,5);auto insufficient=execute({few,few},root.filePath("few"));ok&=check(insufficient.success&&insufficient.pairs.first().poseFallback,"small plane must fall back");
    auto invalid=a;invalid.cloud.sourceIndices[1]=invalid.cloud.sourceIndices.first();ok&=check(!execute({a,invalid},root.filePath("invalid")).success,"duplicate provenance accepted");
    invalid=a;invalid.cloud.fullPoints[0].x=std::numeric_limits<float>::quiet_NaN();ok&=check(!execute({a,invalid},root.filePath("nonfinite")).success,"nonfinite accepted");
    SelectedPlaneStitchRequest cancellation;cancellation.frames={a,b};cancellation.outputDirectory=root.filePath("cancel");bool cancel=false;cancellation.isCancelled=[&cancel]{return cancel;};cancellation.progress=[&cancel](float,const QString &){cancel=true;};
    const auto cancelled=stitchSelectedPlanes(cancellation);ok&=check(cancelled.cancelled&&!cancelled.success&&cancelled.points.isEmpty()&&!QFile::exists(cancellation.outputDirectory+"/selected_plane_robot_base.ply"),"cancel published partial result");
    for(float phase:{0.8f,0.9f}) {
        cancel=false;cancellation.outputDirectory=root.filePath(QStringLiteral("cancel_phase%1").arg(phase));
        cancellation.progress=[&cancel,phase](float value,const QString &){if(value>=phase)cancel=true;};
        const auto r=stitchSelectedPlanes(cancellation);
        ok&=check(r.cancelled&&!r.success&&r.points.isEmpty()&&!QFile::exists(cancellation.outputDirectory+"/selected_plane_robot_base.ply"),"merge/output cancellation published partial result");
    }
    ok&=check(!execute({a,b},root.filePath("chain")).success,"existing output overwritten without permission");
    // More than 20k points activates the real worker path; sparse sampling keeps
    // a 100-repeat heap/determinism regression practical in Debug as well.
    auto big=fixture(0,150,4.0f),shifted=big;offset.setToIdentity();offset.translate(0,0,0.2f);transform(&shifted,offset);
    const auto sharedFrames=QVector<SelectedPlaneFrame>{big,shifted};const auto extraSharedCopy=sharedFrames;
    SelectedPlaneOptions sequential;sequential.parallelEnabled=false;
    const auto baseline=execute(sharedFrames,root.filePath("serial"),sequential);
    ok&=check(baseline.success,"serial baseline failed");
    for(int tasks:{1,2,4}){SelectedPlaneOptions parallel;parallel.maxParallelTasks=tasks;const auto r=execute(sharedFrames,root.filePath(QStringLiteral("parallel%1").arg(tasks)),parallel);ok&=check(r.success&&identical(baseline,r),"parallel points/diagnostics differ");}
    ok&=check(extraSharedCopy.first().cloud.fullPoints.first().z==big.cloud.fullPoints.first().z,"workers modified shared input");
    return ok?0:1;
}
