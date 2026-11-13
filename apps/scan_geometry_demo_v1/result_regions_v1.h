#pragma once
#include <pcv/planefitting/plane_mask_v1.h>
#include <pcv/stitching/selected_plane_stitch.h>
#include <QSet>

namespace pcv::demo {
// 拼接修正后的robot_base投影基准；间距/原点单位mm，valid=false时从参考面计算。
struct ResultRegionBasisV1 {
    pcv::planefitting::PlaneModel model;
    QVector3D axisU;
    double spacingU = 0, spacingV = 0;
    double gridOriginU = 0, gridOriginV = 0;
    bool fixedGridOrigin = false; // 解析后固定网格，确保修剪/重试不漂移。
    bool valid = false;
};
// One entry per retained stitch frame, including frames with no annotation.
struct ResultInterferenceFrameV1 {
    pcv::planefitting::PlaneMaskMeshV1 mask;
    QString error;
};
struct ResultRegionsRequestV1 {
    QVector<pointcloud::Point3D> points; // 完整拼接结果robot_base/mm，不是展示子集。
    QVector<QMatrix4x4> corrections; // 与frames同序：仅保留参与拼接的帧。
    QVector<ResultInterferenceFrameV1> frames;
    pcv::stitching::SelectedPlaneFrame reference;
    int referenceKeptIndex = -1; // 保留帧列表下标，与原输入referenceFrame不同。
    QSet<int> removed; // points原始下标；越界失败。
    ResultRegionBasisV1 basis;
    std::shared_ptr<pcv::ProcessingBudget> budget; // 共享内部优化预算，不包含全部业务数据。
    std::function<bool()> isCancelled; // 可空；worker轮询，回调必须线程安全。
};
struct ResultRegionsResultV1 {
    pcv::planefitting::PlaneMaskMeshV1 workpiece, interference;
    ResultRegionBasisV1 basis;
    bool workpieceOk = false, interferenceOk = false, cancelled = false; // 两个区域独立判定；空干涉可成功。
    QString error;
    QJsonObject performance; // 诊断字段，不进入业务schema。
};
// 同步纯计算，不落盘；部分失败仍可能返回有效另一侧，须分别检查ok。
ResultRegionsResultV1 buildResultRegionsV1(const ResultRegionsRequestV1 &request);
} // namespace pcv::demo
