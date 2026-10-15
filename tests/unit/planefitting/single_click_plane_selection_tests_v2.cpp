#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtMath>
#include <algorithm>
#include <atomic>
#include <future>
#include <iostream>
#include <limits>

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
    if(!check(valid(holes,holeResult,410,0.4f)&&holeResult.planeIndices.size()==holes.size(),"large hole lost border / narrow connection"))return false;
    QVector<pointcloud::Point3D> mixed;patch(mixed,40,40,0,0.2f,0.2f);patch(mixed,20,20,8.0f,0.8f,0.4f);
    const int targetCount=int(mixed.size());patch(mixed,40,40,0,0.2f,0.2f,5);patch(mixed,20,20,60);
    const auto mixedIndex=preparePlaneSelectionIndex(mixed);
    for(int seed:{820,1810}) {
        const auto r=selectPlaneAtPoint(mixed,mixedIndex.index,seed);
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
int benchmark() {
    QVector<pointcloud::Point3D> points;points.reserve(6300000);patch(points,1000,6300,0,0.1f,0.1f);
    const auto prepared=preparePlaneSelectionIndex(points);if(!prepared.index)return 1;
    std::cout<<"synthetic_points="<<points.size()<<" preparation_ms="<<prepared.preparationMs<<'\n';
    QVector<double> timings;SingleClickOptions options;options.parallelEnabled=true;options.maxParallelTasks=4;
    for(int run=0;run<6;++run) {
        const auto r=selectPlaneAtPoint(points,prepared.index,3150500,options);
        if(!valid(points,r,3150500,0.4f)||r.planeIndices.size()!=points.size())return 1;
        std::cout<<"run="<<run<<" algorithm_ms="<<r.timings.totalMs<<" connectivity_ms="<<r.timings.connectivityMs<<" refinement_iterations="<<r.refinementIterations<<'\n';
        if(run)timings.push_back(r.timings.totalMs);
    }
    std::sort(timings.begin(),timings.end());std::cout<<"median_algorithm_ms="<<timings.at(2)<<" scope=synthetic_module_only_excludes_pick_snapshot_and_display\n";return 0;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--benchmark"))return benchmark();
    return gaps()&&surfaces()&&cancellation()?0:1;
}
