#include "selected_plane_tasks_v2.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/workpiece/temp_workpiece_interface.h>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QVector3D>
#include <cmath>
#include <algorithm>
#include <limits>
#include <new>
#include <utility>
#include <type_traits>
namespace pcv::ui {
bool parseSelectedPlaneScanJsonV2(const QString &path, SelectedPlaneInputV2 *input, QString *error) {
    if (!input) return false;
    pcv::interface::TempScanningInfo scan;
    if (!pcv::interface::parseTempScanningInfo(path, &scan, error)) return false;
    const QDir directory(QFileInfo(path).absolutePath());
    SelectedPlaneInputV2 parsed;
    parsed.path = QDir::cleanPath(directory.absoluteFilePath(scan.pointCloudFile));
    parsed.calibration = QDir::cleanPath(directory.absoluteFilePath(scan.calibrationFile));
    parsed.scanJson = QFileInfo(path).absoluteFilePath();
    parsed.scanId = scan.scanId;
    parsed.layout = scan.pointCloudLayout;
    parsed.warning = scan.warning;
    parsed.start = scan.robotPoseStart;
    parsed.end = scan.robotPoseEnd;
    *input = parsed;
    return true;
}
using SelectedPlaneTask = SelectedPlaneTaskV2;
constexpr qsizetype kCancellationCheckStride = 4096;
namespace {
SelectedPlaneTask finishPlaneSnapshotV2(SelectedPlaneTask task, const pointcloud::RobotBaseFrame &source,
    const QVector<int> &seeds, const pcv::planefitting::PlaneModel &model, float rms,
    const QVector3D &samplingAxisU, const CancellationTokenV2 &token,
    const std::shared_ptr<pcv::ProcessingBudget> &budget) {
    QElapsedTimer timer,stage;timer.start();stage.start();
    if(!token||token->load()) {task.cancelled=true;return task;}
    if(source.fullPoints.size()!=source.sourceIndices.size()||source.fullPoints.size()!=source.scanRatios.size()
        ||task.planeIndices.isEmpty()) {task.error=QStringLiteral("完整选面快照的来源映射或索引无效");return task;}
    for(int seed:seeds)if(seed<0||seed>=source.fullPoints.size()) {task.error=QStringLiteral("选面控制点越界");return task;}
    if(!std::is_sorted(task.planeIndices.cbegin(),task.planeIndices.cend()))
        std::sort(task.planeIndices.begin(),task.planeIndices.end());
    int previous=-1;
    for(int i:std::as_const(task.planeIndices)) {
        if(token->load()){task.cancelled=true;return task;}
        if(i<0||i>=source.fullPoints.size()||i==previous){task.error=QStringLiteral("完整选面索引重复或越界");return task;}
        previous=i;
    }
    task.performance.insert("snapshot_validation_ms",stage.nsecsElapsed()/1e6);stage.restart();
    auto &selected=task.selected;selected.cloud=source;
    selected.cloud.fullPoints.clear();selected.cloud.samplePoints.clear();selected.cloud.sourceIndices.clear();selected.cloud.scanRatios.clear();
    const auto count=task.planeIndices.size();
    selected.cloud.fullPoints.reserve(count);selected.cloud.sourceIndices.reserve(count);selected.cloud.scanRatios.reserve(count);
    for(int i:std::as_const(task.planeIndices)) {
        if(token->load()){task.cancelled=true;return task;}
        selected.cloud.fullPoints.push_back(source.fullPoints.at(i));
        selected.cloud.sourceIndices.push_back(source.sourceIndices.at(i));
        selected.cloud.scanRatios.push_back(source.scanRatios.at(i));
    }
    task.snapshotMs=stage.nsecsElapsed()/1e6;
    task.performance.insert("snapshot_copy_ms",task.snapshotMs);stage.restart();
    selected.normal=QVector3D(model.a,model.b,model.c).normalized();
    const auto center=pcv::planefitting::calculateBoundsCenter(source.fullPoints,task.planeIndices);
    if(!center.ok){task.error=center.error;return task;}
    selected.center=center.center-selected.normal*(QVector3D::dotProduct(selected.normal,center.center)+model.d);
    task.performance.insert("center_ms",stage.nsecsElapsed()/1e6);stage.restart();
    pcv::planefitting::PlaneMaskOptionsV1 maskOptions;
    maskOptions.processingBudget=budget;maskOptions.isCancelled=[token]{return token->load();};
    maskOptions.samplingAxisU=samplingAxisU;
    if(selected.selectionMethod==pcv::stitching::PlaneSelectionMethod::SingleClick) {
        maskOptions.spacingUMm=selected.selectionParameters.value("sampling_spacing_u_mm").toDouble();
        maskOptions.spacingVMm=selected.selectionParameters.value("sampling_spacing_v_mm").toDouble();
    }
    const auto mask=pcv::planefitting::buildPlaneMaskV1(source.fullPoints,task.planeIndices,model,maskOptions);
    task.performance.insert("mask_ms",stage.nsecsElapsed()/1e6);
    for(auto it=mask.performance.begin();it!=mask.performance.end();++it)task.performance.insert(it.key(),it.value());
    if(mask.ok)task.planeMask=mask.mesh;else task.planeMaskError=mask.error;
    if(token->load()){task.cancelled=true;task.error=QStringLiteral("cancelled");return task;}
    selected.rmsMm=rms;selected.originalValidPointCount=source.fullPoints.size();
    selected.selectionMs=task.performance.value("algorithm_ms").toDouble()+timer.elapsed();
    for(int i:seeds)selected.seedSourceIndices.push_back(source.sourceIndices.at(i));
    task.performance.insert("postprocess_ms",timer.nsecsElapsed()/1e6);
    task.ok=true;return task;
}
}
SelectedPlaneTaskV2 snapshotFromSingleClickV2(const pointcloud::RobotBaseFrame &source,
    const QVector<int> &seeds,float tolerance,const pcv::planefitting::SingleClickResult &fit,
    const CancellationTokenV2 &token,const std::shared_ptr<pcv::ProcessingBudget> &budget) {
    SelectedPlaneTask task;task.kind=SelectedPlaneTask::Fit;
    if(!token||!fit.ok()||token->load()) {
        task.error=fit.error;task.cancelled=fit.status==pcv::planefitting::SingleClickStatus::Cancelled||(token&&token->load());
        task.outOfMemory=fit.outOfMemory;return task;
    }
    if(seeds.size()!=1||!std::isfinite(tolerance)||tolerance<=0) {task.error=QStringLiteral("选面快照参数无效");return task;}
    try {
        task.planeIndices=fit.planeIndices;
        task.projectionMs=fit.projectionMs;task.normalConnectivityMs=fit.normalConnectivityMs;task.gapMs=fit.gapMergeMs;
        task.selected.selectionMethod=pcv::stitching::PlaneSelectionMethod::SingleClick;
        auto &parameters=task.selected.selectionParameters;
        parameters.insert("surface_tolerance_mm",tolerance);
        parameters.insert("selection_timing_scope","worker_fit_classification_and_snapshot_excludes_pick_queue_and_display");
        parameters.insert("grid_size_mm",fit.gridSizeMm);parameters.insert("local_spacing_mm",fit.localSpacingMm);
        parameters.insert("algorithm_version","single_click_plane_selection_v2");
        parameters.insert("maximum_gap_mm",fit.maximumGapMm);
        parameters.insert("sampling_spacing_u_mm",fit.samplingSpacingUMm);parameters.insert("sampling_spacing_v_mm",fit.samplingSpacingVMm);
        parameters.insert("connectivity_radius_u_mm",fit.connectivityRadiusUMm);parameters.insert("connectivity_radius_v_mm",fit.connectivityRadiusVMm);
        parameters.insert("merged_region_count",fit.mergedRegionCount);parameters.insert("refinement_iterations",fit.refinementIterations);
        parameters.insert("refinement_converged",fit.refinementConverged);parameters.insert("gap_merge_ms",fit.gapMergeMs);
        parameters.insert("connectivity_radius_mm",fit.connectivityRadiusMm);parameters.insert("local_support_ratio",fit.localSupportRatio);
        parameters.insert("algorithm_ms",fit.timings.totalMs);
        parameters.insert("local_fit_ms",fit.timings.ransacMs);parameters.insert("grid_growth_ms",fit.timings.candidateMs);
        parameters.insert("classification_ms",fit.timings.classificationMs);parameters.insert("connectivity_ms",fit.timings.connectivityMs);
        parameters.insert("pca_ms",fit.timings.pcaMs);
        task.performance={{"algorithm_ms",fit.timings.totalMs},{"local_fit_ms",fit.timings.ransacMs},
            {"candidate_ms",fit.timings.candidateMs},{"classification_ms",fit.timings.classificationMs},
            {"connectivity_ms",fit.timings.connectivityMs},{"refinement_ms",fit.timings.pcaMs},
            {"projection_ms",fit.projectionMs},{"normal_connectivity_ms",fit.normalConnectivityMs},{"gap_ms",fit.gapMergeMs},
            {"connectivity_build_ms",fit.connectivityBuildMs},{"connectivity_traversal_ms",fit.connectivityTraversalMs},
            {"connectivity_cells",double(fit.connectivityCellCount)},{"connectivity_workspace_used",fit.connectivityWorkspaceUsed}};
        return finishPlaneSnapshotV2(std::move(task),source,seeds,fit.model,fit.rmsMm,fit.samplingAxisU,token,budget);
    }catch(const std::bad_alloc &){task.ok=false;task.outOfMemory=true;task.error=QStringLiteral("内存不足，保留原选择");return task;}
}
SelectedPlaneTask fitFullPlaneV2(const pointcloud::RobotBaseFrame &source,const QVector<int> &seeds,
    float tolerance,float maximumGap,bool singleClick,const pcv::planefitting::PlaneSelectionIndexPtr &spatialIndex,
    const CancellationTokenV2 &token,int workers,const std::shared_ptr<pcv::ProcessingBudget> &budget) {
    SelectedPlaneTask task;task.kind=SelectedPlaneTask::Fit;
    if(!token||source.fullPoints.size()!=source.sourceIndices.size()||source.fullPoints.size()!=source.scanRatios.size()
        ||seeds.size()!=(singleClick?1:3)||!std::isfinite(tolerance)||tolerance<=0) {
        task.error=QStringLiteral("完整选面任务的来源映射、控制点或容差无效");return task;
    }
    for(int seed:seeds)if(seed<0||seed>=source.fullPoints.size()){task.error=QStringLiteral("选面控制点越界");return task;}
    try {
        if(singleClick) {
            pcv::planefitting::SingleClickOptions options;options.surfaceToleranceMm=tolerance;options.maximumGapMm=maximumGap;
            options.processingBudget=budget;options.parallelEnabled=source.fullPoints.size()>=100000&&workers>1;
            options.maxParallelTasks=workers;options.isCancelled=[token]{return token->load();};
            const auto fit=pcv::planefitting::selectPlaneAtPoint(source.fullPoints,spatialIndex,seeds.at(0),options);
            return snapshotFromSingleClickV2(source,seeds,tolerance,fit,token,budget);
        }
        pcv::planefitting::Options options;options.initialTolerance=1;options.surfaceTolerance=tolerance;options.useZAxisResidual=false;
        options.parallelEnabled=source.fullPoints.size()>=100000&&workers>1;options.maxParallelTasks=workers;options.isCancelled=[token]{return token->load();};
        const auto fit=pcv::planefitting::fit(source.fullPoints,seeds,options);
        if(!fit.ok||fit.cancelled||token->load()){task.error=fit.error;task.cancelled=fit.cancelled||token->load();return task;}
        task.planeIndices=fit.planeIndices;
        auto &parameters=task.selected.selectionParameters;
        parameters.insert("surface_tolerance_mm",tolerance);parameters.insert("initial_tolerance_mm",1.0);
        parameters.insert("selection_timing_scope","worker_fit_classification_and_snapshot_excludes_pick_queue_and_display");
        parameters.insert("algorithm_ms",fit.timings.totalMs);
        task.performance={{"algorithm_ms",fit.timings.totalMs},{"local_fit_ms",fit.timings.ransacMs},
            {"classification_ms",fit.timings.classificationMs},{"connectivity_ms",fit.timings.connectivityMs},
            {"refinement_ms",fit.timings.pcaMs}};
        return finishPlaneSnapshotV2(std::move(task),source,seeds,fit.model,fit.rmsError,{},token,budget);
    }catch(const std::bad_alloc &){task.ok=false;task.outOfMemory=true;task.error=QStringLiteral("内存不足，保留原选择");return task;}
}

SelectedPlaneTaskV2 loadSelectedPlaneFrameV2(const QString &path, const QString &calibration,
    const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount, const std::shared_ptr<pcv::ProcessingBudget> &budget,
    pointcloud::DepthPointLayout layout, qsizetype displayPointLimit) {
            SelectedPlaneTask task;task.kind=SelectedPlaneTask::LoadDisplay;
            if(displayPointLimit<1) {task.error=QStringLiteral("展示点数上限必须大于零");return task;}
            try {
                const auto cancelled=[token] {return token->load();};
                pointcloud::HandEyeCalibration handEye;
                QElapsedTimer xmlTimer;xmlTimer.start();
                const bool xmlOk=pointcloud::loadHandEyeCalibration(calibration,&handEye,&task.error);
                task.xmlMs=xmlTimer.elapsed();
                if(!xmlOk)return task;
                pcv::detail::io::PlyReadOptions readOptions;readOptions.isCancelled=cancelled;
                readOptions.asciiWorkerCount=workerCount;readOptions.binaryWorkerCount=workerCount;
                // The batch entry never needs the canonical PLY or a read-back
                // comparison; the source fingerprint and header checks stay on.
                QElapsedTimer phase;phase.start();
                const auto raw=pcv::detail::io::readPlyCached(path,{},readOptions,pcv::detail::io::CloudCachePolicy{false});
                task.plyReadMs=phase.elapsed();
                task.fingerprintMs=raw.fingerprintMs;
                task.cacheReadMs=raw.cacheReadMs;
                task.cacheWriteMs=raw.cacheWriteMs;
                task.plyParseMs=raw.usedCache?-1:raw.parseElapsedMs;
                task.usedCache=raw.usedCache;
                task.headerMs=raw.headerElapsedMs;task.boundaryScanMs=raw.usedCache?-1:raw.boundaryScanElapsedMs;
                task.sourceFormat=raw.sourceFormat==pcv::detail::io::PlyFormat::Ascii?QStringLiteral("ascii"):QStringLiteral("binary");
                if(!raw.ok) {task.error=raw.error;task.cancelled=raw.cancelled;return task;}
                pointcloud::CloudTransformOptions transformOptions;
                transformOptions.layout=layout;
                transformOptions.isCancelled=cancelled;
                transformOptions.collectSamplePoints=false;
                transformOptions.parallelEnabled=raw.points.size()>=100000&&workerCount>1;
                transformOptions.maxParallelTasks=workerCount;
                transformOptions.processingBudget=budget;
                phase.restart();
                auto cloud=pointcloud::transformLineScanToRobotBase(raw.points,handEye,start,end,transformOptions);
                task.transformMs=phase.elapsed();
                task.transformMergeMs=cloud.mergeMs;task.transformWorkers=cloud.actualWorkerCount;
                if(!cloud.ok) {task.error=cloud.error;task.cancelled=cloud.cancelled;return task;}
                task.loaded.sourceFile=path;
                task.loaded.fullPoints=std::move(cloud.points);
                task.loaded.sourceIndices=std::move(cloud.sourceIndices);
                task.loaded.scanRatios=std::move(cloud.scanRatios);
                task.loaded.declaredCount=raw.declaredPointCount;
                task.loaded.startBaseFromFlange=pointcloud::robotPoseToMatrix(start);
                task.loaded.endBaseFromFlange=pointcloud::robotPoseToMatrix(end);
                task.loaded.flangeFromDepth=handEye.flangeFromDepth;
                task.loaded.rejectedInvalid=cloud.rejectedInvalid;
                task.loaded.rejectedRange=cloud.rejectedRange;
                task.loaded.signedTravel=cloud.signedTravel;
                task.loaded.dominantTravelAxis=cloud.dominantTravelAxis;
                task.loaded.inputYMinimum=cloud.inputYMinimum;
                task.loaded.inputYMaximum=cloud.inputYMaximum;
                phase.restart();
                // Reserve both display points and forward/reverse mapping
                // storage before allocating the optional display subset.
                task.displayLease=budget->acquire(size_t(qMin<qsizetype>(task.loaded.fullPoints.size(),displayPointLimit))*64);
                task.displayBudgetMs=phase.elapsed();phase.restart();
                if(!task.displayLease) {task.outOfMemory=true;task.error=QStringLiteral("展示加速预算不足，请重试或跳过");return task;}
                if(!prepareDisplayCloudV2(&task,cancelled,displayPointLimit)) {
                    if(!task.cancelled)task.error=task.error.isEmpty()?QStringLiteral("展示子集生成失败"):task.error;
                    return task;
                }
                task.displaySamplingMs=phase.elapsed();
                task.ok=true;
                return task;
            } catch(const std::bad_alloc &) {task.outOfMemory=true;task.error=QStringLiteral("内存不足，帧未加载");return task;}
}

SelectedPlaneTaskV2 loadSelectedDepthFrameV2(const QString &frameDirectory, const QString &calibration,
    const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount,
    const std::shared_ptr<pcv::ProcessingBudget> &budget, qsizetype displayPointLimit) {
    SelectedPlaneTask task;task.kind=SelectedPlaneTask::LoadDisplay;
    if(displayPointLimit<1) {task.error=QStringLiteral("展示点数上限必须大于零");return task;}
    try {
        const auto cancelled=[token]{return !token||token->load();};
        QElapsedTimer phase;phase.start();
        auto source=pcv::camera::readCapturedFrame(frameDirectory,cancelled,
            pcv::camera::CaptureFrameReadMode::PreviewOnly);
        task.plyReadMs=phase.elapsed();
        if(!source.ok||!source.rawFrame) {
            task.error=source.error.isEmpty()?QStringLiteral("深度帧读取失败"):source.error;
            task.cancelled=cancelled();return task;
        }
        task.rawDepthFrame=source.rawFrame;
        phase.restart();
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(source.rawFrame->data);
        digest.addData(source.rawFrame->intensityData);
        task.sourceDigest=digest.result();task.digestMs=phase.elapsed();
        task.ok=!cancelled();task.cancelled=cancelled();return task;
    } catch(const std::bad_alloc &) {
        task.outOfMemory=true;task.error=QStringLiteral("内存不足，深度帧未读取");return task;
    }
}

SelectedPlaneTaskV2 materializeSelectedDepthFrameV2(
    const std::shared_ptr<const pcv::camera::CameraFrame> &rawFrame, const QString &frameDirectory,
    const QString &calibration, const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount,
    const std::shared_ptr<pcv::ProcessingBudget> &budget, qsizetype displayPointLimit) {
    SelectedPlaneTask task;task.kind=SelectedPlaneTask::LoadDisplay;task.rawDepthFrame=rawFrame;
    if(!rawFrame||displayPointLimit<1) {task.error=QStringLiteral("深度帧或展示点数上限无效");return task;}
    try {
        const auto cancelled=[token]{return !token||token->load();};
        QElapsedTimer phase;phase.start();
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(rawFrame->data);
        digest.addData(rawFrame->intensityData);
        task.sourceDigest=digest.result();task.digestMs=phase.elapsed();
        pointcloud::HandEyeCalibration handEye;
        phase.restart();
        const bool xmlOk=pointcloud::loadHandEyeCalibration(calibration,&handEye,&task.error);
        task.xmlMs=phase.elapsed();if(!xmlOk)return task;
        phase.restart();
        const auto converted=pcv::camera::materializeCameraFrame(*rawFrame);
        task.sdkMaterializeMs=phase.elapsed();
        if(!converted.ok||cancelled()) {
            task.error=converted.error.isEmpty()?QStringLiteral("深度转点云已取消"):converted.error;
            task.cancelled=cancelled();return task;
        }
        pointcloud::CloudTransformOptions options;
        options.layout=pointcloud::DepthPointLayout::FullXyz;
        options.useFullXyzRasterRowProgress=true;
        options.rasterWidth=rawFrame->width;options.rasterHeight=rawFrame->height;
        options.rasterSourceIndices=converted.sourceIndices;
        options.isCancelled=cancelled;options.collectSamplePoints=false;
        options.parallelEnabled=converted.points.size()>=100000&&workerCount>1;
        options.maxParallelTasks=workerCount;options.processingBudget=budget;
        phase.restart();
        auto cloud=pointcloud::transformLineScanToRobotBase(converted.points,handEye,start,end,options);
        task.transformMs=phase.elapsed();task.transformMergeMs=cloud.mergeMs;
        task.transformWorkers=cloud.actualWorkerCount;
        if(!cloud.ok||cancelled()) {task.error=cloud.error;task.cancelled=cloud.cancelled||cancelled();return task;}
        task.loaded.sourceFile=frameDirectory;
        task.loaded.fullPoints=std::move(cloud.points);
        task.loaded.sourceIndices=std::move(cloud.sourceIndices);
        task.loaded.scanRatios=std::move(cloud.scanRatios);
        task.loaded.declaredCount=rawFrame->dataPointCount();
        task.loaded.startBaseFromFlange=pointcloud::robotPoseToMatrix(start);
        task.loaded.endBaseFromFlange=pointcloud::robotPoseToMatrix(end);
        task.loaded.flangeFromDepth=handEye.flangeFromDepth;
        task.loaded.rejectedInvalid=cloud.rejectedInvalid;
        task.loaded.rejectedRange=cloud.rejectedRange;
        task.loaded.signedTravel=cloud.signedTravel;
        task.loaded.dominantTravelAxis=cloud.dominantTravelAxis;
        task.loaded.inputYMinimum=cloud.inputYMinimum;task.loaded.inputYMaximum=cloud.inputYMaximum;
        phase.restart();
        task.displayLease=budget->acquire(size_t(qMin<qsizetype>(task.loaded.fullPoints.size(),displayPointLimit))*64);
        task.displayBudgetMs=phase.elapsed();phase.restart();
        if(!task.displayLease) {task.outOfMemory=true;task.error=QStringLiteral("展示加速预算不足，请重试或跳过");return task;}
        if(!prepareDisplayCloudV2(&task,cancelled,displayPointLimit)) {
            if(!task.cancelled)task.error=task.error.isEmpty()?QStringLiteral("展示子集生成失败"):task.error;
            return task;
        }
        task.displaySamplingMs=phase.elapsed();task.ok=true;return task;
    } catch(const std::bad_alloc &) {
        task.outOfMemory=true;task.error=QStringLiteral("内存不足，深度帧未完成物化");return task;
    }
}

SelectedPlaneTaskV2 prepareSelectedPlaneIndexV2(const QVector<pointcloud::Point3D> &points,
    const CancellationTokenV2 &token, int workerCount, const std::shared_ptr<pcv::ProcessingBudget> &budget, int normalBatchSize, bool cacheNormalNeighbors) {
    const bool parallel=points.size()>=100000&&workerCount>1;
            SelectedPlaneTask task;task.kind=SelectedPlaneTask::PrepareIndex;
            try {
                pcv::planefitting::IndexPreparationOptions options;
                options.parallelEnabled=parallel;
                options.maxParallelTasks=workerCount;
                options.cachePointBuckets=true;options.normalBatchSize=normalBatchSize;options.cacheNormalNeighbors=cacheNormalNeighbors;
                options.processingBudget=budget;
                QElapsedTimer phase;phase.start();
                const auto prepared=pcv::planefitting::preparePlaneSelectionIndex(points,[token]{return token->load();},options);
                task.preparationMs=phase.elapsed();task.indexBudgetMs=prepared.budgetAcquireMs;
                task.performance={{"bucket_count",double(prepared.bucketCount)},{"normal_batch_size",prepared.normalBatchSize},
                    {"normal_batches",prepared.normalBatchCount},{"normal_workers",prepared.normalWorkerCount},
                    {"normal_fallback",prepared.normalFallback},{"normal_compute_sum_ms",prepared.normalComputeSumMs},
                    {"normal_join_ms",prepared.normalJoinMs},{"normal_commit_ms",prepared.normalCommitMs},
                    {"neighbor_prepare_ms",prepared.neighborPreparationMs},{"neighbor_cache_used",prepared.neighborCacheUsed},
                    {"neighbor_cache_bytes",double(prepared.neighborCacheBytes)},{"neighbor_fallback",prepared.neighborFallback}};
                if(prepared.cancelled||token->load()) {task.cancelled=true;return task;}
                task.outOfMemory=prepared.outOfMemory;
                task.gridMs=prepared.gridMs;
                task.normalsMs=prepared.normalsMs;
                task.optimizationBytes=prepared.optimizationBytes;
                task.index=prepared.index;
                task.error=prepared.error;
                task.ok=prepared.index!=nullptr;
                return task;
            } catch(const std::bad_alloc &) {task.outOfMemory=true;task.error=QStringLiteral("内存不足，完整选面索引未建立");return task;}
}
bool prepareHeightRangeV1(SelectedPlaneTaskV2 *task, const CancellationTokenV2 &token) {
    if(!task)return false;
    QElapsedTimer timer;timer.start();
    float lower=std::numeric_limits<float>::infinity(),upper=-lower;
    const auto &points=std::as_const(task->loaded.fullPoints);
    for(qsizetype i=0;i<points.size();++i) {
        if(i%kCancellationCheckStride==0&&token&&token->load()) {
            task->cancelled=true;task->heightRangeMs=timer.elapsed();return false;
        }
        const float z=points.at(i).z;
        if(std::isfinite(z)){lower=std::min(lower,z);upper=std::max(upper,z);}
    }
    if(!std::isfinite(lower)){lower=0;upper=1;}
    if(upper<=lower)upper=lower+1.0f;
    if(upper<=lower)upper=std::nextafter(lower,std::numeric_limits<float>::infinity());
    task->heightRange={lower,upper};task->hasHeightRange=true;
    task->heightRangeMs=timer.elapsed();return true;
}
bool prepareDisplayCloudV2(SelectedPlaneTask *task, const std::function<bool()> &cancelled,
    qsizetype displayPointLimit) {
    if(!task)return false;
    task->displayPoints.clear();
    task->displayToFullIndices.clear();
    task->cancelled=false;
    const QVector<pointcloud::Point3D> &full=task->loaded.fullPoints;
    const qsizetype count=full.size();
    if(displayPointLimit<1) {
        task->error=QStringLiteral("展示点数上限必须大于零");
        return false;
    }
    if(count<=0) {
        task->error=QStringLiteral("点云为空，未生成展示子集");
        return false;
    }
    if(count>qsizetype(std::numeric_limits<int>::max())) {
        task->error=QStringLiteral("完整点数超过现有选面/拾取索引能力（%1 > INT_MAX）").arg(count);
        return false;
    }
    const qsizetype displayCount=qMin<qsizetype>(count,displayPointLimit);
    QVector<pointcloud::Point3D> points;
    QVector<int> mapping;
    points.reserve(displayCount);
    mapping.reserve(displayCount);
    for(qsizetype index=0;index<displayCount;++index) {
        if(cancelled&&index%kCancellationCheckStride==0&&cancelled()) {
            task->cancelled=true;
            return false;
        }
        // Deterministic uniform sampling over the original order; the first and
        // last point are always covered and the mapping is strictly increasing.
        const qint64 fullIndex=(displayCount==1)?0
            :qint64((qint64(index)*qint64(count-1))/qint64(displayCount-1));
        mapping.push_back(int(fullIndex));
        points.push_back(full.at(qsizetype(fullIndex)));
    }
    if(cancelled&&cancelled()) {
        task->cancelled=true;
        return false;
    }
    task->displayPoints=std::move(points);
    task->displayToFullIndices=std::move(mapping);
    return true;
}
QByteArray loadedCloudDigestV2(const pointcloud::RobotBaseFrame &cloud, const CancellationTokenV2 &token) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(cloud.sourceFile.toUtf8());
    hash.addData(QByteArray::number(cloud.declaredCount));
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(cloud.startBaseFromFlange.constData()),16*qsizetype(sizeof(float))));
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(cloud.endBaseFromFlange.constData()),16*qsizetype(sizeof(float))));
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(cloud.flangeFromDepth.constData()),16*qsizetype(sizeof(float))));
    hash.addData(QByteArray::number(cloud.signedTravel,'g',9));
    auto append = [&](const auto &values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        for (qsizetype i=0; i<values.size(); i+=4096) {
            if (token->load()) return false;
            const qsizetype n=qMin<qsizetype>(4096,values.size()-i);
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(values.constData()+i),n*qsizetype(sizeof(Value))));
        }
        return true;
    };
    if (!append(cloud.fullPoints)||!append(cloud.sourceIndices)||!append(cloud.scanRatios)) return {};
    return hash.result();
}
} // namespace pcv::ui
