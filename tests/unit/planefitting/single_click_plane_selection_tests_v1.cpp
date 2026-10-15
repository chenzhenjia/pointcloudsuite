#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QCoreApplication>
#include <QtMath>
#include <atomic>
#include <iostream>
#include <limits>

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
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
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
