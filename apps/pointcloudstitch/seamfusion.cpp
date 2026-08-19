#include "seamfusion.h"

#include <QHash>
#include <QSet>
#include <QVector3D>
#include <QtMath>

#include <cmath>
#include <limits>

namespace {
struct Cell3 { qint64 x=0,y=0,z=0; bool operator==(const Cell3&o)const{return x==o.x&&y==o.y&&z==o.z;} };
size_t qHash(const Cell3&c,size_t seed=0)noexcept{return qHashMulti(seed,c.x,c.y,c.z);}
struct Cell2 { qint64 distance=0,travel=0; bool operator==(const Cell2&o)const{return distance==o.distance&&travel==o.travel;} };
size_t qHash(const Cell2&c,size_t seed=0)noexcept{return qHashMulti(seed,c.distance,c.travel);}
QVector3D pv(const pointcloud::Point3D&p){return{p.x,p.y,p.z};}
QVector3D tr(const QMatrix4x4&m){return{m(0,3),m(1,3),m(2,3)};}
Cell3 cell3(const QVector3D&p,float s){return{qint64(std::floor(p.x()/s)),qint64(std::floor(p.y()/s)),qint64(std::floor(p.z()/s))};}
pointcloud::Point3D blended(const pointcloud::Point3D&a,const pointcloud::Point3D&b,float w){
    pointcloud::Point3D p; const float v=1-w; p.x=a.x*v+b.x*w;p.y=a.y*v+b.y*w;p.z=a.z*v+b.z*w;
    QVector3D n(a.nx*v+b.nx*w,a.ny*v+b.ny*w,a.nz*v+b.nz*w);if(n.lengthSquared()>1e-12f)n.normalize();p.nx=n.x();p.ny=n.y();p.nz=n.z();return p;
}
struct Seam {
    QVector3D origin,normal,travel;
    float offset=0;
    float projectedAMin=0, projectedAMax=0, projectedBMin=0, projectedBMax=0;
    float overlapMin=0, overlapMax=0;
    bool actualOverlapValid=false;
};
}

