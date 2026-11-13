#include <pcv/io/artifact_transaction.h>
#include <pcv/io/ply_reader.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main()
{
    QTemporaryDir root;
    if (!expect(root.isValid(), "temporary root")) return 1;
    QVector<pointcloud::Point3D> points(1);
    points[0].x = 1.0f; points[0].y = 2.0f; points[0].z = 3.0f;
    QImage image(2, 1, QImage::Format_Grayscale8);
    image.setPixel(0, 0, 0); image.setPixel(1, 0, 127);
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("schema"), QStringLiteral("test-v1")}}).toJson();

    pcv::io::ArtifactTransaction transaction(root.path());
    if (!expect(transaction.stageBinaryPly(QStringLiteral("cloud.ply"), points), qPrintable(QStringLiteral("stage ply: %1").arg(transaction.error())))
        || !expect(transaction.stageGrayscalePng(QStringLiteral("mask.png"), image), "stage png")
        || !expect(transaction.stageJson(QStringLiteral("meta.json"), json), "stage json")
        || !expect(transaction.commit(), "commit")) return 1;
    if (!expect(QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("cloud.ply"))), "ply exists")
        || !expect(QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("mask.png"))), "png exists")
        || !expect(QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("meta.json"))), "json exists")) return 1;

    pcv::io::ArtifactTransaction noOverwrite(root.path());
    if (!expect(noOverwrite.stageJson(QStringLiteral("meta.json"), QByteArrayLiteral("{}")), "stage existing")
        || !expect(!noOverwrite.commit(), "overwrite rejected")) return 1;

    pcv::io::ArtifactTransaction traversal(root.path());
    if (!expect(!traversal.stageJson(QStringLiteral("../escape.json"), QByteArrayLiteral("{}")), "traversal rejected")) return 1;

    bool cancelled = true;
    pcv::io::ArtifactTransaction cancellation(root.path(), true, [&cancelled] { return cancelled; });
    if (!expect(!cancellation.stageJson(QStringLiteral("cancelled.json"), QByteArrayLiteral("{}")), "cancel rejected")
        || !expect(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("cancelled.json"))), "cancel no output")) return 1;
    return 0;
}
