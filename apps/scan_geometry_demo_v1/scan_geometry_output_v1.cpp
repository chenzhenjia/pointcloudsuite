#include "scan_geometry_output_v1.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace pcv::demo {
namespace {
QJsonArray vector3(const QVector3D &v) { return {double(v.x()), double(v.y()), double(v.z())}; }
QJsonArray pose(const pcv::roi::WorkpieceCoordinateSystem &w) {
    return {double(w.originInRobotBase.x()), double(w.originInRobotBase.y()), double(w.originInRobotBase.z()),
            double(w.poseA), double(w.poseB), double(w.poseC)};
}
QByteArray mappingBytes(const pcv::stitching::SelectedPlaneStitchResult &r, const QSet<int> &removed,
                        const std::function<bool()> &cancelled, bool *ok) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    qsizetype kept = 0;
    for (int i = 0; i < r.points.size(); ++i) if (!removed.contains(i)) ++kept;
    stream << quint32(0x314D4350) << quint32(1) << quint64(kept);
    for (qsizetype i = 0; i < r.points.size(); ++i) {
        if (cancelled && i % 4096 == 0 && cancelled()) { *ok = false; return {}; }
        if (removed.contains(int(i))) continue;
        stream << qint32(r.cloudIds.at(i)) << qint64(r.sourceIndices.at(i)) << r.scanRatios.at(i);
    }
    *ok = stream.status() == QDataStream::Ok;
    return bytes;
}
}

