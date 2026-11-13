#pragma once

#include <selected_plane_tasks.h>
#include <pcv/io/artifact_transaction.h>
#include <QJsonObject>

namespace pcv::demo {

// Application-private SR-2026 adapter. Empty runtimeRoot preserves legacy output.
struct ScanGeometryContractV1 {
    // runtimeRoot为源码树外绝对路径；ID由调用方给定，不在此创建任务。
    QString runtimeRoot, jobId, runId, stepId, workpieceId, controllerId;
    int wobjId = 1; // 当前契约固定1。
    QString scanPathOutputRef; // 可空；有值则须为runtimeRoot相对引用并通过来源校验。
    double maskPixelSizeMm = 0.25; // 正数，mm/px；正式掩码0/128/255。
    bool enabled() const { return !runtimeRoot.isEmpty(); }
};

struct GeometryContractFrameV1 {
    pcv::ui::SelectedPlaneInputV2 input;
    QByteArray sourceDigest; // PLY为完整转换摘要；深度为原始C16/强度摘要，导出重读校验。
    pcv::stitching::SelectedPlaneFrame selected;
    pcv::planefitting::PlaneMaskMeshV1 interferenceMask;
    bool skipped = false; // 原输入顺序保留，跳过项不作为有效点云输出。
};

struct GeometryOutputRequestV1;
// 同步、无落盘；关闭时直接true。失败可写error，成功不保证清空旧值。
bool validateScanGeometryContractV1(const ScanGeometryContractV1 &context, QString *error);
// 纯路径计算，不校验/创建目录；调用前须校验context。
QString pointCloudContractDirectoryV1(const ScanGeometryContractV1 &context);
// 同步暂存资产和最后的JSON，不commit；调用方负责整笔事务commit/abort。
// document/error可空；成功可写document，失败可写error。会重读原始来源。
bool stagePointCloudContractV1(pcv::io::ArtifactTransaction &transaction,
    const GeometryOutputRequestV1 &request, QJsonObject *document, QString *error);
// 同步校验当前JSON子集及跨字段/相对路径形式；不证明磁盘资产或上游身份真实。
bool validatePointCloudResultV1(const QJsonObject &document, QString *error);

} // namespace pcv::demo
