#include "scan_geometry_contract_v1.h"
#include "scan_geometry_output_v1.h"

#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/io/ply_reader.h>
#include <QBuffer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pcv::demo {
namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool finite(const QVector3D &v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}
bool safeId(const QString &s) {
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$"));
    return expression.match(s).hasMatch();
}
bool relativeRef(const QString &s) {
    if (s.isEmpty() || s.contains('\\') || s.contains(':') || s.startsWith('/') || s != QDir::cleanPath(s)) return false;
    for (const auto &part : s.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
    return s != "system/temp" && !s.startsWith("system/temp/");
}
// Resolve every existing ancestor as well, so a junction cannot escape root.
QString resolved(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) return QDir::fromNativeSeparators(info.canonicalFilePath());
    const auto parent = info.absolutePath();
    if (parent == info.absoluteFilePath()) return {};
    return QDir(resolved(parent)).filePath(info.fileName());
}
bool beneath(const QString &root, const QString &path) {
    return path.startsWith(root + '/', Qt::CaseInsensitive);
}
bool timestamp(const QJsonValue &value) {
    const auto s = value.toString();
    static const QRegularExpression zone(QStringLiteral("(Z|[+-][0-9]{2}:[0-9]{2})$"));
    return zone.match(s).hasMatch() && QDateTime::fromString(s, Qt::ISODateWithMs).isValid();
}
bool numericArray(const QJsonValue &value, int count, bool positive = false) {
    if (!value.isArray() || value.toArray().size() != count) return false;
    for (const auto &v : value.toArray())
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || (positive && v.toDouble() <= 0)) return false;
    return true;
}
bool readObject(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("JSON object required: %1").arg(path));
    *object = document.object(); return true;
}
bool samePose(const QJsonValue &value, const pointcloud::RobotPose &pose) {
    if (!numericArray(value, 6)) return false;
    const auto array = value.toArray();
    const double expected[]{pose.x, pose.y, pose.z, pose.rx, pose.ry, pose.rz};
    for (int i = 0; i < 6; ++i) if (std::abs(array.at(i).toDouble() - expected[i]) > 0.0011) return false;
    return true;
}
bool validScan(const ScanGeometryContractV1 &c, const QVector<GeometryContractFrameV1> &frames, QString *error) {
    const auto path = QDir(c.runtimeRoot).filePath(c.scanPathOutputRef);
    if (!relativeRef(c.scanPathOutputRef) || !beneath(resolved(c.runtimeRoot), resolved(path)))
        return fail(error, QStringLiteral("scan_path_output_ref must stay inside runtime_data_root"));
    QJsonObject o;
    if (!readObject(path, &o, error)) return false;
    if (o.value("schema") != "sr2026.scan-path-output" || o.value("schema_version") != 1
        || o.value("job_id") != c.jobId || o.value("run_id") != c.runId || o.value("step_id") != c.stepId
        || o.value("business_code") != "global_scan" || o.value("execution_status") != "completed"
        || o.value("coordinate_frame") != "base" || o.value("path_count") != frames.size()
        || o.value("points_per_path") != 2 || !timestamp(o.value("generated_at")))
        return fail(error, QStringLiteral("scan_path_output identity/status/count/frame mismatch"));
    const auto points = o.value("points").toArray();
    if (points.size() != frames.size() * 2) return fail(error, QStringLiteral("scan_path_output endpoint count mismatch"));
    for (int i = 0; i < frames.size(); ++i)
        if (!samePose(points.at(i * 2), frames.at(i).input.start) || !samePose(points.at(i * 2 + 1), frames.at(i).input.end))
            return fail(error, QStringLiteral("scan_path_output endpoints differ from input frame %1").arg(i + 1));
    const auto plan = o.value("source_plan_ref").toString();
    const auto planPath = QDir(c.runtimeRoot).filePath(plan);
    if (!relativeRef(plan) || !beneath(resolved(c.runtimeRoot), resolved(planPath)) || !QFileInfo(planPath).isFile())
        return fail(error, QStringLiteral("scan_path_output source_plan_ref missing or outside runtime root"));
    QJsonObject p;
    if (!readObject(planPath, &p, error)) return false;
    if (p.value("schema")!="sr2026.process-plan" || p.value("schema_version")!=1
        || p.value("job_id") != c.jobId || p.value("run_id") != c.runId || p.value("step_id") != c.stepId
        || p.value("workpiece_id")!=c.workpieceId || p.value("process").toObject().value("business_code") != "global_scan")
        return fail(error, QStringLiteral("source plan identity mismatch"));
    return true;
}
// Rasterize measured occupied cells in WObj XY. Image top is +Y, right is +X;
// its physical rectangle is symmetric about the candidate WObj origin.
bool maskImage(const GeometryOutputRequestV1 &r, const QVector<pointcloud::Point3D> &trimmed,
               QImage *image, QJsonObject *metadata, QString *error) {
    QVector<int> indices; indices.reserve(trimmed.size());
    for (int i = 0; i < trimmed.size(); ++i) indices.append(i);
    const auto &w = r.wobj1;
    const auto n = w.axisZInRobotBase;
    pcv::planefitting::PlaneModel model;
    model.a = n.x(); model.b = n.y(); model.c = n.z(); model.d = -QVector3D::dotProduct(n, w.originInRobotBase);
    pcv::planefitting::PlaneMaskOptionsV1 options;
    options.samplingAxisU = w.axisXInRobotBase; options.isCancelled = r.isCancelled;
    // Rebuild from complete, retained points: a previously displayed mesh would
    // still contain trimmed cells and omit later frames/cumulative corrections.
    const auto mask = pcv::planefitting::buildPlaneMaskV1(trimmed, indices, model, options);
    if (!mask.ok) return fail(error, mask.error);
    QVector<pcv::planefitting::PlaneMaskMeshV1> forbidden;
    int keptFrame = 0;
    for (const auto &frame : r.contractFrames) {
        if (frame.skipped) continue;
        auto mesh = frame.interferenceMask;
        if (keptFrame >= r.result.corrections.size()) return fail(error, QStringLiteral("Missing frame correction"));
        const auto &correction = r.result.corrections.at(keptFrame++);
        for (auto &v : mesh.vertices) v = correction.map(v);
        forbidden.append(std::move(mesh));
    }
    const auto xy = [&w](const QVector3D &p) {
        const auto v = w.robotBaseToWorkpiece.map(p); return QPointF(v.x(), v.y());
    };
    double halfX = 0, halfY = 0;
    for (const auto &p : mask.mesh.vertices) {
        const auto v = xy(p);
        if (!finite(p) || !std::isfinite(v.x()) || !std::isfinite(v.y())) return fail(error, QStringLiteral("Non-finite mask vertex"));
        halfX = std::max(halfX, std::abs(v.x())); halfY = std::max(halfY, std::abs(v.y()));
    }
    const double pixel = r.contract.maskPixelSizeMm;
    const double width = std::ceil(halfX * 2 / pixel), height = std::ceil(halfY * 2 / pixel);
    if (width < 1 || height < 1 || width > 16384 || height > 16384 || width * height > 16777216)
        return fail(error, QStringLiteral("Mask exceeds 16M pixels/16384 per axis; increase mask_pixel_size_mm"));
    const int nx = int(width), ny = int(height);
    const double sx = nx * pixel, sy = ny * pixel;
    QImage rgb(nx, ny, QImage::Format_RGB32);
    if (rgb.isNull()) return fail(error, QStringLiteral("Cannot allocate plane mask"));
    rgb.fill(Qt::black);
    QPainter painter(&rgb); painter.setPen(Qt::NoPen); painter.setRenderHint(QPainter::Antialiasing, false);
    const auto draw = [&](const pcv::planefitting::PlaneMaskMeshV1 &mesh, int gray) {
        if (mesh.triangleIndices.size() % 3) return false;
        painter.setBrush(QColor(gray, gray, gray));
        for (qsizetype i = 0; i < mesh.triangleIndices.size(); i += 3) {
            if ((i % 4098) == 0 && r.isCancelled && r.isCancelled()) return false;
            QPolygonF polygon;
            for (int j = 0; j < 3; ++j) {
                const auto id = mesh.triangleIndices.at(i + j);
                if (id >= quint32(mesh.vertices.size()) || !finite(mesh.vertices.at(id))) return false;
                const auto p = xy(mesh.vertices.at(id));
                polygon << QPointF((p.x() + sx / 2) / pixel, (sy / 2 - p.y()) / pixel);
            }
            painter.drawPolygon(polygon);
        }
        return true;
    };
    if (!draw(mask.mesh, 128)) return fail(error, QStringLiteral("Invalid/cancelled workpiece mask"));
    for (const auto &mesh : forbidden) if (!draw(mesh, 255)) return fail(error, QStringLiteral("Invalid/cancelled interference mask"));
    painter.end();
    *image = rgb.convertToFormat(QImage::Format_Grayscale8);
    bool hasTemplate = false;
    for (int y = 0; y < ny; ++y) {
        if (r.isCancelled && r.isCancelled()) return fail(error, QStringLiteral("cancelled"));
        const auto row = image->constScanLine(y);
        for (int x = 0; x < nx; ++x) hasTemplate = hasTemplate || row[x] == 128;
    }
    if (!hasTemplate) return fail(error, QStringLiteral("Mask has no usable template pixels"));
    *metadata = {{"pixel_size_px", QJsonArray{nx, ny}}, {"physical_size_mm", QJsonArray{sx, sy}},
        {"mm_per_pixel", QJsonArray{pixel, pixel}},
        {"gray_value_map", QJsonObject{{"background", 0}, {"template", 128}, {"forbidden", 255}}}};
    return true;
}
}

