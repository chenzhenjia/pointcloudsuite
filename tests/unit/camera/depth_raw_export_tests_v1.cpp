#include "depth_capture_storage_v1.h"
#include <pcv/camera/frame_converter.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

#define CHECK(v) do { if (!(v)) { std::cerr << "line " << __LINE__ << ": " #v "\n"; return 1; } } while (false)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir temp; CHECK(temp.isValid());
    pcv::camera::CameraFrame frame; frame.width = 4; frame.height = 3; frame.valid = true;
    frame.completeScan = true; frame.frameType = pcv::camera::FrameType::DepthC16;
    frame.rawFrameType = frame.frameType; frame.boundarySource = pcv::camera::ScanBoundarySource::SdkImageFrame;
    frame.data.resize(24); frame.intensityData = QByteArray(12, char(50));
    frame.intensityWidth = 4; frame.intensityHeight = 3; frame.sdkReserved = QByteArray(8, char(7));
    for (int i = 0; i < 12; ++i) qToLittleEndian<qint16>(i == 2 ? -32768 : qint16(i - 5), frame.data.data() + i * 2);
    frame.exposureTimeStamps = {10, 20, 30}; frame.encoders = {-3, 0, 3};
    const auto env = pcv::camera::cameraConversionEnvironment();
    CHECK(env.error.isEmpty()); frame.sdkVersion = env.sdkVersion; frame.conversionFingerprint = env.fingerprint;
    const QString calibrationPath = QDir(temp.path()).filePath(QStringLiteral("厂商标定.ini"));
    QFile calibration(calibrationPath); CHECK(calibration.open(QIODevice::WriteOnly));
    CHECK(calibration.write("vendor-calibration") == 18); calibration.close();
    CHECK(QDir(temp.path()).mkdir("frame_00000001"));
    pcv::camera::CaptureFrameRecord record; QString error;
    auto incomplete = frame; incomplete.intensityData.clear();
    CHECK(!pcv::camera::detail::saveDepthFrame(temp.path(), 1, incomplete, true, &record, &error,
                                                calibrationPath, true, "Virtual", "VirTest"));
    CHECK(pcv::camera::detail::saveDepthFrame(temp.path(), 1, frame, true, &record, &error,
                                               calibrationPath, true, "Virtual", "VirTest"));
    const QDir dir(record.directory); QFile raw(dir.filePath("depth.raw")); CHECK(raw.open(QIODevice::ReadOnly));
    CHECK(raw.readAll() == frame.data);
    QFile metaFile(dir.filePath("depth.json")); CHECK(metaFile.open(QIODevice::ReadOnly));
    const auto meta = QJsonDocument::fromJson(metaFile.readAll()).object();
    CHECK(meta.value("schema") == "pcv-depth-raw-frame-v1");
    CHECK(meta.value("raw_type") == "pure_c16" && meta.value("header_bytes").toInt() == 0);
    CHECK(meta.value("raw_sha256").toString()
          == QString::fromLatin1(QCryptographicHash::hash(frame.data, QCryptographicHash::Sha256).toHex()));
    CHECK(meta.value("width").toInt() == 4 && meta.value("height").toInt() == 3);
    CHECK(meta.value("invalid_depth").toObject().value("value").toInt() == -32768);
    CHECK(meta.value("device_serial") == "VirTest" && meta.value("conversion_fingerprint") == env.fingerprint);
    CHECK(meta.value("device_model") == "Virtual");
    CHECK(meta.value("conversion_output_unit") == "mm");
    const auto intensityInfo = meta.value("conversion_intensity").toObject();
    CHECK(intensityInfo.value("sample_type") == "uint8" && intensityInfo.value("count").toInt() == 12);
    const auto exposureInfo = meta.value("exposure").toObject();
    CHECK(exposureInfo.value("sample_type") == "int64" && exposureInfo.value("byte_order") == "little_endian"
          && exposureInfo.value("count").toInt() == 3 && exposureInfo.value("sha256").toString().size() == 64);
    const auto encoderInfo = meta.value("encoder").toObject();
    CHECK(encoderInfo.value("sample_type") == "int32" && encoderInfo.value("count").toInt() == 3);
    const auto calibrationInfo = meta.value("calibration").toObject();
    CHECK(calibrationInfo.value("status") == "user_supplied_unverified");
    CHECK(calibrationInfo.value("file") == "vendor_calibration.ini");
    QFile savedCalibration(dir.filePath(calibrationInfo.value("file").toString()));
    CHECK(savedCalibration.open(QIODevice::ReadOnly) && savedCalibration.readAll() == "vendor-calibration");
    CHECK(QFileInfo(dir.filePath("intensity.png")).exists() == false);
    for (const auto &name : {QStringLiteral("conversion_intensity.mono8"), QStringLiteral("exposure.i64"), QStringLiteral("encoder.i32")})
        CHECK(QFileInfo::exists(dir.filePath(name)));
    std::cout << "pure C16 RAW export metadata and auxiliary sidecars passed\n"; return 0;
}
