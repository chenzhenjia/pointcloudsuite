#pragma once
#include <pcv/workpiece/template_features_v1.h>
#include <QMatrix4x4>
namespace pcv::matching {
struct TemplateMatchParametersV1 {
    double correspondenceMm=2,minimumCoverage=.3;
    int minimumMatches=100,maximumHypotheses=4096,refineCount=16,maximumCandidates=5;
};
struct TemplateMatchCandidateV1 {
    QMatrix4x4 targetFromTemplate;
    double rmsMm=0,coverage=0,score=0,observability=0;
    qsizetype matchedPoints=0;
    bool acceptable=false;
};
struct TemplateMatchRequestV1 {
    QVector<pointcloud::Point3D> templatePoints,targetPoints;
    QVector<pcv::workpiece::TemplatePlaneV1> templatePlanes,targetPlanes;
    TemplateMatchParametersV1 parameters;
    std::function<bool()> isCancelled;
    std::function<void(const QString &,int,int)> progress;
    std::shared_ptr<pcv::ProcessingBudget> budget;
};
struct TemplateMatchResultV1 {
    bool success=false,cancelled=false,searchLimited=false;
    QString error;
    QVector<TemplateMatchCandidateV1> candidates;
    QJsonObject timings;
    int evaluatedHypotheses=0;
};
TemplateMatchResultV1 matchTemplateV1(const TemplateMatchRequestV1 &request);
QJsonObject matchParametersJsonV1(const TemplateMatchParametersV1 &parameters);
QJsonObject matchCandidateJsonV1(const TemplateMatchCandidateV1 &candidate);
}