QString pointCloudContractDirectoryV1(const ScanGeometryContractV1 &c) {
    return QDir(c.runtimeRoot).filePath(QStringLiteral("jobs/%1/runs/%2/03-point-cloud").arg(c.jobId, c.runId));
}

bool validateScanGeometryContractV1(const ScanGeometryContractV1 &c, QString *error) {
    if (!c.enabled()) return true;
    if (!QFileInfo(c.runtimeRoot).isAbsolute() || QDir(c.runtimeRoot).isRoot())
        return fail(error, QStringLiteral("runtime_data_root must be an absolute non-root directory"));
    const auto root = resolved(c.runtimeRoot);
    const auto source = resolved(QString::fromUtf8(PCV_SCAN_GEOMETRY_SOURCE_ROOT));
    if (root.isEmpty() || root.compare(source, Qt::CaseInsensitive) == 0 || beneath(source, root))
        return fail(error, QStringLiteral("runtime_data_root must stay outside source tree"));
    for (const auto &id : {c.jobId, c.runId, c.stepId, c.workpieceId, c.controllerId})
        if (!safeId(id)) return fail(error, QStringLiteral("Business IDs require 1-96 ASCII letters/digits/_/-"));
    if (c.wobjId != 1) return fail(error, QStringLiteral("Current geometry implementation supports wobj_id=1"));
    if (!std::isfinite(c.maskPixelSizeMm) || c.maskPixelSizeMm <= 0)
        return fail(error, QStringLiteral("mask_pixel_size_mm must be finite and positive"));
    const auto target = resolved(pointCloudContractDirectoryV1(c));
    if (!beneath(root, target)) return fail(error, QStringLiteral("Output directory escapes runtime_data_root"));
    if(!beneath(root,resolved(QDir(c.runtimeRoot).filePath("system/temp"))))
        return fail(error,QStringLiteral("Temporary directory escapes runtime_data_root"));
    if (!c.scanPathOutputRef.isEmpty() && !relativeRef(c.scanPathOutputRef))
        return fail(error, QStringLiteral("Invalid scan_path_output_ref"));
    return true;
}

