#include <pcv/camera/frame_converter.h>
#include <pcv/io/ply_writer.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDataStream>
#include <pcv/io/artifact_transaction.h>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int inputAt = args.indexOf("--input"), outputAt = args.indexOf("--output");
    if (inputAt < 0 || outputAt < 0 || inputAt + 1 >= args.size() || outputAt + 1 >= args.size()) {
        std::cerr << "usage: depth_raw_to_ply_v1 --input <depth.json> --output <new.ply>\n"; return 2;
    }
    const QString jsonPath = QFileInfo(args.at(inputAt + 1)).absoluteFilePath();
    const QString outputPath = QFileInfo(args.at(outputAt + 1)).absoluteFilePath();
    if (QFileInfo::exists(outputPath)) { std::cerr << "output exists; refusing overwrite\n"; return 2; }
    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly)) { std::cerr << jsonFile.errorString().toStdString(); return 10; }
    const auto meta = QJsonDocument::fromJson(jsonFile.readAll()).object();
    if (meta.value("schema") != "pcv-depth-raw-frame-v1" || meta.value("raw_type") != "pure_c16"
        || meta.value("byte_order") != "little_endian" || meta.value("header_bytes").toInt(-1) != 0) {
        std::cerr << "unsupported or invalid depth JSON\n"; return 10;
    }
    const auto w = meta.value("width").toInt(), h = meta.value("height").toInt();
    const auto samples = qint64(w) * h;
    if (w <= 0 || h < 2 || samples > (256ll * 1024 * 1024 / 12)
        || meta.value("row_stride_bytes").toInt() != w * 2
        || meta.value("data_bytes").toInteger() != samples * 2) {
        std::cerr << "invalid raster metadata\n"; return 10;
    }
    auto readBytes = [&](const QString &name, qint64 expected, const QString &sha, QByteArray *out) {
        QFile f(QDir(QFileInfo(jsonPath).absolutePath()).filePath(name));
        if (!f.open(QIODevice::ReadOnly) || f.size() != expected) return false;
        *out = f.readAll();
        return out->size() == expected
            && QString::fromLatin1(QCryptographicHash::hash(*out, QCryptographicHash::Sha256).toHex()) == sha;
    };
    pcv::camera::CameraFrame frame;
    frame.frameType = pcv::camera::FrameType::DepthC16; frame.width = quint32(w); frame.height = quint32(h);
    frame.valid = meta.value("valid").toBool(); frame.completeScan = true;
    frame.data = QByteArray();
    const auto rawName = meta.value("raw_file").toString();
    if (!readBytes(rawName, samples * 2, meta.value("raw_sha256").toString(), &frame.data)) {
        std::cerr << "RAW missing, truncated, or SHA-256 mismatch\n"; return 10;
    }
    if (!readBytes("conversion_intensity.mono8", samples,
                   meta.value("conversion_intensity_sha256").toString(), &frame.intensityData)) {
        std::cerr << "conversion intensity missing or SHA-256 mismatch\n"; return 10;
    }
    frame.intensityWidth = frame.width; frame.intensityHeight = frame.height;
    const auto scale = meta.value("scale").toObject(), offset = meta.value("offset").toObject();
    frame.xScale = float(scale.value("x").toDouble()); frame.yScale = float(scale.value("y").toDouble()); frame.zScale = float(scale.value("z").toDouble());
    frame.xOffset = qint32(offset.value("x").toInt()); frame.yOffset = qint32(offset.value("y").toInt()); frame.zOffset = qint32(offset.value("z").toInt());
    frame.frameNumber = meta.value("sdk_frame_number").toString().toUInt();
    frame.timeStamp = meta.value("timestamp").toString().toLongLong();
    frame.sdkVersion = meta.value("sdk_version").toString();
    frame.conversionFingerprint = meta.value("conversion_fingerprint").toString();
    frame.sdkReserved = QByteArray::fromHex(meta.value("sdk_reserved").toString().toLatin1());
    const auto config = meta.value("conversion_config").toObject();
    for (const auto &key : {QStringLiteral("exposure_file"), QStringLiteral("encoder_file")}) {
        const auto size = meta.value(key == "exposure_file" ? "exposure_bytes" : "encoder_bytes").toInteger();
        if (size == 0) continue;
        QByteArray bytes;
        const auto name = meta.value(key).toString();
        const auto sha = meta.value(key == "exposure_file" ? "exposure_sha256" : "encoder_sha256").toString();
        if (!readBytes(name, size, sha, &bytes)) { std::cerr << "line metadata invalid\n"; return 10; }
        QDataStream stream(bytes); stream.setByteOrder(QDataStream::LittleEndian);
        if (key == "exposure_file") while (!stream.atEnd()) { qint64 v; stream >> v; frame.exposureTimeStamps.push_back(v); }
        else while (!stream.atEnd()) { qint32 v; stream >> v; frame.encoders.push_back(v); }
    }
    const auto environment = pcv::camera::cameraConversionEnvironment();
    if (!environment.error.isEmpty() || environment.sdkVersion != frame.sdkVersion
        || environment.fingerprint != frame.conversionFingerprint || environment.configuration != config) {
        std::cerr << "SDK version or conversion configuration mismatch\n"; return 20;
    }
    const auto result = pcv::camera::materializeCameraFrame(frame);
    if (!result.ok) { std::cerr << result.error.toStdString(); return 20; }
    QByteArray indices; QDataStream indexStream(&indices, QIODevice::WriteOnly);
    indexStream.setByteOrder(QDataStream::LittleEndian);
    for (const auto index : result.sourceIndices) indexStream << quint64(index);
    pcv::io::ArtifactTransaction tx(QFileInfo(outputPath).absolutePath());
    const QString fileName = QFileInfo(outputPath).fileName();
    if (indexStream.status() != QDataStream::Ok || !tx.stageBinaryPly(fileName, result.points)
        || !tx.stageBytes(fileName + ".source_indices.u64", indices) || !tx.commit()) {
        std::cerr << tx.error().toStdString(); return 30;
    }
    std::cout << "camera/mm points=" << result.points.size() << " skipped=" << result.skipped
              << " source_indices=" << fileName.toStdString() << ".source_indices.u64\n";
    return 0;
}
