#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/stitching/multiframe_registration.h>
#include <pcv/stitching/seam_fusion.h>

#include <QString>
#include <QVector>

#include <functional>

namespace pcv::interface {

struct StitchingFrameInput {
    QString plyPath;
    pointcloud::RobotPose startPose;
    pointcloud::RobotPose endPose;
};

struct StitchingInputGroup {
    QString calibrationPath;
    QVector<StitchingFrameInput> frames;
};

struct StitchingRegistrationGroup {
    pointcloud::IcpOptions options;
    int sampleStride = 8;
    bool parallelEnabled = true;
    int maxParallelTasks = 0;
    bool collectDiagnosticFrames = true;
    bool structuralCoverageOfMatched = true;
    float minimumStructuralMatchedRatio = 0.10f;
    float visibilityWarningRatio = 0.25f;
    bool tangentCoarseTrimmedScore = true;
    int correspondenceParallelTasks = 0;
    // 2.5D height-map registration (controlled iteration) is carried by
    // StitchingRegistrationGroup::options, i.e. the whole IcpOptions block that
    // this group already forwards. It is deliberately NOT duplicated as a flat
    // field here: two sources for the same switch would let a defaulted flat
    // copy silently disable the feature. Default off.
};

struct StitchingSeamGroup {
    bool enabled = false;
    float halfWidthMm = 8.0f;
};

struct StitchingArtifactGroup {
    QString outputDirectory;
    bool allowOverwrite = false;
    double previewRatio = 0.1;
    bool writeSourceClouds = false;
    bool writeDiagnosticFrames = false;
    bool writePreview = true;
};

struct StitchingArtifactManifest {
    QString formalPly;
    QString previewPly;
    QString mappingFile;
    QString reportJson;
    QStringList sourcePlyFiles;
    QStringList diagnosticPreIcpFiles;
    QStringList diagnosticPostIcpFiles;
};

struct StitchingRequest {
    StitchingInputGroup input;
    StitchingRegistrationGroup registration;
    StitchingSeamGroup seam;
    StitchingArtifactGroup artifacts;
    std::function<bool()> isCancelled;
    std::function<void(float, const QString &)> progress;
};

struct StitchingResult;
StitchingResult stitch(const StitchingRequest &request);

struct [[deprecated("Use StitchingRequest grouped fields")]] StitchingOptions {
    QString calibrationPath;
    QString outputDirectory;
    bool allowOverwrite = true;
    QVector<StitchingFrameInput> frames;
    int sampleStride = 8;
    bool seamEnabled = false;
    float seamHalfWidthMm = 8.0f;
    // Registration tuning that the module-30 orchestration forwards to the
    // module-40 registration options. Defaults follow IcpOptions; they are
    // exposed so a caller (and the regression harness) can A/B a configuration
    // without touching the shared defaults.
    bool structuralCoverageOfMatched = true;
    float minimumStructuralMatchedRatio = 0.10f;
    float visibilityWarningRatio = 0.25f;
    bool tangentCoarseTrimmedScore = true;
    int correspondenceParallelTasks = 0;
    // 2.5D height-map registration, forwarded to IcpOptions.
    bool heightMapRegistrationEnabled = false;
    float heightMapGridMinimumMm = 0.5f;
    float heightMapGridMaximumMm = 2.0f;
    float heightMapMinimumValidRatio = 0.25f;
    int heightMapMinimumValidPoints = 100;
    float heightMapMinimumRelativeImprovement = 0.01f;
    float heightMapMaxTranslationMm = 10.0f;
    float heightMapMaxYawDegrees = 1.0f;
    bool heightMapRestrictedDof = true;
    bool heightMapSearchEnabled = true;
    // Compatible default: the registration result keeps the 2 mm pre/post ICP
    // diagnostic frames. Standard output callers disable this to skip frames.
    bool collectDiagnosticFrames = true;
    bool parallelEnabled = true;
    int maxParallelTasks = 0;
    pointcloud::IcpOptions registrationOptions;
    bool useRegistrationOptions = false;
    double previewRatio = 0.1;
    bool writeSourceClouds = false;
    bool writeDiagnosticFrames = false;
    std::function<bool()> isCancelled;
    std::function<void(float, const QString &)> progress;
    bool writePreview = true;
};

struct StitchingResult {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    QString outputPly;
    StitchingArtifactManifest manifest;
    QVector<pointcloud::Point3D> points;
    pointcloud::MultiFrameRegistrationResult merge;
    QVector<pointcloud::IcpDiagnostics> icpDiagnostics;
    QVector<pointcloud::SeamFusionDiagnostic> seamDiagnostics;
    pointcloud::SeamFusionResult seamFusion;
    QString diagnostics;
    // Stage wall-clock timings (ms). readMs/transformMs/registrationMs are the
    // durations of the conversion and registration calls; seamFusionMs comes
    // from the shared seam fusion result; outputMs covers the formal PLY write.
    // registration detail is reported in diagnostics via the module 40 result.
    qint64 readMs = 0;
    qint64 transformMs = 0;
    qint64 registrationMs = 0;
    qint64 seamFusionMs = 0;
    qint64 outputMs = 0;
    qint64 totalMs = 0;
    bool parallelEnabled = false;
    int requestedParallelTasks = 0;
    int actualParallelTasks = 1;
    qint64 readWorkMs = 0;
    qint64 transformWorkMs = 0;
    qint64 framePreparationWallMs = 0;
};

StitchingResult stitchRawLineProfiles(const StitchingOptions &options);

} // namespace pcv::interface
