#include "pointcloudprocessor.h"

#include <QVector3D>

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool rasterizesCompleteEdgeMask()
{
    QVector<pointcloud::Point3D> points;
    QVector<int> indices;
    for (int y = 0; y <= 4; ++y) {
        for (int x = 0; x <= 4; ++x) {
            indices.push_back(points.size());
            points.push_back({float(x), float(y), 0.0f});
        }
    }
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, 0.0f};
    pointcloud::PlaneEdgeOptions edgeOptions;
    edgeOptions.edgeGridSize = 1.0f;
    edgeOptions.morphologyCloseRadius = 0;
    edgeOptions.morphologyOpenRadius = 0;
    edgeOptions.maximumEdgeGridCells = 10000;
    const auto edge = pointcloud::segmentPlaneEdges(points, indices, plane, edgeOptions);
    if (!expect(edge.ok, "edge segmentation should produce a mask")) return false;

    pointcloud::PlaneEdgeOptions imageOptions = edgeOptions;
    imageOptions.useImageFrame = true;
    imageOptions.imageOrigin = QVector3D(0, 0, 0);
    imageOptions.imageAxisU = QVector3D(1, 0, 0);
    imageOptions.imageAxisV = QVector3D(0, 1, 0);
    imageOptions.autoImageBounds = true;
    imageOptions.imageMargin = 50.0f;
    imageOptions.imagePixelSize = 0.05f;
    imageOptions.imageRoundIncrement = 10.0f;
    imageOptions.maximumImagePixels = 6000000;
    const auto raster = pointcloud::rasterizePlaneEdgeMask(
        points, indices, plane, imageOptions, edge);
    const auto ordinary = pointcloud::extractPlaneImage(
        points, indices, plane, imageOptions);
    if (!expect(raster.ok && raster.edgeMask, "edge mask should become export image")) return false;
    if (!expect(raster.image.format() == QImage::Format_Grayscale8,
                "edge export must be Grayscale8")) return false;
    for (int y = 0; y < raster.image.height(); ++y) {
        const uchar *row = raster.image.constScanLine(y);
        for (int x = 0; x < raster.image.width(); ++x) {
            if (!expect(row[x] == 0 || row[x] == 255,
                        "edge export pixels must be binary")) return false;
        }
    }
    return expect(raster.pixelSize == 0.05f, "edge export must use 0.05 mm per pixel")
        && expect(raster.occupiedCellCount > ordinary.occupiedCellCount * 100,
                  "edge export must use morphology mask, not point occupancy")
        && expect(edge.gridWidth == edge.image.width()
                   && edge.gridHeight == edge.image.height(),
                   "edge result must retain physical raster dimensions");
}

} // namespace

int main()
{
    const bool ok = rasterizesCompleteEdgeMask();
    if (ok) std::cout << "pointcloudprocessor_obstacle_tests: PASS\n";
    return ok ? 0 : 1;
}
