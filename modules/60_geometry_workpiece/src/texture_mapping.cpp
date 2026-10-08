#include <pcv/texture/texture_mapping.h>

#include <QImage>

#include <cmath>

namespace pcv::texture {
namespace {

MappingResult failure(MappingResult result, const QString &message) {
    result.error = message;
    result.ok = false;
    result.grayscale.fill(0);
    result.valid.fill(0);
    result.mappedPointCount = 0;
    return result;
}

bool finitePoint(const pointcloud::Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y)
        && std::isfinite(point.z);
}

} // namespace

MappingResult mapPlaneBrightness(const QVector<pointcloud::Point3D> &points,
                                 const QVector<int> &planeIndices,
                                 const QImage &image,
                                 const MappingOptions &options) {
    MappingResult result;
    result.currentPointCount = points.size();
    result.sourcePointCount = options.declaredPointCount;
    result.planePointCount = planeIndices.size();
    result.grayscale.fill(0, points.size());
    result.valid.fill(0, points.size());
    result.mappingMode = QStringLiteral("source_raster_index");

    if (options.isCancelled && options.isCancelled()) {
        result.cancelled = true;
        return failure(result, QStringLiteral("亮度图映射已取消"));
    }
    if (points.isEmpty() || planeIndices.isEmpty())
        return failure(result, QStringLiteral("当前点云或拟合平面为空"));
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return failure(result, QStringLiteral("亮度图为空或尺寸无效"));
    if (options.declaredPointCount <= 0)
        return failure(result, QStringLiteral("原始 PLY 点数无效，无法建立亮度栅格对应关系"));
    if (options.sourceIndices.size() != points.size())
        return failure(result, QStringLiteral("当前点云缺少完整的原始 PLY source index"));
    if (options.rejectedInvalidPointCount > 0 || options.rejectedRangePointCount > 0) {
        return failure(result, QStringLiteral(
            "原始 PLY 在坐标转换中存在被过滤点，无法保证亮度图一一对应"));
    }

    const qint64 pixelCount = qint64(image.width()) * qint64(image.height());
    if (pixelCount <= 0 || pixelCount != qint64(options.declaredPointCount)) {
        return failure(result, QStringLiteral(
            "亮度图像素数与原始 PLY 点数不一致：图像 %1×%2=%3，PLY=%4")
            .arg(image.width()).arg(image.height()).arg(pixelCount)
            .arg(options.declaredPointCount));
    }
    if (points.size() != options.declaredPointCount) {
        return failure(result, QStringLiteral(
            "当前点云数量已改变，无法保持原始亮度栅格对应关系"));
    }

    QVector<quint8> seen;
    seen.fill(0, int(options.declaredPointCount));
    for (qsizetype pointIndex = 0; pointIndex < options.sourceIndices.size(); ++pointIndex) {
        if ((pointIndex % 150000) == 0 && options.isCancelled && options.isCancelled()) {
            result.cancelled = true;
            return failure(result, QStringLiteral("亮度图映射已取消"));
        }
        const qsizetype sourceIndex = options.sourceIndices.at(pointIndex);
        if (sourceIndex < 0 || sourceIndex >= options.declaredPointCount)
            return failure(result, QStringLiteral("原始 PLY source index 越界"));
        if (seen.at(int(sourceIndex)) != 0)
            return failure(result, QStringLiteral("原始 PLY source index 重复，无法建立一一对应"));
        if (!finitePoint(points.at(pointIndex)))
            return failure(result, QStringLiteral("当前点云包含无效坐标，无法建立亮度栅格对应关系"));
        seen[int(sourceIndex)] = 1;
    }

    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull())
        return failure(result, QStringLiteral("亮度图无法转换为 Mono8"));
    result.imageWidth = gray.width();
    result.imageHeight = gray.height();

    for (int planePosition = 0; planePosition < planeIndices.size(); ++planePosition) {
        if ((planePosition % 150000) == 0 && options.isCancelled && options.isCancelled()) {
            result.cancelled = true;
            return failure(result, QStringLiteral("亮度图映射已取消"));
        }
        const int currentIndex = planeIndices.at(planePosition);
        if (currentIndex < 0 || currentIndex >= points.size())
            return failure(result, QStringLiteral("拟合平面索引超出当前点云范围"));
        const qsizetype sourceIndex = options.sourceIndices.at(currentIndex);
        const int pixelX = int(sourceIndex % gray.width());
        const int pixelY = int(sourceIndex / gray.width());
        result.grayscale[currentIndex] = gray.constScanLine(pixelY)[pixelX];
        result.valid[currentIndex] = 1;
        ++result.mappedPointCount;
    }

    result.skippedPointCount = result.planePointCount - result.mappedPointCount;

    result.ok = result.mappedPointCount > 0;
    if (!result.ok)
        result.error = QStringLiteral("没有有效平面点可映射到亮度图");
    return result;
}

} // namespace pcv::texture
