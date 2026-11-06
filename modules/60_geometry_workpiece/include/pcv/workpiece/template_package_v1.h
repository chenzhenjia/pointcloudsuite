#pragma once
#include <pcv/workpiece/template_features_v1.h>
#include <QSet>
namespace pcv::workpiece {
struct TemplatePackageV1 {
    bool ok=false,cancelled=false;
    QString error,coordinateFrame,identity,infoFile;
    QVector<pointcloud::Point3D> points,interferencePoints;
    QVector<TemplatePlaneV1> planes;
    QJsonObject source;
    QJsonObject timings;
};
TemplatePackageV1 generateStepTemplatePackageV1(const StepModelResult &model,const QSet<int> &workpiece,
    const QSet<int> &interference,double spacing,const TemplateFeatureOptionsV1 &options={});
TemplatePackageV1 loadTemplatePackageV1(QString path,const TemplateFeatureOptionsV1 &options={});
WorkpieceTemplateResult saveTemplatePackageV1(const TemplatePackageV1 &package,QString name,QString outputRoot,
    const std::function<bool()> &isCancelled={});
QString templatePointIdentityV1(const QVector<pointcloud::Point3D> &points,const std::function<bool()> &isCancelled={});
}