bool validatePointCloudResultV1(const QJsonObject &o, QString *error) {
    if (o.value("schema") != "sr2026.point-cloud-result" || o.value("schema_version") != 1
        || o.value("business_code") != "global_scan" || !timestamp(o.value("generated_at")))
        return fail(error, QStringLiteral("Invalid point-cloud-result schema/business/time"));
    for (const auto &key : {"job_id", "run_id", "step_id", "workpiece_id"})
        if (!safeId(o.value(key).toString())) return fail(error, QStringLiteral("Invalid identity: %1").arg(key));
    if (!o.value("template_ref").isNull() || !o.value("template_version").isNull()
        || o.value("algorithm_version").toString().isEmpty()) return fail(error, QStringLiteral("Initial modeling requires null template references"));
    const auto status = o.value("status").toString();
    if (status != "completed" && status != "partial" && status != "failed") return fail(error, QStringLiteral("Invalid result status"));
    const auto inputs = o.value("inputs").toObject();
    const auto scan = inputs.value("scan_path_output_ref");
    if ((!scan.isUndefined() && !relativeRef(scan.toString())) || (status == "completed" && scan.isUndefined()))
        return fail(error, QStringLiteral("Invalid or missing actual scan result reference"));
    if (!relativeRef(inputs.value("calibration_ref").toString()) || !inputs.value("raw_clouds").isArray())
        return fail(error, QStringLiteral("Invalid calibration/raw inputs"));
    QSet<int> paths;
    for (const auto &v : inputs.value("raw_clouds").toArray()) {
        const auto raw = v.toObject(); const double number = raw.value("path_index").toDouble();
        if (number < 1 || number > std::numeric_limits<int>::max() || number != std::floor(number)
            || paths.contains(int(number)) || !relativeRef(raw.value("ref").toString()))
            return fail(error, QStringLiteral("Invalid raw_clouds path_index/ref"));
        paths.insert(int(number));
    }
    const auto validation = o.value("validation").toObject();
    if (!validation.value("errors").isArray() || !validation.value("warnings").isArray()) return fail(error, QStringLiteral("Validation arrays required"));
    for (const auto &key : {"errors", "warnings"}) for (const auto &value : validation.value(key).toArray())
        if (!value.isString()) return fail(error, QStringLiteral("Validation entries must be strings"));
    if (status == "completed" && !validation.value("errors").toArray().isEmpty()) return fail(error, QStringLiteral("Completed result has errors"));
    if (!o.value("outputs").isObject()) return fail(error, QStringLiteral("outputs object required"));
    const auto outputs = o.value("outputs").toObject();
    if (status == "failed") return outputs.isEmpty() || fail(error, QStringLiteral("Failed adapter result must have no outputs"));
    if (paths.size()<2 || !relativeRef(outputs.value("base_cloud_ref").toString())) return fail(error, QStringLiteral("Missing actual clouds"));
    const auto geometry = outputs.value("template_geometry").toObject();
    const auto wobj = outputs.value("wobj_candidate").toObject();
    if (geometry.value("coordinate_frame") != "base" || !relativeRef(geometry.value("point_cloud_ref").toString())
        || wobj.value("coordinate_frame") != "base" || wobj.value("method") != "point_cloud_fit"
        || !safeId(wobj.value("controller_id").toString()) || wobj.value("wobj_id") != 1 || !numericArray(wobj.value("wobj_pose"), 6))
        return fail(error, QStringLiteral("Invalid candidate frame/WObj"));
    const auto mask = geometry.value("plane_mask").toObject();
    if (!relativeRef(mask.value("ref").toString()) || !numericArray(mask.value("pixel_size_px"), 2, true)
        || !numericArray(mask.value("physical_size_mm"), 2, true) || !numericArray(mask.value("mm_per_pixel"), 2, true))
        return fail(error, QStringLiteral("Invalid mask reference/dimensions"));
    for (int i = 0; i < 2; ++i) {
        const double px = mask.value("pixel_size_px").toArray().at(i).toDouble();
        const double size = mask.value("physical_size_mm").toArray().at(i).toDouble();
        const double scale = mask.value("mm_per_pixel").toArray().at(i).toDouble();
        if (px != std::floor(px) || px > 16384 || std::abs(px * scale - size) > 1e-6 * std::max(1.0, size))
            return fail(error, QStringLiteral("Inconsistent mask scale"));
    }
    const auto gray = mask.value("gray_value_map").toObject();
    if (gray.value("background") != 0 || gray.value("template") != 128 || gray.value("forbidden") != 255)
        return fail(error, QStringLiteral("Invalid mask gray mapping"));
    return true;
}

