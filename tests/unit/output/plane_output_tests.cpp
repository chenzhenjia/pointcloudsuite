#include <pcv/output/plane_output.h>

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    pcv::output::JobContext context{temporary.path(), QStringLiteral("job-a"),
                                    QStringLiteral("workpiece-a"), QStringLiteral("plane-a")};
    assert(pcv::output::validateJobContext(context));
    assert(!pcv::output::validateJobContext({temporary.path(), QString(), "w", "b"}));
    assert(!pcv::output::validateJobContext({temporary.path(), "job", "w", "../bad"}));
    assert(!pcv::output::validateJobContext({temporary.path(), "job", "w", "C:/bad"}));

    QImage image(3, 2, QImage::Format_Grayscale8);
    image.fill(0);
    image.setPixel(1, 1, 225);
    QVector<pointcloud::Point3D> points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    pcv::output::PlaneOutputMetadata metadata;
    metadata.sourcePointCloud = QStringLiteral("input.ply");
    metadata.TBaseWorkpiece.setToIdentity();
    metadata.TWorkpieceBase.setToIdentity();
    const auto result = pcv::output::writePlaneOutput(context, image, points, {0, 1, 2}, metadata);
    assert(result.success);
    assert(result.planePng == QStringLiteral("point_cloud/plane/plane-a.png"));
    assert(result.planeJson == QStringLiteral("point_cloud/plane/plane-a.json"));
    assert(result.planeRobotBasePly == QStringLiteral("point_cloud/plane/plane-a_plane_robot_base.ply"));
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("jobs/job-a"));
    QImage written(QDir(root).filePath(result.planePng));
    assert(written.format() == QImage::Format_Grayscale8);
    for (int y = 0; y < written.height(); ++y)
        for (int x = 0; x < written.width(); ++x)
            assert(written.constScanLine(y)[x] == 0 || written.constScanLine(y)[x] == 255);
    QFile jsonFile(QDir(root).filePath(result.planeJson));
    assert(jsonFile.open(QIODevice::ReadOnly));
    const QJsonObject json = QJsonDocument::fromJson(jsonFile.readAll()).object();
    assert(json.value("schema").toString() == QString::fromLatin1(pcv::output::kPlaneOutputSchema));
    assert(json.value("image").toObject().value("pixel_size_mm").toDouble() == 0.05);
    assert(json.value("status").toObject().value("success").toBool());
    QFile ply(QDir(root).filePath(result.planeRobotBasePly));
    assert(ply.open(QIODevice::ReadOnly));
    const QByteArray header = ply.read(512);
    assert(header.contains("format binary_little_endian 1.0"));
    assert(header.contains("comment target_frame robot_base"));

    auto singularMetadata = metadata;
    singularMetadata.TBaseWorkpiece.fill(0.0f);
    auto singularContext = context;
    singularContext.destinationDirectory = QDir(temporary.path()).filePath(QStringLiteral("singular"));
    const auto singular = pcv::output::writePlaneOutput(
        singularContext, image, points, {0, 1, 2}, singularMetadata);
    assert(!singular.success);
    assert(singular.errorCode == QStringLiteral("PCV_FRAME_001"));
    assert(QDir(singularContext.destinationDirectory).entryList(QDir::Files).isEmpty());

    const auto invalidIndices = pcv::output::writePlaneOutput(
        singularContext, image, points, {0, 1, 99}, metadata);
    assert(!invalidIndices.success);
    assert(invalidIndices.errorCode == QStringLiteral("PCV_PLANE_001"));
    assert(QDir(singularContext.destinationDirectory).entryList(QDir::Files).isEmpty());

    // A UI-selected destination must be honored instead of being redirected
    // to the default runtime_data/jobs tree.
    const QString selectedDirectory = QDir(temporary.path()).filePath(QStringLiteral("selected"));
    auto selectedContext = context;
    selectedContext.baseName = QStringLiteral("selected-plane");
    selectedContext.destinationDirectory = selectedDirectory;
    const auto selected = pcv::output::writePlaneOutput(
        selectedContext, image, points, {0, 1, 2}, metadata);
    assert(selected.success);
    assert(QFileInfo::exists(QDir(selectedDirectory).filePath(selected.planePng)));
    assert(QFileInfo::exists(QDir(selectedDirectory).filePath(selected.planeJson)));
    assert(QFileInfo::exists(QDir(selectedDirectory).filePath(selected.planeRobotBasePly)));
    assert(selected.planePng == QStringLiteral("selected-plane.png"));
    std::cout << "plane_output_tests: PASS\n";
    return 0;
}
