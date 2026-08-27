#include <pcv/interface/temp_workpiece_interface.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writePly(const QString &path)
{
    const QByteArray vertices = QByteArrayLiteral("1 0 1\n10 5 1\n0 10 1\n10 10 1\n");
    return writeFile(path, QByteArrayLiteral("ply\nformat ascii 1.0\n element vertex 4\n")
        .replace(" element", "element")
        + QByteArrayLiteral("\nproperty float x\nproperty float y\nproperty float z\nend_header\n")
        + vertices);
}

bool writeCalibration(const QString &path)
{
    return writeFile(path, QByteArrayLiteral(
        "<ArithConfig><RTmatDepth2robot>"
        "<RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/>"
        "<TVec t0=\"0\" t1=\"0\" t2=\"0\"/>"
        "</RTmatDepth2robot></ArithConfig>"));
}

bool writeCalibrationWithTranslation(const QString &path, int x, int y, int z)
{
    return writeFile(path, QByteArrayLiteral(
        "<ArithConfig><RTmatDepth2robot>"
        "<RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/>"
        "<TVec t0=\"%1\" t1=\"%2\" t2=\"%3\"/>"
        "</RTmatDepth2robot></ArithConfig>")
        .replace("%1", QByteArray::number(x))
        .replace("%2", QByteArray::number(y))
        .replace("%3", QByteArray::number(z)));
}

QJsonObject inputJson(const QString &layout, const QString &ply, const QString &xml)
{
    return QJsonObject{
        {QStringLiteral("schema_version"), QString::fromLatin1(pcv::interface::kTempScanningSchema)},
        {QStringLiteral("kind"), QString::fromLatin1(pcv::interface::kTempScanningKind)},
        {QStringLiteral("created_at"), QStringLiteral("2026-08-24T08:30:00.000+08:00")},
        {QStringLiteral("scan"), QJsonObject{
            {QStringLiteral("scan_id"), QStringLiteral("scan_001")},
            {QStringLiteral("point_cloud_file"), ply},
            {QStringLiteral("point_cloud_layout"), layout},
            {QStringLiteral("coordinate_frame"), QStringLiteral("camera")},
            {QStringLiteral("robot_pose_start"), QJsonArray{0, 0, 0, 0, 0, 0}},
            {QStringLiteral("robot_pose_end"), QJsonArray{0, 10, 0, 0, 0, 0}}}},
        {QStringLiteral("calibration"), QJsonObject{
            {QStringLiteral("calibration_file"), xml},
            {QStringLiteral("source_frame"), QStringLiteral("camera")},
            {QStringLiteral("target_frame"), QStringLiteral("robot_base")}}}
    };
}

