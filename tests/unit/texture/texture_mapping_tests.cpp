#include <pcv/texture/texture_mapping.h>

#include <QCoreApplication>

#include <iostream>

namespace {
bool require(bool value, const char *message) {
    if (!value) std::cerr << message << '\n';
    return value;
}

QImage testImage() {
    QImage image(2, 2, QImage::Format_Grayscale8);
    image.scanLine(0)[0] = 10;
    image.scanLine(0)[1] = 20;
    image.scanLine(1)[0] = 30;
    image.scanLine(1)[1] = 40;
    return image;
}

pcv::texture::MappingOptions validOptions() {
    pcv::texture::MappingOptions options;
    options.sourceIndices = {0, 1, 2, 3};
    options.declaredPointCount = 4;
    return options;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QVector<pointcloud::Point3D> points{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};

    const auto direct = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), validOptions());
    if (!require(direct.ok && direct.mappingMode == QStringLiteral("source_raster_index"),
                 "direct raster mapping should succeed")) return 1;
    if (!require(direct.imageWidth == 2 && direct.imageHeight == 2
                 && direct.sourcePointCount == 4 && direct.currentPointCount == 4,
                 "raster dimensions and counts should be reported")) return 1;
    if (!require(direct.mappedPointCount == 4 && direct.skippedPointCount == 0,
                 "all plane points should be mapped")) return 1;
    if (!require(direct.grayscale == QVector<quint8>({10, 20, 30, 40}),
                 "row-major mapping must start at the image top-left")) return 1;

    const auto planeOnly = pcv::texture::mapPlaneBrightness(
        points, {0, 3}, testImage(), validOptions());
    if (!require(planeOnly.ok && planeOnly.mappedPointCount == 2
                 && planeOnly.skippedPointCount == 0
                 && planeOnly.valid == QVector<quint8>({1, 0, 0, 1})
                 && planeOnly.grayscale.at(0) == 10
                 && planeOnly.grayscale.at(3) == 40,
                 "only planeIndices pixels should receive texture")) return 1;

    auto shuffled = validOptions();
    shuffled.sourceIndices = {3, 2, 1, 0};
    const auto sourceIndexed = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), shuffled);
    if (!require(sourceIndexed.ok
                 && sourceIndexed.grayscale == QVector<quint8>({40, 30, 20, 10}),
                 "source index must determine the sampled pixel")) return 1;

    auto mismatch = validOptions();
    mismatch.declaredPointCount = 3;
    const auto mismatchResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), mismatch);
    if (!require(!mismatchResult.ok && mismatchResult.error.contains(QStringLiteral("像素数")),
                 "image and original PLY counts must match")) return 1;

    auto incomplete = validOptions();
    incomplete.sourceIndices.resize(3);
    const auto incompleteResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), incomplete);
    if (!require(!incompleteResult.ok && incompleteResult.error.contains(QStringLiteral("source index")),
                 "missing source indices must be rejected")) return 1;

    auto duplicate = validOptions();
    duplicate.sourceIndices[1] = 0;
    const auto duplicateResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), duplicate);
    if (!require(!duplicateResult.ok && duplicateResult.error.contains(QStringLiteral("重复")),
                 "duplicate source indices must be rejected")) return 1;

    auto outOfRange = validOptions();
    outOfRange.sourceIndices[2] = 9;
    const auto outOfRangeResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), outOfRange);
    if (!require(!outOfRangeResult.ok && outOfRangeResult.error.contains(QStringLiteral("越界")),
                 "out-of-range source indices must be rejected")) return 1;

    auto rejected = validOptions();
    rejected.rejectedInvalidPointCount = 1;
    const auto rejectedResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), rejected);
    if (!require(!rejectedResult.ok && rejectedResult.error.contains(QStringLiteral("过滤")),
                 "filtered source points must reject the mapping")) return 1;

    QImage rgb(2, 2, QImage::Format_ARGB32);
    rgb.setPixel(0, 0, qRgb(255, 0, 0));
    rgb.setPixel(1, 0, qRgb(0, 255, 0));
    rgb.setPixel(0, 1, qRgb(0, 0, 255));
    rgb.setPixel(1, 1, qRgb(255, 255, 255));
    const auto gray = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, rgb, validOptions());
    if (!require(gray.ok && gray.imageWidth == 2 && gray.imageHeight == 2,
                 "RGB image should be converted to Mono8 without resizing")) return 1;

    auto cancelled = validOptions();
    cancelled.isCancelled = [] { return true; };
    const auto cancelledResult = pcv::texture::mapPlaneBrightness(
        points, {0, 1, 2, 3}, testImage(), cancelled);
    if (!require(cancelledResult.cancelled && !cancelledResult.ok,
                 "mapping cancellation must not publish a result")) return 1;

    const auto emptyPlane = pcv::texture::mapPlaneBrightness(
        points, {}, testImage(), validOptions());
    if (!require(!emptyPlane.ok, "empty plane indices must be rejected")) return 1;

    return 0;
}
