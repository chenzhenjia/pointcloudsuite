#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QCoreApplication>
#include <QtMath>
#include <atomic>
#include <iostream>
#include <limits>
#include <QElapsedTimer>
#include <algorithm>
#include <future>

namespace baseline_suite {
using namespace pcv::planefitting;
namespace {
bool require(bool value,const char *message) {if(!value)std::cerr<<message<<'\n';return value;}
QVector<pointcloud::Point3D> plane(float angle,int side=30,bool noise=false,bool hole=false) {
    QVector<pointcloud::Point3D> points;const float s=std::sin(qDegreesToRadians(angle)),c=std::cos(qDegreesToRadians(angle));
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        if(hole&&x>=12&&x<17&&y>=12&&y<17)continue;
        const float n=noise?float((x*13+y*7)%11-5)*0.003f:0;
        points.push_back({float(x)*0.5f,float(y)*0.5f*c-n*s,20+float(y)*0.5f*s+n*c});
    }
    return points;
}
bool same(const SingleClickResult &a,const SingleClickResult &b) {
    return a.status==b.status&&a.planeIndices==b.planeIndices&&a.model.a==b.model.a&&a.model.b==b.model.b&&a.model.c==b.model.c&&a.model.d==b.model.d&&a.rmsMm==b.rmsMm;
}
}
int run() {
    for(float angle:{0.0f,30.0f,60.0f,75.0f,90.0f}) {
        const auto points=plane(angle,30,true);const auto sharedCopy=points;
        const auto prepared=preparePlaneSelectionIndex(points);
        if(!require(bool(prepared.index),"index preparation failed"))return 1;
        const auto selected=selectPlaneAtPoint(points,prepared.index,465);
        if(!require(selected.ok()&&selected.planeIndices.size()==points.size()&&selected.planeIndices.contains(465),"oriented/noisy plane selection failed")) {std::cerr<<selected.error.toStdString()<<'\n';return 1;}
        for(int tasks:{1,2,4}) {SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=tasks;
            if(!require(same(selected,selectPlaneAtPoint(sharedCopy,prepared.index,465,options)),"serial/parallel shared-container mismatch"))return 1;
        }
        if(!require(selectPlaneAtPoint(points,prepared.index,-1).status==SingleClickStatus::InvalidInput,"invalid click accepted"))return 1;
        auto changed=points;changed[0].z+=1;
        if(!require(selectPlaneAtPoint(changed,prepared.index,465).status==SingleClickStatus::InvalidInput,"stale cache accepted"))return 1;
    }
    auto points=plane(0);const int firstCount=int(points.size());
    auto separate=plane(0);for(auto &p:separate)p.x+=40;points.append(separate);
    auto parallel=plane(0);for(auto &p:parallel)p.z+=5;points.append(parallel);
    const auto prepared=preparePlaneSelectionIndex(points);const auto selected=selectPlaneAtPoint(points,prepared.index,465);
    if(!require(selected.ok()&&selected.planeIndices.size()==firstCount&&selected.planeIndices.last()<firstCount,"disjoint/parallel surfaces leaked"))return 1;
    const auto holes=plane(30,30,true,true);const auto holeIndex=preparePlaneSelectionIndex(holes);
    const auto holeResult=selectPlaneAtPoint(holes,holeIndex.index,93);
    if(!require(holeResult.ok()&&holeResult.planeIndices.size()==holes.size(),"connected surface with hole lost points"))return 1;
    auto folded=plane(0,40);
    for(int y=0;y<40;++y)for(int z=0;z<40;++z)folded.push_back({0,float(y)*0.5f,20+float(z)*0.5f});
    const auto foldIndex=preparePlaneSelectionIndex(folded);const auto foldResult=selectPlaneAtPoint(folded,foldIndex.index,820);
    if(!require(foldResult.ok()&&foldResult.planeIndices.contains(820),"plane beside fold not selected"))return 1;
    for(int i:foldResult.planeIndices)if(!require(folded.at(i).z<=20.4f,"fold crossed onto side face"))return 1;
    if(!require(!selectPlaneAtPoint(folded,foldIndex.index,800).ok(),"ambiguous fold click accepted"))return 1;
    QVector<pointcloud::Point3D> line;for(int i=0;i<200;++i)line.push_back({float(i),0,0});
    const auto lineIndex=preparePlaneSelectionIndex(line);
    if(!require(!selectPlaneAtPoint(line,lineIndex.index,100).ok(),"collinear neighborhood accepted"))return 1;
    QVector<pointcloud::Point3D> small(50);
    if(!require(!preparePlaneSelectionIndex(small).index,"insufficient input indexed"))return 1;
    auto invalid=plane(0);invalid[0].x=std::numeric_limits<float>::quiet_NaN();const auto invalidIndex=preparePlaneSelectionIndex(invalid);
    if(!require(selectPlaneAtPoint(invalid,invalidIndex.index,0).status==SingleClickStatus::InvalidInput,"non-finite seed accepted"))return 1;
    SingleClickOptions bad;bad.surfaceToleranceMm=0;
    if(!require(selectPlaneAtPoint(points,prepared.index,465,bad).status==SingleClickStatus::InvalidInput,"invalid tolerance accepted"))return 1;
    if(!require(preparePlaneSelectionIndex(points,[]{return true;}).cancelled,"index cancellation lost"))return 1;
    for(int tasks:{1,2,4})for(int stop:{1,500,1500}) {
        std::atomic_int checks{0};SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=tasks;
        options.isCancelled=[&checks,stop]{return ++checks>=stop;};
        const auto result=selectPlaneAtPoint(points,prepared.index,465,options);
        if(!require(result.status==SingleClickStatus::Cancelled&&result.planeIndices.isEmpty(),"cancelled selection published partial points"))return 1;
    }
    return 0;
}
}
namespace connectivity_suite {
using namespace pcv::planefitting;
namespace {
bool check(bool ok,const char *message) {if(!ok)std::cerr<<message<<'\n';return ok;}
void patch(QVector<pointcloud::Point3D> &points,int width,int height,float x0,float dx=0.5f,float dy=0.5f,float z=0) {
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)points.push_back({x0+x*dx,y*dy,z});
}
void rotate(QVector<pointcloud::Point3D> &points,float tilt,float spin) {
    const float c=std::cos(qDegreesToRadians(tilt)),s=std::sin(qDegreesToRadians(tilt));
    const float cu=std::cos(qDegreesToRadians(spin)),su=std::sin(qDegreesToRadians(spin));
    for(auto &p:points) {
        const float x=p.x*cu-p.y*su,y=p.x*su+p.y*cu,z=p.z;
        p={x,y*c-z*s,y*s+z*c};
    }
}
bool valid(const QVector<pointcloud::Point3D> &points,const SingleClickResult &r,int seed,float tolerance) {
    if(!r.ok()||!r.planeIndices.contains(seed)||!std::is_sorted(r.planeIndices.begin(),r.planeIndices.end()))return false;
    int previous=-1;
    for(int i:r.planeIndices) {
        if(i<=previous||i>=points.size())return false;previous=i;
        const auto &p=points.at(i);
        if(std::abs(r.model.a*p.x+r.model.b*p.y+r.model.c*p.z+r.model.d)>tolerance)return false;
    }
    return r.refinementIterations>=1&&r.refinementIterations<=6;
}
bool same(const SingleClickResult &a,const SingleClickResult &b) {
    return a.status==b.status&&a.planeIndices==b.planeIndices&&a.model.a==b.model.a&&a.model.b==b.model.b&&a.model.c==b.model.c
        &&a.model.d==b.model.d&&a.rmsMm==b.rmsMm&&a.mergedRegionCount==b.mergedRegionCount&&a.refinementIterations==b.refinementIterations;
}
bool gaps() {
    for(float gap:{2.0f,3.0f,3.01f}) {
        QVector<pointcloud::Point3D> points;patch(points,32,32,0);patch(points,32,32,15.5f+gap);
        const auto prepared=preparePlaneSelectionIndex(points);
        for(int seed:{400,1424}) {
            const auto result=selectPlaneAtPoint(points,prepared.index,seed);
            SingleClickOptions noReuse;noReuse.reuseConnectivityWorkspace=false;
            if(!check(same(result,selectPlaneAtPoint(points,prepared.index,seed,noReuse)),"gap workspace mismatch"))return false;
            const int expected=gap<=3?2048:1024;
            if(!check(valid(points,result,seed,0.4f)&&result.planeIndices.size()==expected,"gap threshold/completeness failed")) {
                std::cerr<<gap<<" seed="<<seed<<" count="<<result.planeIndices.size()<<" "<<result.error.toStdString()<<'\n';return false;
            }
            if(!check(result.mergedRegionCount==(gap<=3?1:0),"merged region diagnostic wrong"))return false;
            SingleClickOptions disabled;disabled.maximumGapMm=0;
            const auto noGap=selectPlaneAtPoint(points,prepared.index,seed,disabled);
            if(!check(valid(points,noGap,seed,0.4f)&&noGap.planeIndices.size()==1024&&noGap.mergedRegionCount==0,"zero gap did not preserve separation"))return false;
        }
    }
    return true;
}
bool surfaces() {
    for(float tilt:{0.0f,30.0f,75.0f,90.0f}) {
        QVector<pointcloud::Point3D> points;
        for(int k=0;k<3;++k)patch(points,32,32,k*18.0f);
        rotate(points,tilt,27);
        const auto prepared=preparePlaneSelectionIndex(points);
        for(int seed:{400,1424,2448}) {
            const auto r=selectPlaneAtPoint(points,prepared.index,seed);
            if(!check(valid(points,r,seed,0.4f)&&r.planeIndices.size()==3072&&r.mergedRegionCount==2,"multiple gaps / rotated plane incomplete"))return false;
        }
    }
    QVector<pointcloud::Point3D> anisotropic;patch(anisotropic,80,60,0,0.05f,1.5f);
    rotate(anisotropic,60,37);const auto anisoIndex=preparePlaneSelectionIndex(anisotropic);
    SingleClickOptions disabled;disabled.maximumGapMm=0;
    const auto anisoResult=selectPlaneAtPoint(anisotropic,anisoIndex.index,2440,disabled);
    if(!check(valid(anisotropic,anisoResult,2440,0.4f)&&anisoResult.planeIndices.size()==anisotropic.size(),"anisotropic rotated scan incomplete without gap merge")) {
        std::cerr<<anisoResult.error.toStdString()<<" count="<<anisoResult.planeIndices.size()<<" spacings="<<anisoResult.samplingSpacingUMm<<","<<anisoResult.samplingSpacingVMm<<'\n';return false;
    }
    QVector<pointcloud::Point3D> holes;
    for(int y=0;y<80;++y)for(int x=0;x<80;++x) {
        if(x>=25&&x<55&&y>=20&&y<60)continue;
        holes.push_back({x*0.5f,y*0.5f,0});
    }
    const auto holesIndex=preparePlaneSelectionIndex(holes);const auto holeResult=selectPlaneAtPoint(holes,holesIndex.index,410);
    SingleClickOptions noReuse;noReuse.reuseConnectivityWorkspace=false;
    if(!check(same(holeResult,selectPlaneAtPoint(holes,holesIndex.index,410,noReuse)),"hole workspace mismatch"))return false;
    if(!check(valid(holes,holeResult,410,0.4f)&&holeResult.planeIndices.size()==holes.size(),"large hole lost border / narrow connection"))return false;
    QVector<pointcloud::Point3D> mixed;patch(mixed,40,40,0,0.2f,0.2f);patch(mixed,20,20,8.0f,0.8f,0.4f);
    const int targetCount=int(mixed.size());patch(mixed,40,40,0,0.2f,0.2f,5);patch(mixed,20,20,60);
    const auto mixedIndex=preparePlaneSelectionIndex(mixed);
    for(int seed:{820,1810}) {
        const auto r=selectPlaneAtPoint(mixed,mixedIndex.index,seed);
        SingleClickOptions noReuse;noReuse.reuseConnectivityWorkspace=false;
        if(!check(same(r,selectPlaneAtPoint(mixed,mixedIndex.index,seed,noReuse)),"mixed workspace mismatch"))return false;
        if(!check(valid(mixed,r,seed,0.4f)&&r.planeIndices.size()==targetCount&&r.planeIndices.last()<targetCount,"mixed density / parallel layer / separate object failed")) {
            std::cerr<<r.error.toStdString()<<" count="<<r.planeIndices.size()<<'\n';return false;
        }
    }
    // Coplanar independent patches within the threshold intentionally merge.
    QVector<pointcloud::Point3D> nearby;patch(nearby,32,32,0);patch(nearby,32,32,18);
    const auto nearbyIndex=preparePlaneSelectionIndex(nearby);const auto shared=nearby;
    const auto baseline=selectPlaneAtPoint(nearby,nearbyIndex.index,400);
    for(int tasks:{1,2,4}) {
        SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=tasks;
        if(!check(same(baseline,selectPlaneAtPoint(shared,nearbyIndex.index,400,options)),"shared-buffer serial/parallel mismatch"))return false;
    }
    // Noise and small outliers must not discard whole mixed cells.
    auto noisy=holes;for(int i=0;i<noisy.size();++i)noisy[i].z=float((i*13)%11-5)*0.005f;
    const int actual=int(noisy.size());for(int i=0;i<50;++i)noisy.push_back({20+i*0.01f,20,0.39f});
    const auto noisyIndex=preparePlaneSelectionIndex(noisy);const auto noisyResult=selectPlaneAtPoint(noisy,noisyIndex.index,410);
    if(!check(valid(noisy,noisyResult,410,0.4f)&&noisyResult.planeIndices.size()>=actual,"noise/mixed cell caused missing plane"))return false;
    QVector<pointcloud::Point3D> folded;patch(folded,40,40,0);
    const int planeCount=int(folded.size());
    for(int y=0;y<40;++y)for(int z=0;z<40;++z)folded.push_back({0,y*0.5f,z*0.5f});
    const auto foldIndex=preparePlaneSelectionIndex(folded);
    const auto foldResult=selectPlaneAtPoint(folded,foldIndex.index,820);
    if(!check(same(foldResult,selectPlaneAtPoint(folded,foldIndex.index,820,noReuse)),"fold workspace mismatch"))return false;
    if(!check(valid(folded,foldResult,820,0.4f),"fold selection invalid"))return false;
    for(int i=0;i<planeCount;++i)if(!check(foldResult.planeIndices.contains(i),"fold neighborhood discarded true plane points"))return false;
    for(int i:foldResult.planeIndices)if(!check(folded.at(i).z<=0.4f,"fold crossed onto side"))return false;
    return true;
}
bool cancellation() {
    QVector<pointcloud::Point3D> points;patch(points,40,40,0);patch(points,40,40,22);
    const auto prepared=preparePlaneSelectionIndex(points);const auto shared=points;
    std::atomic_int total{0};SingleClickOptions count;count.isCancelled=[&total]{++total;return false;};
    if(!selectPlaneAtPoint(points,prepared.index,820,count).ok())return false;
    const int checks=total.load();
    for(int tasks:{1,2,4})for(int stop:{1,checks/2,checks*9/10}) {
        std::atomic_int calls{0};SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=tasks;
        options.isCancelled=[&calls,stop]{return ++calls>=stop;};
        const auto r=selectPlaneAtPoint(shared,prepared.index,820,options);
        if(!check(r.status==SingleClickStatus::Cancelled&&r.planeIndices.isEmpty(),"late cancellation published points"))return false;
    }
    // Owner waits for all work before releasing its captured immutable buffer.
    std::atomic_bool stop{false};auto running=std::async(std::launch::async,[shared,prepared,&stop] {
        SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=4;options.isCancelled=[&stop]{return stop.load();};
        return selectPlaneAtPoint(shared,prepared.index,820,options);
    });stop=true;
    if(!check(running.get().status==SingleClickStatus::Cancelled,"owner shutdown did not join cancellation"))return false;
    for(float gap:{-1.0f,50.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        SingleClickOptions options;options.maximumGapMm=gap;
        if(!check(selectPlaneAtPoint(points,prepared.index,820,options).status==SingleClickStatus::InvalidInput,"invalid gap accepted"))return false;
    }
    return true;
}
bool optimizedIndexEquivalence() {
    QVector<pointcloud::Point3D> points;patch(points,40,40,0);patch(points,40,40,22);
    const auto shared=points;const auto referenceIndex=preparePlaneSelectionIndex(points);
    const auto reference=selectPlaneAtPoint(points,referenceIndex.index,820);
    if(!check(reference.ok(),"index reference failed"))return false;
    for(int workers:{1,2,4})for(size_t bytes:{size_t(0),size_t(16)*1024*1024}) {
        auto budget=std::make_shared<pcv::ProcessingBudget>(bytes);
        {
            IndexPreparationOptions prepare;prepare.parallelEnabled=true;prepare.maxParallelTasks=workers;
            prepare.cachePointBuckets=true;prepare.processingBudget=budget;
            const auto index=preparePlaneSelectionIndex(shared,{},prepare);
            if(!check(bool(index.index)&&index.pointBucketCacheUsed==(bytes>0),"point bucket budget fallback invalid"))return false;
            SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=workers;options.processingBudget=budget;
            if(!check(same(reference,selectPlaneAtPoint(shared,index.index,820,options)),"optimized grid/model/result changed"))return false;
            std::atomic_int calls{0};
            const auto stopped=preparePlaneSelectionIndex(shared,[&calls]{return ++calls>=4;},prepare);
            if(!check(stopped.cancelled&&!stopped.index,"index cancellation published partial grid"))return false;
        }
        if(!check(budget->used()==0,"index budget lease leaked"))return false;
    }
    return true;
}
bool neighborAndWorkspaceEquivalence() {
    QVector<pointcloud::Point3D> data;patch(data,40,40,0);patch(data,40,40,22);
    const auto points=data;const auto retained=points;Q_UNUSED(retained);
    const auto baselineIndex=preparePlaneSelectionIndex(points);
    SingleClickOptions baselineOptions;baselineOptions.reuseConnectivityWorkspace=false;
    const auto baseline=selectPlaneAtPoint(points,baselineIndex.index,820,baselineOptions);
    if(!check(baseline.ok(),"v3 baseline failed"))return false;
    for(int workers:{1,2})for(size_t bytes:{size_t(0),size_t(64)*1024*1024}) {
        auto budget=std::make_shared<pcv::ProcessingBudget>(bytes);
        {
            IndexPreparationOptions options;options.cacheNormalNeighbors=true;options.parallelEnabled=true;
            options.maxParallelTasks=workers;options.normalBatchSize=65536;options.processingBudget=budget;
            const auto index=preparePlaneSelectionIndex(points,{},options);
            if(!check(index.index&&index.neighborCacheUsed==(bytes>0),"neighbor cache budget path invalid"))return false;
            if(!check(budget->used()==0,"transient neighbor lease retained by index"))return false;
            for(bool reuse:{false,true}) {
                SingleClickOptions selection;selection.reuseConnectivityWorkspace=reuse;selection.processingBudget=budget;
                if(!check(same(baseline,selectPlaneAtPoint(points,index.index,820,selection)),"neighbor/workspace changed selection"))return false;
            }
            std::atomic_int calls{0};const auto cancelled=preparePlaneSelectionIndex(points,[&calls]{return ++calls>10;},options);
            if(!check(cancelled.cancelled&&!cancelled.index,"neighbor cancellation published partial index"))return false;
        }
        if(!check(budget->used()==0,"v3 optional lease leaked"))return false;
    }
    return true;
}
bool parallelProjectionEquivalence() {
    // Exceeds the dispatch threshold and spans more than one bounded window.
    QVector<pointcloud::Point3D> points;patch(points,320,320,0);
    const auto shared=points;
    const auto index=preparePlaneSelectionIndex(points);
    const auto reference=selectPlaneAtPoint(points,index.index,51360);
    if(!check(reference.ok()&&reference.planeIndices.size()==points.size(),"projection reference incomplete"))return false;
    for(int workers:{1,2,4})for(size_t bytes:{size_t(0),size_t(16)*1024*1024}) {
        auto budget=std::make_shared<pcv::ProcessingBudget>(bytes);
        SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=workers;options.processingBudget=budget;
        const auto selected=selectPlaneAtPoint(shared,index.index,51360,options);
        if(!check(same(reference,selected)&&budget->used()==0,"parallel projection changed selected model/indices or leaked budget"))return false;
        if(!check(selected.projectionMs>=0&&selected.normalConnectivityMs>=0,"projection diagnostics invalid"))return false;
    }
    return check(points.constData()==shared.constData(),"projection detached shared input");
}
bool normalBatchEquivalence() {
    // Spacing equals the capped index cell size: >65536 distinct buckets.
    QVector<pointcloud::Point3D> data;patch(data,320,320,0,5.0f,5.0f);
    const auto points=data;const auto retained=points;Q_UNUSED(retained);
    const auto referenceIndex=preparePlaneSelectionIndex(points);
    const auto reference=selectPlaneAtPoint(points,referenceIndex.index,51360);
    if(!check(reference.ok(),"batch baseline failed"))return false;
    for(int batch:{4096,65536})for(int workers:{1,2,4}) {
        IndexPreparationOptions options;options.normalBatchSize=batch;options.parallelEnabled=true;options.maxParallelTasks=workers;
        const auto prepared=preparePlaneSelectionIndex(points,{},options);
        if(!check(prepared.index&&prepared.normalBatchSize==batch&&prepared.normalWorkerCount==qMin(workers,int((prepared.bucketCount+batch-1)/batch)),"batch diagnostics invalid"))return false;
        if(!check(same(reference,selectPlaneAtPoint(points,prepared.index,51360)),"batch changed plane selection"))return false;
    }
    // Exercise cached neighbors with actual parallel chunks (>65536 buckets).
    {
        auto budget=std::make_shared<pcv::ProcessingBudget>(64u*1024u*1024u);
        IndexPreparationOptions options;options.cacheNormalNeighbors=true;
        options.parallelEnabled=true;options.maxParallelTasks=2;options.normalBatchSize=65536;options.processingBudget=budget;
        const auto prepared=preparePlaneSelectionIndex(points,{},options);
        if(!check(prepared.index&&prepared.neighborCacheUsed&&prepared.normalWorkerCount==2&&budget->used()==0,
            "parallel neighbor cache path invalid"))return false;
        if(!check(same(reference,selectPlaneAtPoint(points,prepared.index,51360)),"parallel neighbor cache changed selection"))return false;
    }
    for(size_t bytes:{size_t(0),size_t(512)*1024}) {
        auto budget=std::make_shared<pcv::ProcessingBudget>(bytes);
        {
            IndexPreparationOptions options;options.normalBatchSize=65536;options.parallelEnabled=true;options.maxParallelTasks=2;options.processingBudget=budget;
            const auto prepared=preparePlaneSelectionIndex(points,{},options);
            if(!check(prepared.index&&prepared.normalFallback==(bytes?QStringLiteral("budget_small_batch"):QStringLiteral("budget_serial")),"batch budget fallback invalid"))return false;
            if(!check(same(reference,selectPlaneAtPoint(points,prepared.index,51360)),"fallback changed result"))return false;
            auto calls=std::make_shared<std::atomic_int>(0);
            const auto cancelled=preparePlaneSelectionIndex(points,[calls]{return ++*calls>100;},options);
            if(!check(cancelled.cancelled&&!cancelled.index,"large batch cancellation published result"))return false;
        }
        if(!check(budget->used()==0,"batch budget leaked"))return false;
    }
    return true;
}
}
int run() {
    return gaps()&&surfaces()&&cancellation()&&optimizedIndexEquivalence()&&parallelProjectionEquivalence()&&normalBatchEquivalence()&&neighborAndWorkspaceEquivalence()?0:1;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const int baseline=baseline_suite::run();
    return baseline ? baseline : connectivity_suite::run();
}