SeamFusionResult applyTrajectorySeamFusion(pointcloud::WorldCloudMergeResult *merge,
    const QVector<pointcloud::WorldCloudInput>&inputs,const SeamFusionOptions&o)
{
    SeamFusionResult r;if(!merge)return r;r.inputPoints=merge->points.size();
    if(!o.enabled||inputs.size()<2||merge->points.isEmpty()||o.halfWidth<=0||o.mutualDistance<=0||o.decisionCellSize<=0){r.outputPoints=r.inputPoints;return r;}
    QVector<QVector3D> centers,directions;
    for(int index=0;index<inputs.size();++index){const auto&i=inputs[index];const QVector3D a=tr(i.startBaseFromFlange),b=tr(i.endBaseFromFlange),d=b-a;const QMatrix4x4 correction=merge->registrationCorrections.value(index,QMatrix4x4());centers<<correction.map((a+b)*.5f);const QVector3D correctedDirection=correction.mapVector(d);if(correctedDirection.lengthSquared()<=1e-12f){r.outputPoints=r.inputPoints;return r;}directions<<correctedDirection.normalized();}
    QVector3D axis=centers.last()-centers.first();if(axis.lengthSquared()<=1e-12f){r.outputPoints=r.inputPoints;return r;}axis.normalize();
    for(int i=1;i<centers.size();++i)if(QVector3D::dotProduct(centers[i]-centers[i-1],axis)<=1e-6f){SeamFusionDiagnostic d;d.cloudA=i-1;d.cloudB=i;d.reason=QStringLiteral("扫描未沿同一拼接方向排序，已拒绝接缝裁剪");r.diagnostics<<d;r.outputPoints=r.inputPoints;return r;}
    QVector<Seam> seams;
    for(int i=0;i+1<centers.size();++i){
        Seam s;s.origin=(centers[i]+centers[i+1])*.5f;s.normal=(centers[i+1]-centers[i]).normalized();
        s.offset=QVector3D::dotProduct(s.origin,s.normal);s.travel=directions[i]+directions[i+1];
        s.travel-=s.normal*QVector3D::dotProduct(s.travel,s.normal);
        if(s.travel.lengthSquared()<=1e-12f){SeamFusionDiagnostic d;d.cloudA=i;d.cloudB=i+1;d.reason=QStringLiteral("无法确定接缝处扫描行程方向");r.diagnostics<<d;r.outputPoints=r.inputPoints;return r;}
        s.travel.normalize();
        // Derive the seam from the actual post-ICP point ranges, not the robot
        // trajectory midpoint. This handles scans whose valid profile region
        // is offset from the commanded path.
        s.projectedAMin=std::numeric_limits<float>::max();s.projectedAMax=-s.projectedAMin;
        s.projectedBMin=std::numeric_limits<float>::max();s.projectedBMax=-s.projectedBMin;
        for(int k=0;k<merge->points.size();++k){
            const int id=merge->cloudIds.value(k,-1);if(id!=i&&id!=i+1)continue;
            const float projection=QVector3D::dotProduct(pv(merge->points[k]),s.normal);
            if(id==i){s.projectedAMin=qMin(s.projectedAMin,projection);s.projectedAMax=qMax(s.projectedAMax,projection);}
            else{s.projectedBMin=qMin(s.projectedBMin,projection);s.projectedBMax=qMax(s.projectedBMax,projection);}
        }
        s.overlapMin=qMax(s.projectedAMin,s.projectedBMin);s.overlapMax=qMin(s.projectedAMax,s.projectedBMax);
        s.actualOverlapValid=s.projectedAMin<=s.projectedAMax&&s.projectedBMin<=s.projectedBMax&&s.overlapMax>s.overlapMin;
        if(s.actualOverlapValid){s.offset=(s.overlapMin+s.overlapMax)*.5f;s.origin=s.normal*s.offset;}
        seams<<s;
    }

    QVector<QVector<int>> left(seams.size()),right(seams.size());
    for(int k=0;k<merge->points.size();++k){const int id=merge->cloudIds.value(k,-1);if(id<0||id>=inputs.size())continue;const QVector3D p=pv(merge->points[k]);
        if(id>0&&seams[id-1].actualOverlapValid){const int s=id-1;const float d=QVector3D::dotProduct(p,seams[s].normal)-seams[s].offset;if(std::abs(d)<o.halfWidth)right[s]<<k;}
        if(id+1<inputs.size()&&seams[id].actualOverlapValid){const int s=id;const float d=QVector3D::dotProduct(p,seams[s].normal)-seams[s].offset;if(std::abs(d)<o.halfWidth)left[s]<<k;}}
    QVector<bool> seamUsable(seams.size(),false);
    for(int s=0;s<seams.size();++s)seamUsable[s]=seams[s].actualOverlapValid&&!left[s].isEmpty()&&!right[s].isEmpty();
    QVector<bool> keepCore(merge->points.size(),true);
    for(int k=0;k<merge->points.size();++k){const int id=merge->cloudIds.value(k,-1);if(id<0||id>=inputs.size())continue;const QVector3D p=pv(merge->points[k]);
        if(id>0&&seamUsable[id-1]){const int s=id-1;const float d=QVector3D::dotProduct(p,seams[s].normal)-seams[s].offset;keepCore[k]=keepCore[k]&&d>=o.halfWidth;}
        if(id+1<inputs.size()&&seamUsable[id]){const int s=id;const float d=QVector3D::dotProduct(p,seams[s].normal)-seams[s].offset;keepCore[k]=keepCore[k]&&d<=-o.halfWidth;}}
    QVector<pointcloud::Point3D> out;QVector<int> ids;QVector<qsizetype> sources;QVector<float> ratios;out.reserve(merge->points.size());ratios.reserve(merge->points.size());
    auto append=[&](const pointcloud::Point3D&p,int id,qsizetype source,float ratio){out<<p;ids<<id;sources<<source;ratios<<ratio;};
    for(int i=0;i<merge->points.size();++i)if(keepCore[i])append(merge->points[i],merge->cloudIds[i],merge->sourceIndices[i],merge->scanRatios.value(i,0.0f));
    const float limit2=o.mutualDistance*o.mutualDistance;
    for(int si=0;si<seams.size();++si){SeamFusionDiagnostic d;d.cloudA=si;d.cloudB=si+1;d.projectedAMin=seams[si].projectedAMin;d.projectedAMax=seams[si].projectedAMax;d.projectedBMin=seams[si].projectedBMin;d.projectedBMax=seams[si].projectedBMax;d.actualOverlapMin=seams[si].overlapMin;d.actualOverlapMax=seams[si].overlapMax;d.seamProjection=seams[si].offset;d.actualOverlapValid=seams[si].actualOverlapValid;d.bandPointsA=left[si].size();d.bandPointsB=right[si].size();d.bandPoints=d.bandPointsA+d.bandPointsB;d.corePoints=out.size();
        if(!seams[si].actualOverlapValid){d.applied=false;d.reason=QStringLiteral("seam_outside_actual_overlap：真实投影区无有效重叠，保留两帧完整点云");r.diagnostics<<d;continue;}
        if(left[si].isEmpty()||right[si].isEmpty()){d.applied=false;d.reason=QStringLiteral("seam_outside_actual_overlap：融合带仅有单侧点，禁止裁剪并保留两帧完整点云");r.diagnostics<<d;continue;}
        auto nearest=[&](const QVector<int>&from,const QVector<int>&to){QHash<Cell3,QVector<int>> grid;for(int i:to)grid[cell3(pv(merge->points[i]),o.mutualDistance)]<<i;QHash<int,int> answer;
            for(int source:from){const QVector3D p=pv(merge->points[source]);const Cell3 c=cell3(p,o.mutualDistance);float best=limit2;int selected=-1;
                for(qint64 z=-1;z<=1;++z)for(qint64 y=-1;y<=1;++y)for(qint64 x=-1;x<=1;++x){auto f=grid.constFind({c.x+x,c.y+y,c.z+z});if(f==grid.cend())continue;for(int candidate:f.value()){float q=(pv(merge->points[candidate])-p).lengthSquared();if(q<=best){best=q;selected=candidate;}}}
                if(selected>=0)answer.insert(source,selected);}return answer;};
        const auto lr=nearest(left[si],right[si]),rl=nearest(right[si],left[si]);QSet<int> ml,mr;
        for(auto it=lr.cbegin();it!=lr.cend();++it){const int li=it.key(),ri=it.value();if(rl.value(ri,-1)!=li)continue;ml<<li;mr<<ri;const float sd=QVector3D::dotProduct((pv(merge->points[li])+pv(merge->points[ri]))*.5f,seams[si].normal)-seams[si].offset;const float w=qBound(0.f,(sd+o.halfWidth)/(2*o.halfWidth),1.f);const int chosen=w>=.5f?ri:li;const float ratio=merge->scanRatios.value(chosen,0.0f);append(blended(merge->points[li],merge->points[ri],w),merge->cloudIds[chosen],merge->sourceIndices[chosen],ratio);++d.mutualPairs;++d.interpolatedPoints;}
        QHash<Cell2,QVector<QPair<int,bool>>> cells;
        auto addUnmatched=[&](const QVector<int>&indices,const QSet<int>&matched,bool isRight){for(int index:indices)if(!matched.contains(index)){const QVector3D p=pv(merge->points[index]);const float sd=QVector3D::dotProduct(p,seams[si].normal)-seams[si].offset;const float travel=QVector3D::dotProduct(p,seams[si].travel);const Cell2 cell{qint64(std::floor(sd/o.decisionCellSize)),qint64(std::floor(travel/o.decisionCellSize))};cells[cell].push_back(qMakePair(index,isRight));}};
        addUnmatched(left[si],ml,false);addUnmatched(right[si],mr,true);
        for(auto it=cells.cbegin();it!=cells.cend();++it){bool hasLeft=false,hasRight=false;for(const auto&entry:it.value()){hasRight=hasRight||entry.second;hasLeft=hasLeft||!entry.second;}const double centerDistance=(double(it.key().distance)+0.5)*o.decisionCellSize;const double probability=qBound(0.0,(centerDistance+o.halfWidth)/(2.0*o.halfWidth),1.0);const quint64 hash=(quint64(it.key().distance)*73856093ULL)^(quint64(it.key().travel)*19349663ULL);bool chooseRight=double(hash&0xffffffffULL)/4294967296.0<probability;chooseRight=chooseRight&&hasRight;chooseRight=chooseRight||(!hasLeft&&hasRight);for(const auto&entry:it.value())if(entry.second==chooseRight){append(merge->points[entry.first],merge->cloudIds[entry.first],merge->sourceIndices[entry.first],merge->scanRatios.value(entry.first,0.0f));++d.unmatchedPreserved;}else++d.unmatchedDiscarded;++d.decisionCells;}
        d.applied=true;d.reason=QStringLiteral("参考羽化接缝：互为最近邻插值，未匹配点按二维决策块选择单一来源");r.diagnostics<<d;}
    merge->points=std::move(out);merge->cloudIds=std::move(ids);merge->sourceIndices=std::move(sources);merge->scanRatios=std::move(ratios);r.outputPoints=merge->points.size();return r;
}