GeometryOutputResultV1 saveSelectedPlaneGeometryV1(const GeometryOutputRequestV1 &request) {
    GeometryOutputResultV1 out;
    if(!validateScanGeometryContractV1(request.contract,&out.error))return out;
    const auto cancelled = [&] { return request.isCancelled && request.isCancelled(); };
    if (!request.result.success || request.result.points.size() != request.result.cloudIds.size()
        || request.result.points.size() != request.result.sourceIndices.size()
        || request.result.points.size() != request.result.scanRatios.size()) {
        out.error = QStringLiteral("完整拼接结果来源映射不完整"); return out;
    }
    if (!request.wobj1.valid) { out.error = request.wobj1.error.isEmpty() ? QStringLiteral("WObj1 无效") : request.wobj1.error; return out; }
    if (cancelled()) { out.cancelled = true; out.error = QStringLiteral("已取消"); return out; }
    const QString directory = request.contract.enabled() ? pointCloudContractDirectoryV1(request.contract) : QDir(request.outputRoot).filePath(QStringLiteral("geometry_%1_%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
             QUuid::createUuid().toString(QUuid::Id128)));
    for(int index:request.removed)if(index<0||index>=request.result.points.size()) {
        out.error=QStringLiteral("修剪索引越界");return out;
    }
    QVector<pointcloud::Point3D> trimmed; trimmed.reserve(request.result.points.size() - request.removed.size());
    for (int i = 0; i < request.result.points.size(); ++i) {
        if (i%4096==0&&cancelled()) { out.cancelled = true; out.error = QStringLiteral("已取消"); return out; }
        if (!request.removed.contains(i)) trimmed.push_back(request.result.points.at(i));
    }
    bool mapOk = false;
    const QByteArray map = mappingBytes(request.result, request.removed, request.isCancelled, &mapOk);
    if (!mapOk) { out.cancelled = cancelled(); out.error = out.cancelled ? QStringLiteral("已取消") : QStringLiteral("修剪映射序列化失败"); return out; }
    QJsonArray sourceFrames;
    for (const auto &frame : request.frames) {
        QJsonArray interference; for (int index : frame.interferenceIndices) interference.append(index);
        sourceFrames.append(QJsonObject{{"frame_id", QString::number(frame.id)}, {"source_file", frame.sourceFile},
            {"scan_json", frame.scanJson}, {"interference_full_indices", interference},
            {"interference_point_count", frame.interferenceIndices.size()}});
    }
    auto deletedIndices=request.removed.values();std::sort(deletedIndices.begin(),deletedIndices.end());
    QJsonArray deleted;for(int index:deletedIndices)deleted.append(index);
    const QString originalRoot = QDir::cleanPath(request.result.outputPly.isEmpty() ? QString() : QFileInfo(request.result.outputPly).absolutePath());
    const QJsonObject sidecar{
        {"schema_version", "selected_plane_geometry_v1"}, {"kind", "selected_plane_geometry"},
        {"coordinate_frame", "robot_base"}, {"units", "mm"},
        {"wobj1", QJsonObject{{"origin", vector3(request.wobj1.originInRobotBase)},
            {"axis_x", vector3(request.wobj1.axisXInRobotBase)}, {"axis_y", vector3(request.wobj1.axisYInRobotBase)},
            {"axis_z", vector3(request.wobj1.axisZInRobotBase)}, {"pose_xyzabc", pose(request.wobj1)},
            {"reference_frame_id", request.referenceFrame < 0 ? QString() : QString::number(request.frames.value(request.referenceFrame).id)},
            {"orthogonality_error", double(request.wobj1.orthogonalityError)}}},
        {"source", QJsonObject{{"original_directory", originalRoot}, {"manifest", request.result.reportFile},
            {"report", request.result.reportFile}, {"result_revision", double(request.resultRevision)}, {"frames", sourceFrames}}},
        {"trim", QJsonObject{{"kept_point_count", trimmed.size()}, {"deleted_result_indices", deleted},
            {"trimmed", !request.removed.isEmpty()}}},
        {"interference", sourceFrames}
    };
    const QString ply = QStringLiteral("selected_plane_trimmed.ply"), pcvmap = QStringLiteral("selected_plane_trimmed.pcvmap"), json = QStringLiteral("selected_plane_geometry_v1.json");
    bool fullMapOk=false;
    const auto fullMap=mappingBytes(request.result,{},request.isCancelled,&fullMapOk);
    if(!fullMapOk){out.cancelled=cancelled();out.error=QStringLiteral("完整来源映射序列化未完成");return out;}
    pcv::io::ArtifactTransaction transaction(directory, false, request.isCancelled);
    if (!transaction.stageBinaryPly(QStringLiteral("selected_plane_robot_base.ply"),request.result.points,true)
        || !transaction.stageBytes(QStringLiteral("selected_plane_robot_base.pcvmap"),fullMap)
        || !transaction.stageJson(QStringLiteral("selected_plane_manifest.json"),QJsonDocument(request.result.report).toJson())
        || !transaction.stageJson(QStringLiteral("selected_plane_report.json"),QJsonDocument(request.result.report).toJson())
        || !transaction.stageBinaryPly(ply, trimmed, true, {QStringLiteral("derived trimmed selected plane points")})
        || !transaction.stageBytes(pcvmap, map)
        || !transaction.stageJson(json, QJsonDocument(sidecar).toJson(QJsonDocument::Indented))) {
        out.cancelled = cancelled(); out.error = out.cancelled ? QStringLiteral("已取消") : transaction.error(); return out;
    }
    if (request.contract.enabled() && !stagePointCloudContractV1(transaction, request, nullptr, &out.error)) {
        out.cancelled = cancelled(); return out;
    }
    if (!transaction.commit()) {
        out.cancelled = cancelled(); out.error = out.cancelled ? QStringLiteral("已取消") : transaction.error(); transaction.abort(); return out;
    }
    out.success = true; out.outputDirectory = directory; out.trimmedPly = QDir(directory).filePath(ply);
    out.trimmedMapping = QDir(directory).filePath(pcvmap); out.sidecar = QDir(directory).filePath(json);
    if (request.contract.enabled()) out.contractFile = QDir(directory).filePath("point_cloud_result.json");
    return out;
}
} // namespace pcv::demo
