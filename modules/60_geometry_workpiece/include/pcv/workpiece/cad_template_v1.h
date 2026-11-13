#pragma once
#include <pcv/workpiece/workpiece_template_v1.h>
#include <pcv/core/cad_mesh_v1.h>
#include <memory>

namespace pcv::workpiece {
inline constexpr const char *kCadTemplateSchema = "pcv-workpiece-cad-template-v1";
inline constexpr const char *kCadTemplateKind = "workpiece_cad_template_3d";
inline constexpr const char *kCadTemplatePly = "workpiece_template_cad_model.ply";
inline constexpr const char *kCadTemplateJson = "workpiece_cad_template_info.json";
inline constexpr qint64 kCadMaximumPoints = 10000000;
enum class CadStage { Reading, Validating, Meshing, Sampling };
using CadProgress = std::function<void(CadStage, qint64, qint64)>;
struct CadFaceInfo {
    int id = 0; QString surfaceType; double areaMm2 = 0;
    bool planar = false;
    QVector3D planeOrigin, planeNormal;
};
struct CadFacePointRangeV1 { int faceId=0; qsizetype first=0,count=0; };
class CadModel;
struct ReadStepModelRequest {
    QString file;
    std::function<bool()> isCancelled;
    CadProgress progress;
};
struct StepModelResult {
    bool success = false, cancelled = false;
    QString errorCode, message;
    std::shared_ptr<const CadModel> model;
    QVector<CadFaceInfo> faces;
    pcv::CadMeshV1 mesh;
    QJsonObject source;
    QVector3D minimum, maximum;
};
struct GenerateCadTemplateRequest {
    std::shared_ptr<const CadModel> model;
    QVector<int> faceIds;
    double spacingMm = 0.5;
    std::function<bool()> isCancelled;
    CadProgress progress;
};
struct CadCloudResult {
    bool success = false, cancelled = false;
    QString errorCode, message;
    QVector<pointcloud::Point3D> points;
    QJsonObject source;
    QVector<CadFacePointRangeV1> faceRanges;
};
StepModelResult readStepModel(const ReadStepModelRequest &request);
CadCloudResult generateCadTemplateCloud(const GenerateCadTemplateRequest &request);
WorkpieceTemplateResult saveCadTemplate(const SaveWorkpieceTemplateRequest &request);
WorkpieceTemplateResult loadCadTemplate(const LoadWorkpieceTemplateRequest &request);
// Dispatches only the two explicitly supported template schemas.
WorkpieceTemplateResult loadAnyWorkpieceTemplate(const LoadWorkpieceTemplateRequest &request);
} // namespace pcv::workpiece
