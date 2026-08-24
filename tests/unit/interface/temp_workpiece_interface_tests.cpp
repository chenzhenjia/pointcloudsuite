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

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

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
        {QStringLiteral("robot_pose_end"), endPose},
        {QStringLiteral("plane_seed_indices"), seeds}};
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
