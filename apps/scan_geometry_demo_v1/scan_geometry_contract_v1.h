#pragma once

#include <selected_plane_tasks_v2.h>
#include <pcv/io/artifact_transaction.h>
#include <QJsonObject>

namespace pcv::demo {

// Application-private SR-2026 adapter. Empty runtimeRoot preserves legacy output.
struct ScanGeometryContractV1 {
    QString runtimeRoot, jobId, runId, stepId, workpieceId, controllerId;
    int wobjId = 1;
    QString scanPathOutputRef;
    double maskPixelSizeMm = 0.25;
    bool enabled() const { return !runtimeRoot.isEmpty(); }
};

struct GeometryContractFrameV1 {
    pcv::ui::SelectedPlaneInputV2 input;
    QByteArray sourceDigest;
    pcv::stitching::SelectedPlaneFrame selected;
    pcv::planefitting::PlaneMaskMeshV1 interferenceMask;
    bool skipped = false;
};

struct GeometryOutputRequestV1;
bool validateScanGeometryContractV1(const ScanGeometryContractV1 &context, QString *error);
QString pointCloudContractDirectoryV1(const ScanGeometryContractV1 &context);
// Stages into the same transaction as the legacy geometry sidecar. JSON is last.
bool stagePointCloudContractV1(pcv::io::ArtifactTransaction &transaction,
    const GeometryOutputRequestV1 &request, QJsonObject *document, QString *error);
// Checks the emitted v1 subset, including cross-field and path invariants.
bool validatePointCloudResultV1(const QJsonObject &document, QString *error);

} // namespace pcv::demo