bool stagePointCloudContractV1(pcv::io::ArtifactTransaction &tx, const GeometryOutputRequestV1 &r,
                             QJsonObject *document, QString *error) {
    const auto &c = r.contract;
    if (!validateScanGeometryContractV1(c, error) || !c.enabled()) return false;
    if (QFileInfo(tx.outputRoot()).absoluteFilePath() != QFileInfo(pointCloudContractDirectoryV1(c)).absoluteFilePath())
        return fail(error, QStringLiteral("Contract transaction directory mismatch"));
    if (r.contractFrames.size() < 2 || !r.wobj1.valid) return fail(error,QStringLiteral("Confirmed geometry/WObj and at least two source frames required"));
    if (!pointcloud::validateRigidTransform(r.wobj1.workpieceToRobotBase, error)) return false;
    for (double value : {double(r.wobj1.poseA),double(r.wobj1.poseB),double(r.wobj1.poseC)})
        if(!std::isfinite(value))return fail(error,QStringLiteral("Non-finite WObj angles"));
    const auto &w=r.wobj1;
    if(!finite(w.axisXInRobotBase)||!finite(w.axisYInRobotBase)||!finite(w.axisZInRobotBase))
        return fail(error,QStringLiteral("Non-finite WObj axes"));
    const auto inverse=w.workpieceToRobotBase.inverted();
    for(int i=0;i<3;++i) {
        if(std::abs(w.workpieceToRobotBase(i,0)-w.axisXInRobotBase[i])>1e-5
            ||std::abs(w.workpieceToRobotBase(i,1)-w.axisYInRobotBase[i])>1e-5
            ||std::abs(w.workpieceToRobotBase(i,2)-w.axisZInRobotBase[i])>1e-5)
            return fail(error,QStringLiteral("WObj axes/matrix mismatch"));
    }
    for(int i=0;i<16;++i)if(std::abs(inverse.constData()[i]-w.robotBaseToWorkpiece.constData()[i])>1e-5)
        return fail(error,QStringLiteral("WObj inverse matrix mismatch"));
    const auto roundtrip = pointcloud::robotPoseToMatrix(pointcloud::matrixToRobotPose(r.wobj1.workpieceToRobotBase));
    const pointcloud::RobotPose wpose{r.wobj1.originInRobotBase.x(), r.wobj1.originInRobotBase.y(), r.wobj1.originInRobotBase.z(), r.wobj1.poseA, r.wobj1.poseB, r.wobj1.poseC};
    const auto stated = pointcloud::robotPoseToMatrix(wpose);
    for (int i = 0; i < 16; ++i)
        if (std::abs(stated.constData()[i] - roundtrip.constData()[i]) > 0.001)
            return fail(error, QStringLiteral("WObj pose/matrix mismatch"));
    if (!finite(r.wobj1.originInRobotBase)) return fail(error, QStringLiteral("Non-finite WObj"));
    QString scanError;
    if (!c.scanPathOutputRef.isEmpty() && !validScan(c, r.contractFrames, &scanError)) return fail(error, scanError);
    const auto baseRef = QStringLiteral("jobs/%1/runs/%2/03-point-cloud/").arg(c.jobId, c.runId);
    const auto cancelled = [&] { return r.isCancelled && r.isCancelled(); };
    QJsonArray raws, evidenceFrames;
    QByteArray calibrationBytes;
    int keptFrame = 0;
    for (int i = 0; i < r.contractFrames.size(); ++i) {
        if (cancelled()) return fail(error, QStringLiteral("cancelled"));
        const auto &frame = r.contractFrames.at(i);
        if (frame.skipped) {evidenceFrames.append(QJsonObject{{"path_index",i+1},{"skipped",true}});continue;}
        const auto &input = frame.input;
        QFile xml(input.calibration);
        if (!xml.open(QIODevice::ReadOnly)) return fail(error, xml.errorString());
        const auto bytes = xml.readAll();
        if (calibrationBytes.isEmpty()) calibrationBytes = bytes;
        if (bytes != calibrationBytes) return fail(error, QStringLiteral("One contract result requires one identical calibration XML"));
        pointcloud::HandEyeCalibration calibration;
        if (!pointcloud::loadHandEyeCalibration(input.calibration, &calibration, error)) return false;
        for (int k = 0; k < 16; ++k)
            if (std::abs(calibration.flangeFromDepth.constData()[k] - frame.selected.cloud.flangeFromDepth.constData()[k]) > 1e-6)
                return fail(error, QStringLiteral("Calibration changed after plane confirmation"));
        QVector<pointcloud::Point3D> rawPoints;
        pointcloud::CloudTransformOptions options;
        options.layout = input.layout; options.collectSamplePoints = false; options.isCancelled = r.isCancelled;
        if (input.depthFrame) {
            const auto source = pcv::camera::readCapturedFrame(input.path, r.isCancelled, pcv::camera::CaptureFrameReadMode::PreviewOnly);
            if (!source.ok || !source.rawFrame) return fail(error, source.error);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(source.rawFrame->data); hash.addData(source.rawFrame->intensityData);
            if (hash.result()!=frame.sourceDigest) return fail(error, QStringLiteral("Depth source changed since confirmation"));
            const auto converted = pcv::camera::materializeCameraFrame(*source.rawFrame);
            if (!converted.ok) return fail(error, converted.error);
            rawPoints = converted.points; options.layout = pointcloud::DepthPointLayout::FullXyz;
            options.useFullXyzRasterRowProgress = true; options.rasterWidth = source.rawFrame->width;
            options.rasterHeight = source.rawFrame->height; options.rasterSourceIndices = converted.sourceIndices;
        } else {
            pcv::detail::io::PlyReadOptions read; read.isCancelled = r.isCancelled;
            const auto raw = pcv::detail::io::readPly(input.path, read);
            if (!raw.ok) return fail(error, raw.error);
            rawPoints = raw.points;
        }
        const auto transformed = pointcloud::transformLineScanToRobotBase(rawPoints, calibration, input.start, input.end, options);
        if (!transformed.ok) return fail(error, transformed.error);
        if(!input.depthFrame) {
            pointcloud::RobotBaseFrame loaded;
            loaded.sourceFile=input.path;loaded.declaredCount=rawPoints.size();
            loaded.startBaseFromFlange=pointcloud::robotPoseToMatrix(input.start);
            loaded.endBaseFromFlange=pointcloud::robotPoseToMatrix(input.end);
            loaded.flangeFromDepth=calibration.flangeFromDepth;loaded.signedTravel=transformed.signedTravel;
            loaded.fullPoints=transformed.points;loaded.sourceIndices=transformed.sourceIndices;loaded.scanRatios=transformed.scanRatios;
            const auto token=std::make_shared<std::atomic_bool>(cancelled());
            if(pcv::ui::loadedCloudDigestV2(loaded,token)!=frame.sourceDigest)
                return fail(error,QStringLiteral("PLY source changed since confirmation"));
        }
        // Compare confirmed selected points to the reread source, by original
        // raster index. Never reapply hand-eye to the already-base output.
        const auto &selected = frame.selected.cloud;
        if (selected.fullPoints.size() != selected.sourceIndices.size()) return fail(error, QStringLiteral("Invalid selected provenance"));
        qsizetype cursor = 0;
        for (qsizetype j = 0; j < selected.sourceIndices.size(); ++j) {
            if ((j % 4096) == 0 && cancelled()) return fail(error, QStringLiteral("cancelled"));
            const auto sourceIndex = selected.sourceIndices.at(j);
            while (cursor < transformed.sourceIndices.size() && transformed.sourceIndices.at(cursor) < sourceIndex) ++cursor;
            if (cursor >= transformed.sourceIndices.size() || transformed.sourceIndices.at(cursor) != sourceIndex)
                return fail(error, QStringLiteral("Source indices changed since confirmation"));
            const auto &a = selected.fullPoints.at(j); const auto &b = transformed.points.at(cursor);
            if (QVector3D(a.x-b.x, a.y-b.y, a.z-b.z).length() > 0.001f)
                return fail(error, QStringLiteral("Source geometry changed since confirmation"));
        }
        const auto rawName = QStringLiteral("raw/scan_%1.ply").arg(i + 1, 3, 10, QLatin1Char('0'));
        if (!tx.stageBinaryPly(rawName, rawPoints, true)) return fail(error, tx.error());
        // Additional source evidence stays outside the public JSON shape.
        // Depth raster indices are meaningful only with the original owned frame.
        if(input.depthFrame) {
            QDirIterator files(input.path,QDir::Files|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
            while(files.hasNext()) {
                const auto path=files.next();const auto info=files.fileInfo();
                if(info.isSymLink()||cancelled()) return fail(error,QStringLiteral("Linked/cancelled depth source"));
                QFile f(path);if(!f.open(QIODevice::ReadOnly))return fail(error,f.errorString());
                const auto target=QStringLiteral("source/depth_%1/").arg(i+1,3,10,QLatin1Char('0'))+QDir(input.path).relativeFilePath(path);
                if(!tx.stageBytes(target,f.readAll()))return fail(error,tx.error());
            }
        }
        raws.append(QJsonObject{{"path_index", i + 1}, {"ref", baseRef + rawName}});
        const auto poseArray=[](const pointcloud::RobotPose &p){return QJsonArray{p.x,p.y,p.z,p.rx,p.ry,p.rz};};
        QJsonObject frameEvidence{{"path_index",i+1},{"raw_cloud_ref",baseRef+rawName},
            {"layout",input.depthFrame||input.layout==pointcloud::DepthPointLayout::FullXyz?"FullXyz":"LineProfileXz"},
            {"start_pose",poseArray(input.start)},{"end_pose",poseArray(input.end)},
            {"confirmed_source_digest",QString::fromLatin1(frame.sourceDigest.toHex())},
            {"selection_parameters",frame.selected.selectionParameters}};
        if(input.depthFrame)frameEvidence.insert("depth_frame_ref",baseRef+QStringLiteral("source/depth_%1/frame.json").arg(i+1,3,10,QLatin1Char('0')));
        evidenceFrames.append(frameEvidence);
        ++keptFrame;
    }
    if (keptFrame < 2 || keptFrame != r.result.corrections.size()) return fail(error, QStringLiteral("Selected frame/correction count mismatch"));
    if(r.result.report.value("coordinate_frame")!="robot_base")return fail(error,QStringLiteral("Stitch result must declare robot_base"));
    for(const auto &m:r.result.corrections) if(!pointcloud::validateRigidTransform(m,error))return false;
    QVector<pointcloud::Point3D> trimmed;
    trimmed.reserve(r.result.points.size() - r.removed.size());
    for (int i = 0; i < r.result.points.size(); ++i) {
        if ((i % 4096) == 0 && cancelled()) return fail(error, QStringLiteral("cancelled"));
        const auto &p = r.result.points.at(i);
        if (!finite({p.x,p.y,p.z}) || !finite({p.nx,p.ny,p.nz})) return fail(error, QStringLiteral("Non-finite output point/normal"));
        if (!r.removed.contains(i)) trimmed.append(p);
    }
    if (trimmed.size() < 3) return fail(error, QStringLiteral("Insufficient retained geometry"));
    QImage image; QJsonObject mask;
    if (!maskImage(r, trimmed, &image, &mask, error)) return false;
    mask.insert("ref", baseRef + "plane_mask.png");
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return fail(error, QStringLiteral("Cannot encode Grayscale8 PNG"));
    QJsonObject inputs{{"calibration_ref", baseRef + "source/handeye.xml"}, {"raw_clouds", raws}};
    QJsonArray warnings{QStringLiteral("probe_targets_not_available"), QStringLiteral("stitch_quality_warn_only")};
    if (!c.scanPathOutputRef.isEmpty()) inputs.insert("scan_path_output_ref", c.scanPathOutputRef);
    else warnings.append(QStringLiteral("scan_path_output_not_available"));
    for (const auto &frame : r.contractFrames) if (frame.skipped) { warnings.append(QStringLiteral("input_frames_skipped")); break; }
    QJsonObject o{{"schema", "sr2026.point-cloud-result"}, {"schema_version", 1},
        {"job_id", c.jobId}, {"run_id", c.runId}, {"step_id", c.stepId}, {"business_code", "global_scan"},
        {"workpiece_id", c.workpieceId}, {"template_ref", QJsonValue(QJsonValue::Null)}, {"template_version", QJsonValue(QJsonValue::Null)},
        {"algorithm_version", "point-cloud-processing-v001"}, {"status", c.scanPathOutputRef.isEmpty() || raws.size() != r.contractFrames.size() ? "partial" : "completed"},
        {"inputs", inputs}, {"outputs", QJsonObject{{"base_cloud_ref", baseRef + "stitched_base.ply"},
            {"template_geometry", QJsonObject{{"coordinate_frame", "base"}, {"point_cloud_ref", baseRef + "template_candidate.ply"}, {"plane_mask", mask}}},
            {"wobj_candidate", QJsonObject{{"coordinate_frame", "base"}, {"controller_id", c.controllerId}, {"wobj_id", c.wobjId},
                {"wobj_pose", QJsonArray{wpose.x,wpose.y,wpose.z,wpose.rx,wpose.ry,wpose.rz}}, {"method", "point_cloud_fit"}}}}},
        {"validation", QJsonObject{{"errors", QJsonArray{}}, {"warnings", warnings}}},
        {"generated_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!validatePointCloudResultV1(o, error)) return false;
    // stageGrayscalePng intentionally binarizes legacy masks; preserve ternary
    // pixels using encoded PNG bytes without changing that stable API.
    const QJsonObject evidence{{"kind","scan_geometry_processing_evidence_v1"},{"result_revision",double(r.resultRevision)},
        {"reference_frame",r.referenceFrame},{"mask_pixel_size_mm",c.maskPixelSizeMm},{"frames",evidenceFrames},
        {"mask_convention","center=WObj XY origin; image right=+X; image top=+Y; cell coverage; forbidden overrides template"}};
    QString json=QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
    const QString poseText=QStringLiteral("\"wobj_pose\": [%1, %2, %3, %4, %5, %6]")
        .arg(wpose.x,0,'f',3).arg(wpose.y,0,'f',3).arg(wpose.z,0,'f',3)
        .arg(wpose.rx,0,'f',3).arg(wpose.ry,0,'f',3).arg(wpose.rz,0,'f',3);
    json.replace(QRegularExpression(QStringLiteral("\"wobj_pose\":\\s*\\[[^\\]]*\\]")),poseText);
    if (!tx.stageBytes("source/handeye.xml", calibrationBytes)
        || !tx.stageJson("source/processing_evidence_v1.json",QJsonDocument(evidence).toJson(QJsonDocument::Indented))
        || !tx.stageBinaryPly("stitched_base.ply", r.result.points, true)
        || !tx.stageBinaryPly("template_candidate.ply", trimmed, true)
        || !tx.stageBytes("plane_mask.png", png)
        || !tx.stageJson("point_cloud_result.json", json.toUtf8())) return fail(error, tx.error());
    if (document) *document = o;
    return true;
}
} // namespace pcv::demo
