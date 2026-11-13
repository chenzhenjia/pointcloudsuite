#include <pcv/io/ply_reader.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/texture/texture_mapping.h>

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

namespace {
bool require(bool value, const char *message) {
    if (!value) std::cerr << message << '\n';
    return value;
}
}

// Application-level contract for the raw-coordinate experiment. It avoids a
// QWidget/OpenGL surface so it remains deterministic in headless CI while
// exercising the reader, arbitrary-plane fit, verification, and mapping
// contracts used by the GUI workflow.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    const QString path = directory.filePath(QStringLiteral("raw_coordinates_v1.ply"));
    QFile ply(path);
    if (!ply.open(QIODevice::WriteOnly)) return 1;
    QByteArray data("ply\nformat ascii 1.0\nelement vertex 20\n"
                    "property float x\nproperty float y\nproperty float z\nend_header\n");
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 4; ++y)
            data += "10 " + QByteArray::number(y * 5) + " " + QByteArray::number(z * 5) + "\n";
    }
    for (int i = 0; i < 4; ++i)
        data += "30 " + QByteArray::number(i * 5) + " 0\n";
    if (ply.write(data) != data.size()) return 1;
    ply.close();

    const auto loaded = pcv::detail::io::readPly(path);
    if (!require(loaded.ok && loaded.declaredPointCount == 20
                 && loaded.points.size() == 20,
                 "raw experiment must preserve the declared count and all points")) return 1;

    QVector<qsizetype> sourceIndices;
    sourceIndices.reserve(loaded.points.size());
    for (qsizetype index = 0; index < loaded.points.size(); ++index)
        sourceIndices.push_back(index);
    if (!require(sourceIndices.size() == loaded.points.size() && sourceIndices.at(7) == 7,
                 "raw experiment must establish identity source indices")) return 1;

    pcv::planefitting::Options fitOptions;
    fitOptions.useZAxisResidual = false;
    fitOptions.initialTolerance = 0.01f;
    fitOptions.surfaceTolerance = 0.01f;
    fitOptions.ransacIterations = 32;
    fitOptions.minInliers = 3;
    fitOptions.keepSeedComponentOnly = false;
    fitOptions.deferFinalClassification = true;
    const auto firstPlane = pcv::planefitting::fit(loaded.points, {0, 3, 12}, fitOptions);
    if (!require(firstPlane.ok && firstPlane.planeIndices.size() == 16
                 && std::abs(firstPlane.model.a) > 0.9f,
                 "raw experiment must fit an arbitrary vertical plane by perpendicular distance")) return 1;

    const auto verification = pcv::planefitting::validateConsistency(
        loaded.points, firstPlane.model, {0, 3, 12}, {5, 6, 9}, 1.0f, 0.4f);
    if (!require(verification.status == pcv::planefitting::ConsistencyStatus::Passed,
                 "the second triplet must pass same-plane verification")) return 1;

    QImage image(5, 4, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int value = y * 5 + x + 10;
            image.setPixel(x, y, qRgb(value, value, value));
        }
    }
    pcv::texture::MappingOptions options;
    options.declaredPointCount = loaded.declaredPointCount;
    options.sourceIndices = sourceIndices;
    const auto mapped = pcv::texture::mapPlaneBrightness(
        loaded.points, firstPlane.planeIndices, image, options);
    if (!require(mapped.ok && mapped.mappedPointCount == 16
                 && mapped.imageWidth == 5 && mapped.imageHeight == 4,
                 "verified plane must map the original row-major raster")) return 1;
    for (int i = 0; i < loaded.points.size(); ++i) {
        if (!require(mapped.valid.at(i) == (i < 16 ? 1 : 0),
                     "only plane indices may receive texture")) return 1;
        if (i < 16 && !require(mapped.grayscale.at(i) == quint8(i + 10),
                               "source index must select the matching pixel")) return 1;
    }

    options.isCancelled = [] { return true; };
    const auto cancelled = pcv::texture::mapPlaneBrightness(
        loaded.points, firstPlane.planeIndices, image, options);
    if (!require(cancelled.cancelled && !cancelled.ok
                 && cancelled.mappedPointCount == 0
                 && cancelled.valid.size() == loaded.points.size(),
                 "cancelled mapping must not publish a partial result")) return 1;

    options.isCancelled = {};
    options.declaredPointCount = 19;
    const auto rejected = pcv::texture::mapPlaneBrightness(
        loaded.points, firstPlane.planeIndices, image, options);
    if (!require(!rejected.ok && rejected.mappedPointCount == 0
                 && rejected.valid.size() == loaded.points.size(),
                 "a raster size mismatch must reject the whole mapping")) return 1;
    return 0;
}
