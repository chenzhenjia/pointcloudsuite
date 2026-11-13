#pragma once
#include <pcv/planefitting/plane_mask_v1.h>
#include <pcv/stitching/selected_plane_stitch.h>
#include <QSet>

namespace pcv::demo {
struct ResultRegionBasisV1 {
    pcv::planefitting::PlaneModel model;
    QVector3D axisU;
    double spacingU = 0, spacingV = 0;
    double gridOriginU = 0, gridOriginV = 0;
    bool fixedGridOrigin = false;
    bool valid = false;
};
// One entry per retained stitch frame, including frames with no annotation.
struct ResultInterferenceFrameV1 {
    pcv::planefitting::PlaneMaskMeshV1 mask;
    QString error;
};
struct ResultRegionsRequestV1 {
    QVector<pointcloud::Point3D> points;
    QVector<QMatrix4x4> corrections;
    QVector<ResultInterferenceFrameV1> frames;
    pcv::stitching::SelectedPlaneFrame reference;
    int referenceKeptIndex = -1;
    QSet<int> removed;
    ResultRegionBasisV1 basis;
    std::shared_ptr<pcv::ProcessingBudget> budget;
    std::function<bool()> isCancelled;
};
struct ResultRegionsResultV1 {
    pcv::planefitting::PlaneMaskMeshV1 workpiece, interference;
    ResultRegionBasisV1 basis;
    bool workpieceOk = false, interferenceOk = false, cancelled = false;
    QString error;
    QJsonObject performance;
};
ResultRegionsResultV1 buildResultRegionsV1(const ResultRegionsRequestV1 &request);
} // namespace pcv::demo
