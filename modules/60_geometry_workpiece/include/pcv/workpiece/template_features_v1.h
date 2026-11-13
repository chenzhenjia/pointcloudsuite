#pragma once
#include <pcv/workpiece/cad_template_v1.h>
#include <pcv/planefitting/plane_mask_v1.h>
#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QSet>
namespace pcv::workpiece {
struct TemplatePlaneV1 {
    int faceId=0;
    bool interference=false;
    qsizetype first=0,count=0; // CAD package range; measured planes use indices.
    QVector<int> indices;
    pcv::planefitting::PlaneModel model;
    QVector3D center,normal,axis;
    pcv::planefitting::PlaneMaskMeshV1 region;
};
struct TemplateFeaturesV1 {
    bool ok=false,cancelled=false;
    QString error;
    QVector<TemplatePlaneV1> planes;
    QJsonObject timings;
};
struct TemplateFeatureOptionsV1 {
    float toleranceMm=.4f;
    int maximumSeeds=32;
    std::function<bool()> isCancelled;
    std::shared_ptr<pcv::ProcessingBudget> budget;
};
TemplateFeaturesV1 prepareTemplateFeaturesV1(const QVector<pointcloud::Point3D> &points,const TemplateFeatureOptionsV1 &options={});
TemplateFeaturesV1 cadTemplateFeaturesV1(const StepModelResult &model,const CadCloudResult &cloud,const QSet<int> &interference,
    const std::function<bool()> &isCancelled={});
QJsonObject serializeTemplateFeaturesV1(const QVector<TemplatePlaneV1> &planes);
TemplateFeaturesV1 parseTemplateFeaturesV1(const QJsonObject &object,qsizetype workpieceCount,qsizetype interferenceCount);
}
