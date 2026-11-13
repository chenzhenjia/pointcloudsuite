#include <pcv/stitching/selected_plane_stitch.h>
#include "selected_plane_refinement.h"
#include <pcv/io/artifact_transaction.h>
#include <pcv/registration/handeye_transform.h>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QQuaternion>
#include <QtMath>
#include <cmath>
#include <new>

namespace pcv::stitching {
namespace {
QJsonArray matrixJson(const QMatrix4x4 &m) {
    QJsonArray a; for(int r=0;r<4;++r) { QJsonArray row; for(int c=0;c<4;++c) row.append(m(r,c)); a.append(row); } return a;
}
QJsonArray vectorJson(const QVector3D &v) { return {v.x(),v.y(),v.z()}; }
bool finite(const QVector3D &v) { return std::isfinite(v.x())&&std::isfinite(v.y())&&std::isfinite(v.z()); }
SelectedPlaneStitchResult run(const SelectedPlaneStitchRequest &request) {
    SelectedPlaneStitchResult result;
    QElapsedTimer total; total.start();
    const auto cancelled=[&request] { return request.isCancelled&&request.isCancelled(); };
    const auto fail=[&result,&cancelled](const QString &code,const QString &message) {
        result.success=false; result.cancelled=cancelled(); result.errorCode=code; result.message=message;
        result.report.insert("formal_output",false);
        result.report.insert("error_code",code);
        result.report.insert("error_message",message);
        result.points.clear(); result.cloudIds.clear(); result.sourceIndices.clear(); result.scanRatios.clear();
        return result;
    };
    if(request.frames.size()<2 || request.outputDirectory.isEmpty()) return fail(QStringLiteral("selected_plane_input_invalid"),QStringLiteral("至少需要两帧所选平面及输出目录"));
    if(!std::isfinite(request.options.maximumTranslationMm)||request.options.maximumTranslationMm<=0||request.options.maximumTranslationMm>10
       ||!std::isfinite(request.options.maximumAngleDegrees)||request.options.maximumAngleDegrees<=0||request.options.maximumAngleDegrees>1
       ||request.options.maxParallelTasks<0||request.options.maxParallelTasks>4)
        return fail(QStringLiteral("selected_plane_options_invalid"),QStringLiteral("修正预算不得超过 10 mm/1°，任务数为 0～4"));
    QJsonArray framesJson,pairsJson;
    qint64 selectionMs=0;
    for(int f=0;f<request.frames.size();++f) {
        if(cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
        const auto &frame=request.frames.at(f); const auto &cloud=frame.cloud;
        if(cloud.fullPoints.isEmpty()||cloud.fullPoints.size()!=cloud.sourceIndices.size()||cloud.fullPoints.size()!=cloud.scanRatios.size()
            ||cloud.declaredCount<=0||frame.originalValidPointCount<cloud.fullPoints.size()
            ||!finite(frame.normal)||frame.normal.lengthSquared()<1e-12f||!finite(frame.center)||!std::isfinite(frame.rmsMm)||frame.rmsMm<0
            ||!pointcloud::validateRigidTransform(cloud.startBaseFromFlange)||!pointcloud::validateRigidTransform(cloud.endBaseFromFlange)
            ||!pointcloud::validateRigidTransform(cloud.flangeFromDepth))
            return fail(QStringLiteral("selected_plane_frame_invalid"),QStringLiteral("第 %1 帧点集、平面或元数据无效").arg(f+1));
        QSet<qsizetype> indices;
        for(qsizetype i=0;i<cloud.fullPoints.size();++i) {
            if((i%4096)==0&&cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
            const auto &p=cloud.fullPoints.at(i); const auto index=cloud.sourceIndices.at(i);
            if(!finite(QVector3D(p.x,p.y,p.z))||!finite(QVector3D(p.nx,p.ny,p.nz))||!std::isfinite(cloud.scanRatios.at(i))
                ||index<0||index>=cloud.declaredCount||indices.contains(index))
                return fail(QStringLiteral("selected_plane_provenance_invalid"),QStringLiteral("第 %1 帧点或来源索引无效").arg(f+1));
            indices.insert(index);
        }
        QJsonArray seeds;
        if(frame.selectionMethod!=PlaneSelectionMethod::ThreePoint && frame.selectionMethod!=PlaneSelectionMethod::SingleClick)
            return fail(QStringLiteral("selected_plane_seeds_invalid"),QStringLiteral("选面方式无效"));
        const int requiredSeeds=frame.selectionMethod==PlaneSelectionMethod::SingleClick?1:3;
        if(frame.seedSourceIndices.size()!=requiredSeeds) return fail(QStringLiteral("selected_plane_seeds_invalid"),QStringLiteral("选面方式与原始控制点数量不一致"));
        QSet<qsizetype> uniqueSeeds;
        for(auto index:frame.seedSourceIndices) {
            if(index<0||index>=cloud.declaredCount||uniqueSeeds.contains(index)
                ||(frame.selectionMethod==PlaneSelectionMethod::SingleClick && !indices.contains(index)))
                return fail(QStringLiteral("selected_plane_seeds_invalid"),QStringLiteral("控制点索引越界、重复或点击点不在所选平面内"));
            uniqueSeeds.insert(index); seeds.append(double(index));
        }
        const QVector3D normal=frame.normal.normalized();
        framesJson.append(QJsonObject{{"source_file",cloud.sourceFile},{"declared_point_count",double(cloud.declaredCount)},
            {"original_valid_point_count",double(frame.originalValidPointCount)},{"selected_point_count",double(cloud.fullPoints.size())},
            {"seed_source_indices",seeds},{"plane_normal",vectorJson(normal)},{"plane_center",vectorJson(frame.center)},
            {"selection_method",frame.selectionMethod==PlaneSelectionMethod::SingleClick?"single_click":"three_point"},
            {"selection_parameters",frame.selectionParameters},
            {"plane_offset_mm",-QVector3D::dotProduct(normal,frame.center)},{"plane_rms_mm",frame.rmsMm},{"selection_ms",double(frame.selectionMs)},
            {"start_base_from_flange",matrixJson(cloud.startBaseFromFlange)},{"end_base_from_flange",matrixJson(cloud.endBaseFromFlange)},
            {"flange_from_depth",matrixJson(cloud.flangeFromDepth)}});
        selectionMs+=frame.selectionMs;
    }
    QElapsedTimer registration; registration.start();
    result.corrections.resize(request.frames.size());
    for(int i=1;i<request.frames.size();++i) {
        if(cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
        const auto progress=[&request,i](float value,const QString &message) {
            if(request.progress) request.progress((float(i-1)+value)/float(request.frames.size()-1)*0.8f,message);
        };
        auto pair=pointcloud::refineSelectedPlanePair(request.frames.at(i),request.frames.at(i-1),request.options,request.isCancelled,progress);
        if(cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
        QMatrix4x4 cumulative;
        if(pair.fineApplied) cumulative=result.corrections.at(i-1)*pair.pairCorrection;
        pair.cumulativeCorrection=cumulative; result.corrections[i]=cumulative; result.pairs.push_back(pair);
        QJsonArray iterations; for(int v:pair.iterations) iterations.append(v);
        pairsJson.append(QJsonObject{{"source_frame",i},{"target_frame",i-1},{"fine_applied",pair.fineApplied},{"pose_fallback",pair.poseFallback},
            {"chain_anchor_reset",pair.chainAnchorReset},{"reason",pair.reason},{"completed_levels",pair.completedLevels},{"iterations",iterations},
            {"correspondences",pair.correspondences},{"rmse_mm",pair.rmseMm},{"matched_ratio",pair.matchedRatio},{"observable_dof",pair.observableDof},
            {"degenerate",pair.degenerate},{"converged",pair.converged},{"pair_matrix",matrixJson(pair.pairCorrection)},
            {"cumulative_matrix",matrixJson(cumulative)},{"cumulative_translation_mm",QVector3D(cumulative(0,3),cumulative(1,3),cumulative(2,3)).length()},
            {"cumulative_angle_deg",qRadiansToDegrees(2.0f*std::acos(qBound(0.0f,std::abs(QQuaternion::fromRotationMatrix(cumulative.normalMatrix()).normalized().scalar()),1.0f)))},
            {"accepted",QJsonValue(QJsonValue::Null)},{"structural_coverage_3mm",QJsonValue(QJsonValue::Null)}});
    }
    const qint64 registrationMs=registration.elapsed();
    if(request.progress) request.progress(0.8f,QStringLiteral("正在按原点序合并所选平面"));
    QElapsedTimer mergeTimer; mergeTimer.start();
    for(int f=0;f<request.frames.size();++f) {
        const auto &cloud=request.frames.at(f).cloud; const auto &matrix=result.corrections.at(f);
        for(qsizetype i=0;i<cloud.fullPoints.size();++i) {
            if(i%4096==0&&cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
            auto p=cloud.fullPoints.at(i); const auto v=matrix.map(QVector3D(p.x,p.y,p.z));
            const auto n=matrix.mapVector(QVector3D(p.nx,p.ny,p.nz));
            if(!finite(v)||!finite(n)) return fail(QStringLiteral("selected_plane_transform_invalid"),QStringLiteral("累计转换出现非有限点"));
            p.x=v.x();p.y=v.y();p.z=v.z();p.nx=n.x();p.ny=n.y();p.nz=n.z();
            result.points.push_back(p);result.cloudIds.push_back(f);result.sourceIndices.push_back(cloud.sourceIndices.at(i));result.scanRatios.push_back(cloud.scanRatios.at(i));
        }
    }
    const qint64 mergeMs=mergeTimer.elapsed();
    const QString ply=QStringLiteral("selected_plane_robot_base.ply"),map=QStringLiteral("selected_plane_robot_base.pcvmap"),report=QStringLiteral("selected_plane_manifest.json");
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0x314D4350)<<quint32(1)<<quint64(result.points.size());
    for(qsizetype i=0;i<result.points.size();++i) {
        if(i%4096==0&&cancelled()) return fail(QStringLiteral("cancelled"),QStringLiteral("已取消"));
        stream<<qint32(result.cloudIds.at(i))<<qint64(result.sourceIndices.at(i))<<result.scanRatios.at(i);
    }
    if(stream.status()!=QDataStream::Ok) return fail(QStringLiteral("selected_plane_mapping_failed"),QStringLiteral("来源映射序列化失败"));
    if(request.progress) request.progress(0.9f,QStringLiteral("正在事务化输出所选平面"));
    io::ArtifactTransaction transaction(request.outputDirectory,request.allowOverwrite,request.isCancelled);
    QElapsedTimer output;output.start();
    if(!transaction.stageBinaryPly(ply,result.points,true,{QStringLiteral("robot_base selected measured plane points")})||!transaction.stageBytes(map,bytes))
        return fail(QStringLiteral("selected_plane_output_failed"),transaction.error());
    int applied=0;for(const auto &p:result.pairs) if(p.fineApplied)++applied;
    result.report=QJsonObject{{"processing_mode","selected_plane_stitch"},{"quality_policy","warn_only"},{"strict_validation_status","not_run"},
        {"formal_output",true},{"coordinate_frame","robot_base"},{"calibration_path",request.calibrationPath},{"fine_enabled",request.options.fineEnabled},
        {"maximum_pair_translation_mm",request.options.maximumTranslationMm},{"maximum_pair_angle_deg",request.options.maximumAngleDegrees},
        {"frames",framesJson},{"pairs",pairsJson},{"point_count",double(result.points.size())},{"fine_applied_count",applied},
        {"pose_fallback_count",result.pairs.size()-applied},{"ply_file",ply},{"mapping_file",map},{"selection_ms",double(selectionMs)},
        {"registration_ms",double(registrationMs)},{"merge_ms",double(mergeMs)},{"output_stage_ms",double(output.elapsed())},
        {"total_precommit_ms",double(total.elapsed())},{"timing_scope","output_stage excludes manifest staging and commit"}};
    if(!transaction.stageJson(report,QJsonDocument(result.report).toJson())||!transaction.commit()) return fail(QStringLiteral("selected_plane_output_failed"),transaction.error());
    result.report.insert("output_commit_wall_ms",double(output.elapsed()));result.report.insert("total_wall_ms",double(total.elapsed()));
    result.success=true;result.outputPly=QDir(request.outputDirectory).filePath(ply);result.mappingFile=QDir(request.outputDirectory).filePath(map);
    result.reportFile=QDir(request.outputDirectory).filePath(report);result.message=QStringLiteral("正式文件已提交；几何质量仅警告，未执行旧严格验收");
    if(request.progress) request.progress(1,result.message);
    return result;
}
}
SelectedPlaneStitchResult stitchSelectedPlanes(const SelectedPlaneStitchRequest &request) {
    try { return run(request); }
    catch(const std::bad_alloc &) { SelectedPlaneStitchResult r;r.errorCode=QStringLiteral("selected_plane_memory_failed");r.message=QStringLiteral("内存不足；未发布部分结果");return r; }
}
}
