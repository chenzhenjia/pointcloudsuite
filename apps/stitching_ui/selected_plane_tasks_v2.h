#pragma once
#include <pcv/stitching/selected_plane_stitch.h>
#include <pcv/planefitting/reference_plane_match_v2.h>
#include <pcv/planefitting/high_side_plane_candidates_v1.h>
#include <pcv/planefitting/plane_mask_v1.h>
#include <pcv/workpiece/workpiece_template_v1.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/camera/frame_data.h>
#include <QElapsedTimer>
#include <QByteArray>
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>

namespace pcv::ui {
inline constexpr qsizetype kDefaultDisplayPointLimitV2 = 300000;
struct SelectedPlaneInputV2 {
    QString path;
    bool depthFrame = false;
    pointcloud::RobotPose start, end;
    QString calibration, scanJson, scanId;
    pointcloud::DepthPointLayout layout = pointcloud::DepthPointLayout::LineProfileXz;
    QString warning;
};
bool parseSelectedPlaneScanJsonV2(const QString &path, SelectedPlaneInputV2 *input, QString *error);

struct SelectedPlaneTaskV2 {
    enum Kind { LoadDisplay, PrepareIndex, Fit, Match, HighSideSearch, Stitch, SaveTemplate, SaveGeometry, BuildResultRegions } kind = LoadDisplay;
    pointcloud::RobotBaseFrame loaded;
    std::shared_ptr<const pcv::camera::CameraFrame> rawDepthFrame;
    QVector<pointcloud::Point3D> displayPoints;
    QVector<int> displayToFullIndices;
    pcv::stitching::SelectedPlaneFrame selected;
    pcv::stitching::SelectedPlaneStitchResult stitched;
    pcv::workpiece::WorkpieceTemplateResult savedTemplate;
    pcv::workpiece::WorkpieceTemplateInfo templateSummary;
    QVector<int> planeIndices;
    pcv::planefitting::PlaneMaskMeshV1 planeMask;
    QString planeMaskError;
    pcv::planefitting::PlaneSelectionIndexPtr index;
    double preparationMs = 0, gridMs = 0, normalsMs = 0, indexBudgetMs = 0;
    qint64 plyReadMs = -1, fingerprintMs = -1, cacheReadMs = -1, cacheWriteMs = -1;
    qint64 transformMs = -1, displaySamplingMs = -1, displayBudgetMs = -1;
    qint64 plyParseMs = -1;
    qint64 xmlMs = -1, sdkMaterializeMs = -1, digestMs = -1, heightRangeMs = -1;
    qint64 headerMs = -1, boundaryScanMs = -1;
    QString sourceFormat;
    QPair<float,float> heightRange{0.0f,1.0f};
    bool hasHeightRange = false;
    double transformMergeMs = 0, snapshotMs = 0, projectionMs = 0, normalConnectivityMs = 0, gapMs = 0;
    // Session diagnostics only; never part of formal output schemas.
    QJsonObject performance;
    int transformWorkers = 1;
    qint64 workerMs = 0;
    QElapsedTimer startedAt, completedAt;
    size_t optimizationBytes = 0;
    QByteArray sourceDigest;
    bool sourceVerified = false;
    double verifiedMs = -1;
    pcv::planefitting::ReferencePlaneMatchStatusV2 matchStatus = pcv::planefitting::ReferencePlaneMatchStatusV2::NotFound;
    pcv::planefitting::HighSidePlaneSearchStatusV1 highSideSearchStatus = pcv::planefitting::HighSidePlaneSearchStatusV1::InvalidInput;
    QVector<pcv::planefitting::HighSidePlaneCandidateV1> highSideCandidates;
    QString error;
    QString savedGeometryPath;
    bool geometrySaved = false;
    std::shared_ptr<pcv::ProcessingBudget::Lease> displayLease;
    bool ok = false, cancelled = false, outOfMemory = false, usedCache = false;
};
using CancellationTokenV2 = std::shared_ptr<std::atomic_bool>;
bool prepareHeightRangeV1(SelectedPlaneTaskV2 *task, const CancellationTokenV2 &token);
bool prepareDisplayCloudV2(SelectedPlaneTaskV2 *task, const std::function<bool()> &cancelled,
    qsizetype displayPointLimit = kDefaultDisplayPointLimitV2);
SelectedPlaneTaskV2 loadSelectedPlaneFrameV2(const QString &path, const QString &calibration,
    const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount, const std::shared_ptr<pcv::ProcessingBudget> &budget,
    pointcloud::DepthPointLayout layout = pointcloud::DepthPointLayout::LineProfileXz,
    qsizetype displayPointLimit = kDefaultDisplayPointLimitV2);
SelectedPlaneTaskV2 loadSelectedDepthFrameV2(const QString &frameDirectory, const QString &calibration,
    const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount, const std::shared_ptr<pcv::ProcessingBudget> &budget,
    qsizetype displayPointLimit = kDefaultDisplayPointLimitV2);
SelectedPlaneTaskV2 materializeSelectedDepthFrameV2(
    const std::shared_ptr<const pcv::camera::CameraFrame> &rawFrame, const QString &frameDirectory,
    const QString &calibration, const pointcloud::RobotPose &start, const pointcloud::RobotPose &end,
    const CancellationTokenV2 &token, int workerCount,
    const std::shared_ptr<pcv::ProcessingBudget> &budget,
    qsizetype displayPointLimit = kDefaultDisplayPointLimitV2);
SelectedPlaneTaskV2 prepareSelectedPlaneIndexV2(const QVector<pointcloud::Point3D> &points,
    const CancellationTokenV2 &token, int workerCount, const std::shared_ptr<pcv::ProcessingBudget> &budget,
    int normalBatchSize = 4096, bool cacheNormalNeighbors = false);
SelectedPlaneTaskV2 fitFullPlaneV2(const pointcloud::RobotBaseFrame &source, const QVector<int> &seeds,
    float tolerance, float maximumGap, bool singleClick,
    const pcv::planefitting::PlaneSelectionIndexPtr &spatialIndex,
    const CancellationTokenV2 &token, int workers, const std::shared_ptr<pcv::ProcessingBudget> &budget);
// Consumes an already computed full-resolution selection; performs no fitting.
SelectedPlaneTaskV2 snapshotFromSingleClickV2(const pointcloud::RobotBaseFrame &source,
    const QVector<int> &seeds, float tolerance, const pcv::planefitting::SingleClickResult &fit,
    const CancellationTokenV2 &token, const std::shared_ptr<pcv::ProcessingBudget> &budget);
QByteArray loadedCloudDigestV2(const pointcloud::RobotBaseFrame &cloud, const CancellationTokenV2 &token);
} // namespace pcv::ui
