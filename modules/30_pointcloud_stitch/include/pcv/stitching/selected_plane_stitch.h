#pragma once

#include <pcv/stitching/multiframe_registration.h>
#include <QJsonObject>

namespace pcv::stitching {

enum class PlaneSelectionMethod { ThreePoint, SingleClick };

// Points are already in robot_base; this entry never applies hand-eye again.
struct SelectedPlaneFrame {
    pointcloud::RobotBaseFrame cloud;
    QVector3D normal;
    QVector3D center;
    float rmsMm = 0.0f;
    QVector<qsizetype> seedSourceIndices;
    qsizetype originalValidPointCount = 0;
    qint64 selectionMs = 0;
    PlaneSelectionMethod selectionMethod = PlaneSelectionMethod::ThreePoint;
    QJsonObject selectionParameters;
};

struct SelectedPlaneOptions {
    bool fineEnabled = true; // Also permits an identical-input pose-only comparison.
    bool parallelEnabled = true;
    int maxParallelTasks = 0;
    float maximumTranslationMm = 10.0f;
    float maximumAngleDegrees = 1.0f;
};

struct SelectedPlanePairDiagnostic {
    bool fineApplied = false;
    bool poseFallback = false;
    bool chainAnchorReset = false;
    bool degenerate = true;
    bool converged = false;
    int completedLevels = 0;
    int observableDof = 0;
    int correspondences = 0;
    float rmseMm = 0.0f;
    float matchedRatio = 0.0f;
    QVector<int> iterations;
    QString reason;
    QMatrix4x4 pairCorrection;
    QMatrix4x4 cumulativeCorrection;
};

struct SelectedPlaneStitchRequest {
    QVector<SelectedPlaneFrame> frames;
    SelectedPlaneOptions options;
    QString calibrationPath;
    QString outputDirectory;
    bool allowOverwrite = false;
    std::function<bool()> isCancelled;
    pointcloud::ProgressCallback progress;
};

struct SelectedPlaneStitchResult {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    QString outputPly;
    QString mappingFile;
    QString reportFile;
    QVector<pointcloud::Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    QVector<SelectedPlanePairDiagnostic> pairs;
    QVector<QMatrix4x4> corrections;
    QJsonObject report;
};

SelectedPlaneStitchResult stitchSelectedPlanes(const SelectedPlaneStitchRequest &request);

} // namespace pcv::stitching
