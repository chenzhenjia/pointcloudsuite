#pragma once

#include <pcv/core/point_types.h>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <functional>

namespace pcv::workpiece {

inline constexpr const char *kWorkpieceTemplateSchema = "pcv-workpiece-template-v1";
inline constexpr const char *kWorkpieceTemplateKind = "workpiece_template_3d";
inline constexpr const char *kWorkpieceTemplatePly = "workpiece_template_robot_base.ply";
inline constexpr const char *kWorkpieceTemplateJson = "workpiece_template_info.json";

enum class WorkpieceTemplateStage {
    ValidatingPoints, WritingCloud, WritingMetadata, Committing,
    ReadingMetadata, ReadingCloud
};
// Called on the worker (and possibly reader workers), never on the GUI thread.
using WorkpieceTemplateProgress =
    std::function<void(WorkpieceTemplateStage, qsizetype, qsizetype)>;

struct WorkpieceTemplateInfo {
    QString name;
    QString versionId;
    QString createdAt;
    QString coordinateFrame = QStringLiteral("robot_base");
    QString units = QStringLiteral("mm");
    qsizetype pointCount = 0;
    QVector3D minimum;
    QVector3D maximum;
    QString pointCloudFile = QString::fromLatin1(kWorkpieceTemplatePly);
    // Informational file-level provenance; these files need not remain available.
    QJsonObject source;
};

struct SaveWorkpieceTemplateRequest {
    QString name;
    QString outputRoot;
    QString coordinateFrame; // Caller must establish robot_base; never inferred.
    QVector<pointcloud::Point3D> points;
    QJsonObject source;
    std::function<bool()> isCancelled;
    WorkpieceTemplateProgress progress;
};

struct LoadWorkpieceTemplateRequest {
    QString infoFile;
    std::function<bool()> isCancelled;
    WorkpieceTemplateProgress progress;
};

struct WorkpieceTemplateResult {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    QString outputDirectory;
    QString infoFile;
    QString pointCloudFile;
    WorkpieceTemplateInfo info;
    QVector<pointcloud::Point3D> points; // Only published by a successful load.
};

QString defaultWorkpieceTemplateRoot();
WorkpieceTemplateResult saveWorkpieceTemplate(const SaveWorkpieceTemplateRequest &request);
WorkpieceTemplateResult loadWorkpieceTemplate(const LoadWorkpieceTemplateRequest &request);

} // namespace pcv::workpiece
