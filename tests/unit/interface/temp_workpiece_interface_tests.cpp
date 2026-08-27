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

#if 0
namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
#endif

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

#if 0
QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeAsciiPly(const QString &path, const QByteArray &vertices, int vertexCount)
{
    const QByteArray header = QByteArrayLiteral(
        "ply\nformat ascii 1.0\n"
        "element vertex ") + QByteArray::number(vertexCount) + QByteArrayLiteral(
        "\nproperty float x\nproperty float y\nproperty float z\n"
        "end_header\n");
    return writeFile(path, header + vertices);
}

bool writeAsciiPly(const QString &path)
{
    return writeAsciiPly(path, QByteArrayLiteral(
        "1 0 0\n1 10 0\n2 10 0\n2 20 0\n5 5 2\n"), 5);
}

bool writeCalibration(const QString &path)
{
    return writeFile(path, QByteArrayLiteral(
        "<ArithConfig><RTmatDepth2robot>"
        "<RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/>"
        "<TVec t0=\"0\" t1=\"0\" t2=\"0\"/>"
        "</RTmatDepth2robot></ArithConfig>"));
}

QJsonObject makeInputJson(const QString &plyPath,
                          const QString &xmlPath,
                          const QJsonArray &seeds = QJsonArray{0, 1, 2},
                          const QString &layout = QStringLiteral("FullXyz"),
                          const QJsonArray &startPose = QJsonArray{0, 0, 0, 0, 0, 0},
                          const QJsonArray &endPose = QJsonArray{0, 0, 0, 0, 0, 0})
{
    QJsonObject scan{
        {QStringLiteral("scan_id"), QStringLiteral("scan_001")},
        {QStringLiteral("point_cloud_file"), plyPath},
        {QStringLiteral("coordinate_frame"), QStringLiteral("camera")},
        {QStringLiteral("robot_pose_start"), startPose},
        {QStringLiteral("robot_pose_end"), endPose}};
    if (!seeds.isEmpty()) scan.insert(QStringLiteral("plane_seed_indices"), seeds);
    if (!layout.isEmpty()) scan.insert(QStringLiteral("point_cloud_layout"), layout);
    return QJsonObject{
        {QStringLiteral("schema_version"), QString::fromLatin1(pcv::interface::kTempScanningSchema)},
        {QStringLiteral("kind"), QString::fromLatin1(pcv::interface::kTempScanningKind)},
        {QStringLiteral("scan"), scan},
        {QStringLiteral("calibration"), QJsonObject{
            {QStringLiteral("calibration_file"), xmlPath},
            {QStringLiteral("source_frame"), QStringLiteral("camera")},
            {QStringLiteral("target_frame"), QStringLiteral("robot_base")}}}
    };
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    return writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

struct OutputPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    quint32 sourceIndex = 0;
};

bool readBaselinePly(const QString &path, QVector<OutputPoint> *points, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QByteArray headerEnd = QByteArrayLiteral("end_header\n");
    const qsizetype headerOffset = bytes.indexOf(headerEnd);
    if (headerOffset < 0 || !bytes.startsWith(QByteArrayLiteral(
            "ply\nformat binary_little_endian 1.0\n"))) {
        if (error) *error = QStringLiteral("invalid binary little-endian PLY header");
        return false;
    }
    const QByteArray header = bytes.left(headerOffset + headerEnd.size());
    const QByteArray vertexMarker = QByteArrayLiteral("element vertex ");
    const qsizetype vertexOffset = header.indexOf(vertexMarker);
    const qsizetype countEnd = header.indexOf('\n', vertexOffset);
    if (vertexOffset < 0 || countEnd < 0) {
        if (error) *error = QStringLiteral("missing vertex count");
        return false;
    }
    bool countOk = false;
    const int vertexCount = header.mid(vertexOffset + vertexMarker.size(),
                                       countEnd - vertexOffset - vertexMarker.size())
        .trimmed().toInt(&countOk);
    if (!countOk || vertexCount < 0) {
        if (error) *error = QStringLiteral("invalid vertex count");
        return false;
    }

    QByteArray payload = bytes.mid(headerOffset + headerEnd.size());
    QBuffer buffer(&payload);
    if (!buffer.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("failed to open PLY payload");
        return false;
    }
    QDataStream stream(&buffer);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    QVector<OutputPoint> parsed;
    parsed.reserve(vertexCount);
    for (int i = 0; i < vertexCount; ++i) {
        OutputPoint point;
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        stream >> point.x >> point.y >> point.z >> nx >> ny >> nz >> point.sourceIndex;
        if (stream.status() != QDataStream::Ok) {
            if (error) *error = QStringLiteral("truncated PLY payload");
            return false;
        }
        parsed.push_back(point);
    }
    if (points) *points = parsed;
    return true;
}

