#include "result_regions_v1.h"
#include <pcv/registration/handeye_transform.h>
#include <QElapsedTimer>
#include <cmath>
#include <limits>
#include <new>
#include <QStringList>
#include <utility>

namespace pcv::demo {
namespace {
bool finite(const QVector3D &p) { return std::isfinite(p.x())&&std::isfinite(p.y())&&std::isfinite(p.z()); }
bool appendTransformed(pcv::planefitting::PlaneMaskMeshV1 &out,
    const pcv::planefitting::PlaneMaskMeshV1 &source,const QMatrix4x4 &correction,
    const std::function<bool()> &cancelled,QString *error) {
    const auto fail=[&]{*error=QStringLiteral("干涉区域网格或变换无效");return false;};
    if(!pointcloud::validateRigidTransform(correction,error))return false;
    if(source.triangleIndices.size()%3||source.vertices.size()+out.vertices.size()>std::numeric_limits<int>::max()
        ||source.boundaryVertices.size()+out.boundaryVertices.size()>std::numeric_limits<int>::max())return fail();
    // Validate before appending so a bad frame cannot leave a partial region.
    for(qsizetype i=0;i<source.vertices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        if(!finite(source.vertices.at(i))||!finite(correction.map(source.vertices.at(i))))return fail();
    }
    for(qsizetype i=0;i<source.triangleIndices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        if(source.triangleIndices.at(i)>=quint32(source.vertices.size()))return fail();
    }
    for(qsizetype i=0;i<source.boundaryVertices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        if(!finite(source.boundaryVertices.at(i))||!finite(correction.map(source.boundaryVertices.at(i))))return fail();
    }
    for(const auto &range:source.boundaryRanges) {
        if(range.first<0||range.second<4||qint64(range.first)+range.second>source.boundaryVertices.size()
            ||source.boundaryVertices.at(range.first)!=source.boundaryVertices.at(range.first+range.second-1))return fail();
    }
    const auto offset=quint32(out.vertices.size());const int boundaryOffset=int(out.boundaryVertices.size());
    for(qsizetype i=0;i<source.vertices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        out.vertices.append(correction.map(source.vertices.at(i)));
    }
    for(qsizetype i=0;i<source.triangleIndices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        out.triangleIndices.append(offset+source.triangleIndices.at(i));
    }
    for(qsizetype i=0;i<source.boundaryVertices.size();++i) {
        if((i&4095)==0&&cancelled())return false;
        out.boundaryVertices.append(correction.map(source.boundaryVertices.at(i)));
    }
    for(const auto &range:source.boundaryRanges)out.boundaryRanges.append({boundaryOffset+range.first,range.second});
    return true;
}
}
ResultRegionsResultV1 buildResultRegionsV1(const ResultRegionsRequestV1 &r) {
    ResultRegionsResultV1 out;out.basis=r.basis;
    QElapsedTimer total,stage;total.start();stage.start();
    const auto cancelled=[&]{return r.isCancelled&&r.isCancelled();};
    const auto stop=[&]{out.cancelled=true;out.workpiece={};out.interference={};out.workpieceOk=out.interferenceOk=false;
        out.error=QStringLiteral("区域生成已取消");return out;};
    const auto fail=[&](const QString &message){out.error=message;out.performance.insert("regions_total_ms",total.nsecsElapsed()/1e6);return out;};
    if(cancelled())return stop();
    try {
        if(r.frames.size()!=r.corrections.size()||r.referenceKeptIndex<0||r.referenceKeptIndex>=r.corrections.size())
            return fail(QStringLiteral("结果区域缺少有效帧与拼接修正映射"));
        bool redOk=true;QStringList redErrors;
        for(qsizetype i=0;i<r.frames.size();++i) {
            if(cancelled())return stop();
            const auto &frame=r.frames.at(i);QString error=frame.error;
            if(error.isEmpty()&&!appendTransformed(out.interference,frame.mask,r.corrections.at(i),cancelled,&error)) {
                if(cancelled())return stop();
                if(error.isEmpty())error=QStringLiteral("干涉区域变换失败");
            }
            if(!error.isEmpty()){redOk=false;redErrors.append(QStringLiteral("干涉帧 %1：%2").arg(i+1).arg(error));}
        }
        out.interferenceOk=redOk;
        const QString redError=redErrors.join('\n');
        out.performance.insert("regions_interference_ms",stage.nsecsElapsed()/1e6);stage.restart();
        if(!out.basis.valid) {
            const auto &correction=r.corrections.at(r.referenceKeptIndex);QString error;
            if(!pointcloud::validateRigidTransform(correction,&error))return fail(error);
            const auto n=correction.mapVector(r.reference.normal).normalized();
            const auto center=correction.map(r.reference.center);
            if(!finite(n)||!finite(center)||n.lengthSquared()<0.5f)return fail(QStringLiteral("结果参考平面无效"));
            out.basis.model.a=n.x();out.basis.model.b=n.y();out.basis.model.c=n.z();
            out.basis.model.d=-QVector3D::dotProduct(n,center);
            // Match module 60's default projection axis, in corrected coordinates.
            const auto ref=std::abs(n.z())<0.9f?QVector3D(0,0,1):QVector3D(0,1,0);
            out.basis.axisU=QVector3D::crossProduct(ref,n).normalized();out.basis.valid=true;
        }
        QVector<int> indices;indices.reserve(r.points.size());
        for(int i=0;i<r.points.size();++i) {
            if((i&4095)==0&&cancelled())return stop();
            indices.append(i);
        }
        for(int index:r.removed)if(index<0||index>=r.points.size())return fail(QStringLiteral("区域修剪索引越界"));
        pcv::planefitting::PlaneMaskOptionsV1 options;
        options.samplingAxisU=out.basis.axisU;options.spacingUMm=out.basis.spacingU;options.spacingVMm=out.basis.spacingV;
        options.fixedGridOrigin=out.basis.fixedGridOrigin;
        options.gridOriginU=out.basis.gridOriginU;options.gridOriginV=out.basis.gridOriginV;
        options.processingBudget=r.budget;options.isCancelled=cancelled;
        // Resolve spacing from the immutable complete result, even on retry after trimming.
        const bool unresolved=options.spacingUMm<=0||options.spacingVMm<=0||!options.fixedGridOrigin;
        pcv::planefitting::PlaneMaskResultV1 mask;
        if(unresolved||r.removed.isEmpty()) {
            mask=pcv::planefitting::buildPlaneMaskV1(r.points,indices,out.basis.model,options);
            if(mask.cancelled||cancelled())return stop();
            if(mask.spacingUMm>0&&mask.spacingVMm>0) {
                out.basis.spacingU=options.spacingUMm=mask.spacingUMm;out.basis.spacingV=options.spacingVMm=mask.spacingVMm;
                out.basis.gridOriginU=options.gridOriginU=mask.gridOriginU;out.basis.gridOriginV=options.gridOriginV=mask.gridOriginV;
                out.basis.fixedGridOrigin=options.fixedGridOrigin=true;
            }
            if(!mask.ok)return fail(QStringLiteral("工件区域生成失败：%1\n%2").arg(mask.error,redError).trimmed());
            if(!r.removed.isEmpty())mask.mesh={}; // Release the full mesh before building the trimmed region.
        }
        out.performance.insert("regions_full_basis_ms",stage.nsecsElapsed()/1e6);stage.restart();
        if(!r.removed.isEmpty()) {
            indices.clear();
            for(int i=0;i<r.points.size();++i) {
                if((i&4095)==0&&cancelled())return stop();
                if(!r.removed.contains(i))indices.append(i);
            }
            if(indices.isEmpty()){mask={};mask.ok=true;}
            else mask=pcv::planefitting::buildPlaneMaskV1(r.points,indices,out.basis.model,options);
        }
        if(mask.cancelled||cancelled())return stop();
        for(auto it=mask.performance.begin();it!=mask.performance.end();++it)out.performance.insert(it.key(),it.value());
        out.performance.insert("regions_retained_ms",stage.nsecsElapsed()/1e6);
        out.performance.insert("regions_full_points",double(r.points.size()));out.performance.insert("regions_kept_points",double(r.points.size()-r.removed.size()));
        out.performance.insert("regions_vertices",double(mask.mesh.vertices.size()));out.performance.insert("regions_triangles",double(mask.mesh.triangleIndices.size()/3));
        out.performance.insert("regions_red_vertices",double(out.interference.vertices.size()));
        out.performance.insert("regions_total_ms",total.nsecsElapsed()/1e6);
        out.workpieceOk=mask.ok;if(mask.ok)out.workpiece=std::move(mask.mesh);
        out.error=mask.ok?redError:QStringLiteral("工件区域生成失败：%1\n%2").arg(mask.error,redError).trimmed();
        // Match module 20's int-sized GPU buffer contract before publication.
        const auto vertexBytes=(out.workpiece.vertices.size()+out.workpiece.boundaryVertices.size()
            +out.interference.vertices.size()+out.interference.boundaryVertices.size())*qint64(sizeof(QVector3D));
        const auto indexBytes=(out.workpiece.triangleIndices.size()+out.interference.triangleIndices.size())*qint64(sizeof(quint32));
        if(vertexBytes>std::numeric_limits<int>::max()||indexBytes>std::numeric_limits<int>::max()) {
            out.workpiece={};out.interference={};out.workpieceOk=out.interferenceOk=false;
            return fail(QStringLiteral("结果区域超过画布缓冲范围，点云保留"));
        }
        return out;
    }catch(const std::bad_alloc &){
        out.workpiece={};out.interference={};out.workpieceOk=out.interferenceOk=false;
        return fail(QStringLiteral("结果区域生成内存不足"));
    }
}
} // namespace pcv::demo
