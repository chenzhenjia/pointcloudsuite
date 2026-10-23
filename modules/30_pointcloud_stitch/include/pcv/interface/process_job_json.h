#pragma once

#include <pcv/interface/stitching_interface.h>
#include <QJsonObject>

namespace pcv::interface {

struct PcvExecutionFrame {
    quint64 id = 0;
    pointcloud::RobotPose startPose, endPose;
};
struct PcvExecutionResult {
    QString schemaVersion, jobId, captureSession;
    QVector<PcvExecutionFrame> frames;
};
struct PcvPlyProcessParam {
    QString schemaVersion, jobId, executionResult, calibration, mode, outputDir;
};
// Resolved paths are absolute in memory, relative to their own JSON on disk.
struct PcvProcessJob {
    QString parameterFile, taskRoot;
    PcvPlyProcessParam parameters;
    PcvExecutionResult execution;
    StitchingRequest request;
};
struct PcvProcessTemplate {
    QString jobId, name, templateInfo, pointCloud, roiCloud, planeMask;
    pointcloud::RobotPose wobjPose;
};
struct PcvProcessSummary {
    QString jobId, status, errorCode, message;
};

bool readProcessJob(const QString &parameterFile, PcvProcessJob *job, QString *error);
bool readProcessTemplate(const QString &file, PcvProcessTemplate *value, QString *error);
bool readProcessSummary(const QString &file, PcvProcessSummary *value, QString *error);
// Never overwrites terminal results. Success must be published with the index.
bool writeProcessSummary(const PcvProcessJob &job, const QString &status,
                         const QString &code, const QString &message, QString *error);
bool commitProcessTemplate(const PcvProcessJob &job, const PcvProcessTemplate &value,
                           const std::function<bool()> &cancelled, QString *error);

} // namespace pcv::interface