bool near(double actual, double expected, double tolerance = 1.0e-4)
{
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir root;
    assert(root.isValid());
    const QString inputDirectory = QDir(root.path()).filePath(QStringLiteral("input"));
    assert(QDir().mkpath(inputDirectory));
    const QString plyPath = QDir(inputDirectory).filePath(QStringLiteral("scan.ply"));
    const QString xmlPath = QDir(inputDirectory).filePath(QStringLiteral("calibration.xml"));
    const QString jsonPath = QDir(inputDirectory).filePath(QStringLiteral("temp_scanning_info.json"));
    assert(writeAsciiPly(plyPath));
    assert(writeCalibration(xmlPath));

    const QJsonObject input = makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"));
    assert(writeJson(jsonPath, input));
    const QByteArray originalJson = fileBytes(jsonPath);

    pcv::interface::TempScanningInfo parsed;
    QString error;
    assert(pcv::interface::parseTempScanningInfo(QFileInfo(jsonPath).absoluteFilePath(), &parsed, &error));
    assert(parsed.valid);
    assert(parsed.pointCloudLayout == pointcloud::DepthPointLayout::FullXyz);
    assert(parsed.planeSeedIndices == QVector<int>({0, 1, 2}));

    // The user-provided shape is accepted with absolute references and with
    // both optional scan fields omitted. Automatic selection uses real points.
    const QString userJsonPath = QDir(inputDirectory).filePath(QStringLiteral("user_shape.json"));
    QJsonObject userInput = makeInputJson(QFileInfo(plyPath).absoluteFilePath(),
                                          QFileInfo(xmlPath).absoluteFilePath(),
                                          QJsonArray{}, QString());
    userInput.insert(QStringLiteral("created_at"), QStringLiteral("2026-08-24T08:30:00+08:00"));
    assert(writeJson(userJsonPath, userInput));
    assert(pcv::interface::parseTempScanningInfo(userJsonPath, &parsed, &error));
    assert(parsed.planeSeedIndices.isEmpty());
    assert(parsed.createdAtIso8601 == QStringLiteral("2026-08-24T08:30:00+08:00"));
    assert(parsed.pointCloudLayout == pointcloud::DepthPointLayout::FullXyz);
    assert(parsed.warning.contains(QStringLiteral("point_cloud_layout missing; defaulted to FullXyz")));
    pcv::interface::TempWorkpieceOptions userOptions;
    userOptions.scanningInfoPath = userJsonPath;
    userOptions.outputDirectory = QStringLiteral("user_shape_result");
    userOptions.minimumPlaneInliers = 4;
    const auto userResult = pcv::interface::generateTempWorkpiece(userOptions, &error);
    if (!userResult.success) {
        assert(userResult.errorCode == QStringLiteral("PCV_PLANE_001"));
    } else {
        assert(userResult.pointCloudLayout == pointcloud::DepthPointLayout::FullXyz);
        assert(userResult.warning.contains(QStringLiteral("point_cloud_layout missing; defaulted to FullXyz")));
        const QJsonObject userOutput = QJsonDocument::fromJson(
            fileBytes(userResult.tempWorkpieceInfoPath)).object();
        assert(!userOutput.value(QStringLiteral("created_at")).toString().isEmpty());
    }

    pcv::interface::TempWorkpieceOptions options;
    options.scanningInfoPath = QFileInfo(jsonPath).absoluteFilePath();
    options.outputDirectory = QStringLiteral("result");
    options.createdAtIso8601 = QStringLiteral("2026-08-24T12:34:56.000+08:00");
    const auto generated = pcv::interface::generateTempWorkpiece(options, &error);
    if (!generated.success) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    assert(generated.pointCloudLayout == pointcloud::DepthPointLayout::FullXyz);
    assert(generated.interfaceDirectory == QDir(inputDirectory).filePath(QStringLiteral("result")));
    const QByteArray outputBytes = fileBytes(generated.tempWorkpieceInfoPath);
    const QList<QByteArray> expectedKeys{
        QByteArrayLiteral("schema_version"),
        QByteArrayLiteral("kind"),
        QByteArrayLiteral("created_at"),
        QByteArrayLiteral("plane"),
        QByteArrayLiteral("image"),
        QByteArrayLiteral("roi"),
        QByteArrayLiteral("outputs")
    };
    int previousKeyOffset = -1;
    for (const QByteArray &key : expectedKeys) {
        const QByteArray marker = QByteArrayLiteral("\n  \"") + key + QByteArrayLiteral("\":");
        const int keyOffset = outputBytes.indexOf(marker);
        assert(keyOffset > previousKeyOffset);
        previousKeyOffset = keyOffset;
    }
    const auto assertNestedOrder = [&outputBytes](const QByteArray &container,
                                                  const QList<QByteArray> &keys) {
        int offset = outputBytes.indexOf(container);
        assert(offset >= 0);
        for (const QByteArray &key : keys) {
            offset = outputBytes.indexOf(key, offset + 1);
            assert(offset >= 0);
        }
    };
    assertNestedOrder(QByteArrayLiteral("\n  \"plane\": {"), {
        QByteArrayLiteral("\n    \"name\":"),
        QByteArrayLiteral("\n    \"equation\":")});
    assertNestedOrder(QByteArrayLiteral("\n  \"image\": {"), {
        QByteArrayLiteral("\n    \"name\":"),
        QByteArrayLiteral("\n    \"width_px\":"),
        QByteArrayLiteral("\n    \"height_px\":"),
        QByteArrayLiteral("\n    \"width_mm\":"),
        QByteArrayLiteral("\n    \"height_mm\":"),
        QByteArrayLiteral("\n    \"pixel_size_mm\":")});
    assertNestedOrder(QByteArrayLiteral("\n  \"outputs\": {"), {
        QByteArrayLiteral("\n    \"robot_base_point_cloud\":"),
        QByteArrayLiteral("\n    \"roi_point_cloud\":"),
        QByteArrayLiteral("\n    \"plane_mask\":")});
    const QJsonObject output = QJsonDocument::fromJson(outputBytes).object();
    assert(output.value(QStringLiteral("schema_version")).toString()
           == QString::fromLatin1(pcv::interface::kTempWorkpieceSchema));
    assert(output.value(QStringLiteral("kind")).toString()
           == QString::fromLatin1(pcv::interface::kTempWorkpieceKind));
    const QStringList outputKeys = output.keys();
    const QStringList expectedOutputKeys{
        QStringLiteral("created_at"),
        QStringLiteral("image"),
        QStringLiteral("kind"),
        QStringLiteral("outputs"),
        QStringLiteral("plane"),
        QStringLiteral("roi"),
        QStringLiteral("schema_version")
    };
    assert(outputKeys == expectedOutputKeys);
    assert(!output.contains(QStringLiteral("point_cloud_layout")));
    const QJsonObject image = output.value(QStringLiteral("image")).toObject();
    const QStringList expectedImageKeys{
        QStringLiteral("height_mm"), QStringLiteral("height_px"), QStringLiteral("name"),
        QStringLiteral("pixel_size_mm"), QStringLiteral("width_mm"), QStringLiteral("width_px")};
    assert(image.keys() == expectedImageKeys);
    assert(image.value(QStringLiteral("name")).toString() == QStringLiteral("plane_mask.png"));
    const double pixelSize = image.value(QStringLiteral("pixel_size_mm")).toDouble();
    assert(pixelSize > 0.0);
    assert(near(image.value(QStringLiteral("width_mm")).toDouble(),
                image.value(QStringLiteral("width_px")).toInt() * pixelSize));
    assert(near(image.value(QStringLiteral("height_mm")).toDouble(),
                image.value(QStringLiteral("height_px")).toInt() * pixelSize));
    assert(output.value(QStringLiteral("plane")).toObject()
               .value(QStringLiteral("name")).toString() == QString::fromLatin1(pcv::interface::kTempPlaneName));
    assert(output.value(QStringLiteral("plane")).toObject()
               .value(QStringLiteral("equation")).toArray().size() == 6);
    assert(output.value(QStringLiteral("roi")).toString() == QStringLiteral("rectangle"));
    const QJsonObject outputs = output.value(QStringLiteral("outputs")).toObject();
    assert(outputs.value(QStringLiteral("plane_mask")).toString()
           == QDir::fromNativeSeparators(QFileInfo(generated.planeMaskPng).absoluteFilePath()));
    assert(outputs.value(QStringLiteral("robot_base_point_cloud")).toString()
           == QDir::fromNativeSeparators(QFileInfo(generated.baselineRobotBasePly).absoluteFilePath()));
    assert(outputs.value(QStringLiteral("roi_point_cloud")).toString()
           == QDir::fromNativeSeparators(QFileInfo(generated.roiTemplateRobotBasePly).absoluteFilePath()));
    assert(fileBytes(jsonPath) == originalJson);

    assert(generated.sourceFrame == QStringLiteral("camera"));
    assert(generated.targetFrame == QStringLiteral("robot_base"));
    assert(generated.robotPoseStart.x == 0.0);
    assert(generated.robotPoseEnd.x == 0.0);

    QVector<OutputPoint> baseline;
    assert(readBaselinePly(generated.baselineRobotBasePly, &baseline, &error));
    assert(baseline.size() == 5);
    assert(near(baseline[0].x, 1.0) && near(baseline[0].y, 0.0)
           && baseline[0].sourceIndex == 0);
    assert(near(baseline[4].x, 5.0) && near(baseline[4].y, 5.0)
           && baseline[4].sourceIndex == 4);

    // FullXyz uses source order for pose interpolation. The second point is
    // at source ratio 1/4 and therefore receives 2.5 mm of robot travel.
    const QString fullTransformPath = QDir(inputDirectory).filePath(QStringLiteral("full_transform.json"));
    assert(writeJson(fullTransformPath, makeInputJson(
        QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"), QJsonArray{0, 1, 2},
        QStringLiteral("FullXyz"), QJsonArray{100, 0, 0, 0, 0, 0},
        QJsonArray{100, 10, 0, 0, 0, 0})));
    pcv::interface::TempWorkpieceOptions fullOptions;
    fullOptions.scanningInfoPath = fullTransformPath;
    fullOptions.outputDirectory = QStringLiteral("full_transform_result");
    fullOptions.minimumPlaneInliers = 4;
    const auto fullTransform = pcv::interface::generateTempWorkpiece(fullOptions, &error);
    assert(fullTransform.success);
    assert(near(fullTransform.robotPoseStart.x, 100.0));
    assert(near(fullTransform.robotPoseEnd.y, 10.0));
    assert(readBaselinePly(fullTransform.baselineRobotBasePly, &baseline, &error));
    assert(baseline.size() == 5);
    assert(near(baseline[0].x, 101.0) && near(baseline[0].y, 0.0));
    assert(near(baseline[1].x, 101.0) && near(baseline[1].y, 12.5));
    assert(near(baseline[1].z, 0.0) && baseline[1].sourceIndex == 1);

    // LineProfileXz uses input Y as scan position and removes it before
    // applying the interpolated robot pose.
    const QString linePlyPath = QDir(inputDirectory).filePath(QStringLiteral("line_scan.ply"));
    assert(writeAsciiPly(linePlyPath, QByteArrayLiteral(
        "1 0 3\n1 5 3\n2 10 3\n1 11 3\n0 0 0\n"), 5));
    const QString lineJsonPath = QDir(inputDirectory).filePath(QStringLiteral("line_scan.json"));
    assert(writeJson(lineJsonPath, makeInputJson(
        QStringLiteral("line_scan.ply"), QStringLiteral("calibration.xml"), QJsonArray{0, 1, 2},
        QStringLiteral("LineProfileXz"), QJsonArray{100, 0, 0, 0, 0, 0},
        QJsonArray{100, 10, 0, 0, 0, 0})));
    pcv::interface::TempWorkpieceOptions lineOptions;
    lineOptions.scanningInfoPath = lineJsonPath;
    lineOptions.outputDirectory = QStringLiteral("line_scan_result");
    lineOptions.minimumPlaneInliers = 3;
    const auto lineTransform = pcv::interface::generateTempWorkpiece(lineOptions, &error);
    assert(lineTransform.success);
    assert(lineTransform.convertedPointCount == 3);
    assert(lineTransform.rejectedInvalidPointCount == 1);
    assert(lineTransform.rejectedRangePointCount == 1);
    assert(readBaselinePly(lineTransform.baselineRobotBasePly, &baseline, &error));
    assert(baseline.size() == 3);
    assert(near(baseline[0].x, 101.0) && near(baseline[0].y, 0.0)
           && near(baseline[0].z, 3.0) && baseline[0].sourceIndex == 0);
    assert(near(baseline[1].x, 101.0) && near(baseline[1].y, 5.0)
           && near(baseline[1].z, 3.0) && baseline[1].sourceIndex == 1);
    assert(near(baseline[2].x, 102.0) && near(baseline[2].y, 10.0)
           && near(baseline[2].z, 3.0) && baseline[2].sourceIndex == 2);

    // The same input and parameters produce byte-identical converted output.
    const QString repeatDirectory = QDir(inputDirectory).filePath(QStringLiteral("repeat_result"));
    pcv::interface::TempWorkpieceOptions repeatOptions = fullOptions;
    repeatOptions.outputDirectory = repeatDirectory;
    repeatOptions.createdAtIso8601 = QStringLiteral("2026-08-24T12:34:56.000+08:00");
    const auto repeatFirst = pcv::interface::generateTempWorkpiece(repeatOptions, &error);
    assert(repeatFirst.success);
    const QByteArray firstBaseline = fileBytes(repeatFirst.baselineRobotBasePly);
    const auto repeatSecond = pcv::interface::generateTempWorkpiece(repeatOptions, &error);
    assert(repeatSecond.success);
    assert(fileBytes(repeatSecond.baselineRobotBasePly) == firstBaseline);

    // Absolute PLY and XML references are allowed.
    const QString absoluteReferencesPath = QDir(inputDirectory).filePath(QStringLiteral("absolute_references.json"));
    assert(writeJson(absoluteReferencesPath, makeInputJson(QFileInfo(plyPath).absoluteFilePath(),
                                                           QFileInfo(xmlPath).absoluteFilePath())));
    assert(pcv::interface::parseTempScanningInfo(absoluteReferencesPath, &parsed, &error));

    // Source created_at is preserved; a missing value is tolerated with a warning,
    // while an invalid value is rejected instead of being replaced silently.
    assert(parsed.createdAtIso8601.isEmpty());
    assert(parsed.warning.contains(QStringLiteral("created_at missing")));
    QJsonObject validCreatedAt = input;
    validCreatedAt.insert(QStringLiteral("created_at"), QStringLiteral("2026-08-24T08:30:00.000+08:00"));
    const QString validCreatedAtPath = QDir(inputDirectory).filePath(QStringLiteral("valid_created_at.json"));
    assert(writeJson(validCreatedAtPath, validCreatedAt));
    assert(pcv::interface::parseTempScanningInfo(validCreatedAtPath, &parsed, &error));
    assert(parsed.createdAtIso8601 == QStringLiteral("2026-08-24T08:30:00.000+08:00"));
    QJsonObject invalidCreatedAt = validCreatedAt;
    invalidCreatedAt.insert(QStringLiteral("created_at"), QStringLiteral("not-a-time"));
    const QString invalidCreatedAtPath = QDir(inputDirectory).filePath(QStringLiteral("invalid_created_at.json"));
    assert(writeJson(invalidCreatedAtPath, invalidCreatedAt));
    assert(!pcv::interface::parseTempScanningInfo(invalidCreatedAtPath, &parsed, &error));
    options.scanningInfoPath = invalidCreatedAtPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_CONTRACT_001"));

    // Missing layout remains compatible and reports the selected default.
    const QString missingLayoutPath = QDir(inputDirectory).filePath(QStringLiteral("missing_layout.json"));
    assert(writeJson(missingLayoutPath, makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"),
                                                       QJsonArray{0, 1, 2}, QString())));
    assert(pcv::interface::parseTempScanningInfo(missingLayoutPath, &parsed, &error));
    assert(parsed.pointCloudLayout == pointcloud::DepthPointLayout::FullXyz);
    assert(!parsed.layoutWarning.isEmpty());

    // LineProfileXz is accepted by the input contract.
    const QString lineLayoutPath = QDir(inputDirectory).filePath(QStringLiteral("line_layout.json"));
    assert(writeJson(lineLayoutPath, makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"),
                                                    QJsonArray{0, 1, 2}, QStringLiteral("LineProfileXz"))));
    assert(pcv::interface::parseTempScanningInfo(lineLayoutPath, &parsed, &error));
    assert(parsed.pointCloudLayout == pointcloud::DepthPointLayout::LineProfileXz);

    const QString invalidLayoutPath = QDir(inputDirectory).filePath(QStringLiteral("invalid_layout.json"));
    assert(writeJson(invalidLayoutPath, makeInputJson(QStringLiteral("scan.ply"),
                                                       QStringLiteral("calibration.xml"),
                                                       QJsonArray{0, 1, 2},
                                                       QStringLiteral("unknown_layout"))));
    assert(!pcv::interface::parseTempScanningInfo(invalidLayoutPath, &parsed, &error));
    options.scanningInfoPath = invalidLayoutPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_INPUT_002"));

    const QString nullLayoutPath = QDir(inputDirectory).filePath(QStringLiteral("null_layout.json"));
    QJsonObject nullLayout = input;
    QJsonObject nullLayoutScan = nullLayout.value(QStringLiteral("scan")).toObject();
    nullLayoutScan.insert(QStringLiteral("point_cloud_layout"), QJsonValue(QJsonValue::Null));
    nullLayout.insert(QStringLiteral("scan"), nullLayoutScan);
    assert(writeJson(nullLayoutPath, nullLayout));
    assert(!pcv::interface::parseTempScanningInfo(nullLayoutPath, &parsed, &error));
    options.scanningInfoPath = nullLayoutPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode
           == QStringLiteral("PCV_INPUT_002"));

    // Automatic selection fails with an all-collinear real point cloud.
    const QString collinearPlyPath = QDir(inputDirectory).filePath(QStringLiteral("collinear.ply"));
    assert(writeAsciiPly(collinearPlyPath, QByteArrayLiteral("0 0 0\n1 0 0\n2 0 0\n"), 3));
    const QString collinearJsonPath = QDir(inputDirectory).filePath(QStringLiteral("collinear.json"));
    assert(writeJson(collinearJsonPath, makeInputJson(QStringLiteral("collinear.ply"),
                                                       QStringLiteral("calibration.xml"),
                                                       QJsonArray{}, QString())));
    options.scanningInfoPath = collinearJsonPath;
    const auto collinearResult = pcv::interface::generateTempWorkpiece(options, &error);
    assert(!collinearResult.success);
    assert(collinearResult.errorCode == QStringLiteral("PCV_PLANE_001"));

    // Explicit collinear seeds retain the existing plane error behavior.
    const QString explicitCollinearJsonPath = QDir(inputDirectory).filePath(QStringLiteral("explicit_collinear.json"));
    assert(writeJson(explicitCollinearJsonPath, makeInputJson(QStringLiteral("collinear.ply"),
                                                               QStringLiteral("calibration.xml"),
                                                               QJsonArray{0, 1, 2})));
    options.scanningInfoPath = explicitCollinearJsonPath;
    const auto explicitCollinearResult = pcv::interface::generateTempWorkpiece(options, &error);
    assert(!explicitCollinearResult.success);
    assert(explicitCollinearResult.errorCode == QStringLiteral("PCV_PLANE_001"));

    // Two equally supported real planes are ambiguous and must not select one
    // arbitrarily.
    const QString ambiguousPlyPath = QDir(inputDirectory).filePath(QStringLiteral("ambiguous.ply"));
    assert(writeAsciiPly(ambiguousPlyPath, QByteArrayLiteral(
        "0 0 0\n10 0 0\n0 10 0\n10 10 0\n"
        "0 0 10\n10 0 10\n0 10 10\n10 10 10\n"), 8));
    const QString ambiguousJsonPath = QDir(inputDirectory).filePath(QStringLiteral("ambiguous.json"));
    assert(writeJson(ambiguousJsonPath, makeInputJson(QStringLiteral("ambiguous.ply"),
                                                       QStringLiteral("calibration.xml"),
                                                       QJsonArray{}, QString())));
    options.scanningInfoPath = ambiguousJsonPath;
    options.minimumPlaneInliers = 4;
    const auto ambiguousResult = pcv::interface::generateTempWorkpiece(options, &error);
    assert(!ambiguousResult.success);
    assert(ambiguousResult.errorCode == QStringLiteral("PCV_PLANE_001"));

    // Missing required field and invalid seeds are rejected.
    QJsonObject missingField = input;
    QJsonObject missingScan = missingField.value(QStringLiteral("scan")).toObject();
    missingScan.remove(QStringLiteral("scan_id"));
    missingField.insert(QStringLiteral("scan"), missingScan);
    const QString missingFieldPath = QDir(inputDirectory).filePath(QStringLiteral("missing_field.json"));
    assert(writeJson(missingFieldPath, missingField));
    assert(!pcv::interface::parseTempScanningInfo(missingFieldPath, &parsed, &error));
    options.scanningInfoPath = missingFieldPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_CONTRACT_001"));

    const QString invalidSeedsPath = QDir(inputDirectory).filePath(QStringLiteral("invalid_seeds.json"));
    assert(writeJson(invalidSeedsPath, makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"),
                                                      QJsonArray{0, -1, 2})));
    assert(!pcv::interface::parseTempScanningInfo(invalidSeedsPath, &parsed, &error));

    // Relative paths may not escape the JSON directory.
    const QString traversalPath = QDir(inputDirectory).filePath(QStringLiteral("traversal.json"));
    assert(writeJson(traversalPath, makeInputJson(QStringLiteral("../scan.ply"), QStringLiteral("calibration.xml"))));
    assert(!pcv::interface::parseTempScanningInfo(traversalPath, &parsed, &error));
    options.scanningInfoPath = traversalPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_CONTRACT_001"));

    // Missing PLY/XML use the stable missing-input error code.
    const QString missingPlyPath = QDir(inputDirectory).filePath(QStringLiteral("missing_ply.json"));
    assert(writeJson(missingPlyPath, makeInputJson(QStringLiteral("missing.ply"), QStringLiteral("calibration.xml"))));
    options.scanningInfoPath = missingPlyPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_INPUT_001"));

    const QString missingXmlPath = QDir(inputDirectory).filePath(QStringLiteral("missing_xml.json"));
    assert(writeJson(missingXmlPath, makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("missing.xml"))));
    options.scanningInfoPath = missingXmlPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_INPUT_001"));

    // A malformed calibration is a transform failure, not a successful input read.
    const QString badXmlPath = QDir(inputDirectory).filePath(QStringLiteral("bad.xml"));
    assert(writeFile(badXmlPath, QByteArrayLiteral("<bad-calibration/>")));
    const QString badXmlJsonPath = QDir(inputDirectory).filePath(QStringLiteral("bad_xml.json"));
    assert(writeJson(badXmlJsonPath, makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("bad.xml"))));
    options.scanningInfoPath = badXmlJsonPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_TRANSFORM_001"));

    // Malformed pose data is rejected before conversion.
    QJsonObject badPoseJson = input;
    QJsonObject badPoseScan = badPoseJson.value(QStringLiteral("scan")).toObject();
    badPoseScan.insert(QStringLiteral("robot_pose_start"), QJsonArray{0, 0, 0});
    badPoseJson.insert(QStringLiteral("scan"), badPoseScan);
    const QString badPosePath = QDir(inputDirectory).filePath(QStringLiteral("bad_pose.json"));
    assert(writeJson(badPosePath, badPoseJson));
    options.scanningInfoPath = badPosePath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_TRANSFORM_002"));

    // A valid scan layout that yields no converted points cannot report success.
    const QString emptyLinePlyPath = QDir(inputDirectory).filePath(QStringLiteral("empty_line.ply"));
    assert(writeAsciiPly(emptyLinePlyPath, QByteArrayLiteral("1 11 3\n"), 1));
    const QString emptyLineJsonPath = QDir(inputDirectory).filePath(QStringLiteral("empty_line.json"));
    assert(writeJson(emptyLineJsonPath, makeInputJson(
        QStringLiteral("empty_line.ply"), QStringLiteral("calibration.xml"), QJsonArray{0, 1, 2},
        QStringLiteral("LineProfileXz"), QJsonArray{0, 0, 0, 0, 0, 0},
        QJsonArray{0, 10, 0, 0, 0, 0})));
    options.scanningInfoPath = emptyLineJsonPath;
    assert(pcv::interface::generateTempWorkpiece(options, &error).errorCode == QStringLiteral("PCV_TRANSFORM_001"));

    // Empty scanningInfoPath retains the runtime job-directory lookup and defaults output to the JSON directory.
    const QString legacyDirectory = QDir(root.path()).filePath(QStringLiteral("jobs/job_1/interface"));
    assert(QDir().mkpath(legacyDirectory));
    assert(writeAsciiPly(QDir(legacyDirectory).filePath(QStringLiteral("scan.ply"))));
    assert(writeCalibration(QDir(legacyDirectory).filePath(QStringLiteral("calibration.xml"))));
    assert(writeJson(QDir(legacyDirectory).filePath(QStringLiteral("temp_scanning_info.json")),
                     makeInputJson(QStringLiteral("scan.ply"), QStringLiteral("calibration.xml"))));
    options = {};
    options.runtimeRoot = root.path();
    options.jobId = QStringLiteral("job_1");
    assert(pcv::interface::generateTempWorkpiece(options, &error).success);

    std::cout << "temp_workpiece_interface_tests: PASS\n";
    return 0;
}
#endif