void assertMinimalWorkpieceJson(const QJsonObject &output)
{
    assert((output.keys() == QStringList{
        QStringLiteral("created_at"), QStringLiteral("image"), QStringLiteral("kind"),
        QStringLiteral("outputs"), QStringLiteral("plane"), QStringLiteral("roi"),
        QStringLiteral("schema_version")}));
    assert((output.value(QStringLiteral("plane")).toObject().keys() == QStringList{
        QStringLiteral("equation"), QStringLiteral("wobj_num")}));
    assert((output.value(QStringLiteral("image")).toObject().keys() == QStringList{
        QStringLiteral("height_mm"), QStringLiteral("height_px"), QStringLiteral("name"),
        QStringLiteral("pixel_size_mm"), QStringLiteral("width_mm"), QStringLiteral("width_px")}));
    assert((output.value(QStringLiteral("outputs")).toObject().keys() == QStringList{
        QStringLiteral("plane_mask"), QStringLiteral("robot_base_point_cloud"),
        QStringLiteral("roi_point_cloud")}));
    assert(output.value(QStringLiteral("roi")).toString() == QStringLiteral("rectangle"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    assert(root.isValid());
    const QString dir = root.filePath(QStringLiteral("input"));
    assert(QDir().mkpath(dir));
    const QString ply = QDir(dir).filePath(QStringLiteral("scan.ply"));
    const QString xml = QDir(dir).filePath(QStringLiteral("calibration.xml"));
    const QString json = QDir(dir).filePath(QStringLiteral("temp_scanning_info.json"));
    assert(writePly(ply));
    assert(writeCalibration(xml));
    assert(writeFile(json, QJsonDocument(inputJson(QStringLiteral("LineProfileXz"),
                                                   QStringLiteral("scan.ply"),
                                                   QStringLiteral("calibration.xml")))
        .toJson(QJsonDocument::Compact)));

    pcv::interface::TempScanningInfo info;
    QString error;
    assert(pcv::interface::parseTempScanningInfo(json, &info, &error));
    assert(info.createdAtIso8601 == QStringLiteral("2026-08-24T08:30:00.000+08:00"));
    pcv::interface::TempWorkpieceOptions options;
    options.scanningInfoPath = json;
    const auto prepared = pcv::interface::prepareTempWorkpiece(options, &error);
    assert(prepared.success);
    assert(prepared.pointCloudLayout == pointcloud::DepthPointLayout::LineProfileXz);
    assert(prepared.robotBasePoints.size() == 4);
    assert(prepared.sourceIndices == QVector<qsizetype>({0, 1, 2, 3}));
    assert(prepared.resolvedPointCloudFile == QFileInfo(ply).absoluteFilePath());
    assert(prepared.resolvedCalibrationFile == QFileInfo(xml).absoluteFilePath());
    assert(!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("baseline_robot_base.ply"))));

    const auto assertFixedEquationText = [](const QByteArray &bytes) {
        const QByteArray marker = QByteArrayLiteral("\"equation\": [");
        const int begin = bytes.indexOf(marker);
        assert(begin >= 0);
        const int end = bytes.indexOf(']', begin + marker.size());
        assert(end > begin);
        const QList<QByteArray> values = bytes.mid(begin + marker.size(), end - begin - marker.size()).split(',');
        assert(values.size() == 6);
        for (QByteArray value : values) {
            value = value.trimmed();
            const int dot = value.indexOf('.');
            assert(dot > 0);
            assert(value.size() - dot - 1 == 3);
        }
    };

    QJsonObject missingLayout = inputJson(QStringLiteral("LineProfileXz"),
                                          QStringLiteral("scan.ply"),
                                          QStringLiteral("calibration.xml"));
    QJsonObject missingLayoutScan = missingLayout.value(QStringLiteral("scan")).toObject();
    missingLayoutScan.remove(QStringLiteral("point_cloud_layout"));
    missingLayout.insert(QStringLiteral("scan"), missingLayoutScan);
    const QString missingLayoutJson = QDir(dir).filePath(QStringLiteral("missing_layout.json"));
    assert(writeFile(missingLayoutJson,
                     QJsonDocument(missingLayout).toJson(QJsonDocument::Compact)));
    pcv::interface::TempScanningInfo missingLayoutInfo;
    assert(pcv::interface::parseTempScanningInfo(missingLayoutJson,
                                                  &missingLayoutInfo, &error));
    assert(missingLayoutInfo.pointCloudLayout == pointcloud::DepthPointLayout::LineProfileXz);
    assert(missingLayoutInfo.warning.contains(
        QStringLiteral("point_cloud_layout missing; defaulted to LineProfileXz")));

    QJsonObject missingCreatedAt = inputJson(QStringLiteral("LineProfileXz"),
                                             QStringLiteral("scan.ply"),
                                             QStringLiteral("calibration.xml"));
    missingCreatedAt.remove(QStringLiteral("created_at"));
    const QString missingCreatedAtJson = QDir(dir).filePath(QStringLiteral("missing_created_at.json"));
    assert(writeFile(missingCreatedAtJson,
                     QJsonDocument(missingCreatedAt).toJson(QJsonDocument::Compact)));
    pcv::interface::TempScanningInfo missingCreatedAtInfo;
    assert(!pcv::interface::parseTempScanningInfo(missingCreatedAtJson,
                                                   &missingCreatedAtInfo, &error));
    options.scanningInfoPath = missingCreatedAtJson;
    assert(pcv::interface::prepareTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_INPUT_002"));
    options.scanningInfoPath = json;

    // The temporary interface rejects FullXyz and uses the line-profile path only.
    QJsonObject unsupportedFull = inputJson(QStringLiteral("FullXyz"),
                                             QStringLiteral("scan.ply"),
                                             QStringLiteral("calibration.xml"));
    const QString fullJson = QDir(dir).filePath(QStringLiteral("full.json"));
    assert(writeFile(fullJson, QJsonDocument(unsupportedFull).toJson(QJsonDocument::Compact)));
    options.scanningInfoPath = fullJson;
    const auto fullPrepared = pcv::interface::prepareTempWorkpiece(options, &error);
    assert(!fullPrepared.success && fullPrepared.errorCode == QStringLiteral("PCV_INPUT_002"));

    // An XML override is used only for this preparation and does not rewrite JSON.
    const QString overrideXml = QDir(dir).filePath(QStringLiteral("override.xml"));
    assert(writeCalibrationWithTranslation(overrideXml, 50, 0, 0));
    const QByteArray originalJson = readFile(json);
    options.scanningInfoPath = json;
    options.calibrationOverridePath = overrideXml;
    const auto overridden = pcv::interface::prepareTempWorkpiece(options, &error);
    assert(overridden.success);
    assert(overridden.resolvedCalibrationFile == QFileInfo(overrideXml).absoluteFilePath());
    assert(std::abs(overridden.robotBasePoints[0].x - 51.0f) < 1.0e-4f);
    assert(readFile(json) == originalJson);
    options.calibrationOverridePath.clear();
    options.scanningInfoPath = json;

    options.calibrationOverridePath = QDir(dir).filePath(QStringLiteral("missing.xml"));
    const auto missingOverride = pcv::interface::prepareTempWorkpiece(options, &error);
    assert(!missingOverride.success
           && missingOverride.errorCode == QStringLiteral("PCV_INPUT_001"));
    options.calibrationOverridePath.clear();

    QJsonObject line = inputJson(QStringLiteral("LineProfileXz"), QStringLiteral("scan.ply"),
                                  QStringLiteral("calibration.xml"));
    QJsonObject scan = line.value(QStringLiteral("scan")).toObject();
    scan.insert(QStringLiteral("robot_pose_end"), QJsonArray{0, 10, 0, 0, 0, 0});
    line.insert(QStringLiteral("scan"), scan);
    const QString lineJson = QDir(dir).filePath(QStringLiteral("line.json"));
    assert(writeFile(lineJson, QJsonDocument(line).toJson(QJsonDocument::Compact)));
    options.scanningInfoPath = lineJson;
    const auto linePrepared = pcv::interface::prepareTempWorkpiece(options, &error);
    assert(linePrepared.success);
    assert(linePrepared.pointCloudLayout == pointcloud::DepthPointLayout::LineProfileXz);
    assert(linePrepared.robotBasePoints.size() == 4);
    assert(std::abs(linePrepared.robotBasePoints[0].y - 0.0f) < 1.0e-4f);
    assert(std::abs(linePrepared.robotBasePoints[1].y - 5.0f) < 1.0e-4f);
    assert(std::abs(linePrepared.robotBasePoints[2].y - 10.0f) < 1.0e-4f);
    assert(std::abs(linePrepared.robotBasePoints[3].y - 10.0f) < 1.0e-4f);

    QJsonObject invalidLayout = inputJson(QStringLiteral("Unsupported"), QStringLiteral("scan.ply"),
                                          QStringLiteral("calibration.xml"));
    const QString invalidJson = QDir(dir).filePath(QStringLiteral("invalid.json"));
    assert(writeFile(invalidJson, QJsonDocument(invalidLayout).toJson(QJsonDocument::Compact)));
    options.scanningInfoPath = invalidJson;
    assert(pcv::interface::prepareTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_INPUT_002"));

    QJsonObject traversal = inputJson(QStringLiteral("LineProfileXz"), QStringLiteral("../scan.ply"),
                                      QStringLiteral("calibration.xml"));
    const QString traversalJson = QDir(dir).filePath(QStringLiteral("traversal.json"));
    assert(writeFile(traversalJson, QJsonDocument(traversal).toJson(QJsonDocument::Compact)));
    options.scanningInfoPath = traversalJson;
    assert(pcv::interface::prepareTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_CONTRACT_001"));

    pcv::interface::TempWorkpieceFinalizeOptions finalize;
    finalize.outputDirectory = QDir(root.path()).filePath(QStringLiteral("output"));
    finalize.planeIndices = {0, 1, 2, 3};
    finalize.roiIndices = {0, 1, 2, 3};
    finalize.planeMask = QImage(8, 8, QImage::Format_Grayscale8);
    finalize.planeMask.fill(0);
    finalize.planeMask.setPixel(3, 3, 255);
    finalize.planeMask.setPixel(4, 3, 127);
    finalize.planeModel = QVector4D(0, 0, 1, 0);
    finalize.originInRobotBase = QVector3D(1.23456f, -2.3456f, 3.9999f);
    finalize.abcDeg = QVector3D(4.4444f, 5.5564f, 6.6666f);
    finalize.TBaseWorkpiece.setToIdentity();
    finalize.TWorkpieceBase.setToIdentity();
    const auto generated = pcv::interface::finalizeTempWorkpiece(prepared, finalize, &error);
    assert(generated.success);
    assert(generated.createdAtIso8601 == info.createdAtIso8601);
    assert(QFileInfo::exists(generated.baselineRobotBasePly));
    assert(QFileInfo::exists(generated.roiTemplateRobotBasePly));
    assert(QFileInfo::exists(generated.planeMaskPng));
    assert(QFileInfo::exists(generated.tempWorkpieceInfoPath));
    const QImage writtenMask(generated.planeMaskPng);
    assert(writtenMask.format() == QImage::Format_Grayscale8);
    assert(writtenMask.constScanLine(3)[3] == 255);
    assert(writtenMask.constScanLine(3)[4] == 255);
    assert(readFile(generated.baselineRobotBasePly).startsWith(
        QByteArrayLiteral("ply\nformat binary_little_endian 1.0\n")));
    const QByteArray metadataBytes = readFile(generated.tempWorkpieceInfoPath);
    assertFixedEquationText(metadataBytes);
    assert(metadataBytes.contains(QByteArrayLiteral(
        "[\n      1.235,\n      -2.346,\n      4.000,\n      6.667,\n      5.556,\n      4.444\n    ]")));
    const QJsonObject output = QJsonDocument::fromJson(metadataBytes).object();
    assertMinimalWorkpieceJson(output);
    assert(output.value(QStringLiteral("created_at")).toString() == info.createdAtIso8601);
    assert(output.value(QStringLiteral("plane")).toObject().value(QStringLiteral("wobj_num")).toInt() == 1);
    const QJsonArray equation = output.value(QStringLiteral("plane")).toObject()
                                   .value(QStringLiteral("equation")).toArray();
    assert(equation.size() == 6);
    assert(std::abs(equation.at(0).toDouble() - 1.235) < 1.0e-9);
    assert(std::abs(equation.at(1).toDouble() + 2.346) < 1.0e-9);
    assert(std::abs(equation.at(2).toDouble() - 4.000) < 1.0e-9);
    assert(std::abs(equation.at(3).toDouble() - 6.667) < 1.0e-9);
    assert(std::abs(equation.at(4).toDouble() - 5.556) < 1.0e-9);
    assert(std::abs(equation.at(5).toDouble() - 4.444) < 1.0e-9);
    assert(output.value(QStringLiteral("image")).toObject().value(QStringLiteral("pixel_size_mm")).toDouble() == 0.05);
    assert(output.value(QStringLiteral("outputs")).toObject().value(QStringLiteral("plane_mask")).toString()
           == QDir::fromNativeSeparators(QFileInfo(generated.planeMaskPng).absoluteFilePath()));
    assert(output.value(QStringLiteral("outputs")).toObject()
               .value(QStringLiteral("robot_base_point_cloud")).toString()
           == QDir::fromNativeSeparators(QFileInfo(generated.baselineRobotBasePly).absoluteFilePath()));

    finalize.allowOverwrite = false;
    const auto refused = pcv::interface::finalizeTempWorkpiece(prepared, finalize, &error);
    assert(!refused.success && refused.errorCode == QStringLiteral("PCV_OUTPUT_002"));
    std::cout << "temp_workpiece_interface_tests: PASS\n";
    return 0;
}
