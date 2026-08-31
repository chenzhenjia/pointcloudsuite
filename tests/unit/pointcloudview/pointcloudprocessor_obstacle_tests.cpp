#include "pointcloudprocessor.h"

#include <QVector3D>

#include <cmath>
#include <iostream>
#include <limits>

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

bool extractsRealPointOccupancy()
{
    const QVector<pointcloud::Point3D> points{
        {0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f}, {10.0f, 10.0f, 0.0f}};
    const QVector<int> indices{0, 1, 2, 3};
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, 0.0f};
    pointcloud::PlaneEdgeOptions options;
    options.useImageFrame = true;
    options.imageOrigin = QVector3D(0, 0, 0);
    options.imageAxisU = QVector3D(1, 0, 0);
    options.imageAxisV = QVector3D(0, 1, 0);
    options.autoImageBounds = true;
    options.imageMargin = 0.0f;
    options.imagePixelSize = 0.05f;
    options.imageRoundIncrement = 10.0f;
    options.maximumImagePixels = 100000000;
    const auto image = pointcloud::extractPlaneImage(points, indices, plane, options);
    if (!expect(image.ok && image.image.format() == QImage::Format_Grayscale8,
                "direct plane image should be Grayscale8")) return false;
    qsizetype foreground = 0;
    qsizetype background = 0;
    for (int y = 0; y < image.image.height(); ++y) {
        const uchar *row = image.image.constScanLine(y);
        for (int x = 0; x < image.image.width(); ++x) {
            if (row[x] == 255) ++foreground;
            else if (row[x] == 0) ++background;
            else return expect(false, "direct plane image must remain binary");
        }
    }
    return expect(foreground > 0, "real point occupancy must produce foreground")
        && expect(background > foreground, "unoccupied cells must remain background")
        && expect(image.roiIndices == QVector<int>({1, 2, 3}),
                  "automatic image ROI must retain every usable source point in order");
}

bool extractsAutomaticRectangularRoi()
{
    const QVector<pointcloud::Point3D> points{
        {-5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 0.5f},
        {5.0f, -5.0f, 0.0f}, {-5.0f, 5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}};
    const QVector<int> indices{0, 1, 2, 3, 4};
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, 0.0f};
    pointcloud::PlaneEdgeOptions options;
    options.useImageFrame = true;
    options.imageOrigin = QVector3D(0.0f, 0.0f, 0.0f);
    options.imageAxisU = QVector3D(1.0f, 0.0f, 0.0f);
    options.imageAxisV = QVector3D(0.0f, 1.0f, 0.0f);
    options.autoImageBounds = true;
    options.imageMargin = 0.0f;
    options.imagePixelSize = 0.05f;
    options.imageRoundIncrement = 10.0f;
    options.maximumImagePixels = 100000000;
    const auto image = pointcloud::extractPlaneImage(points, indices, plane, options);
    return expect(image.ok, "automatic rectangular ROI should be extracted")
        && expect(image.roiIndices == QVector<int>({0, 2, 3, 4}),
                  "ROI must exclude non-plane points and preserve source order")
        && expect(image.rejectedNonPlanePointCount == 1,
                  "ROI diagnostics must report rejected non-plane points")
        && expect(std::abs(image.width - 10.0f) < 1.0e-6f
                  && std::abs(image.height - 10.0f) < 1.0e-6f,
                  "automatic ROI bounds must be centered and rounded");
}

bool validatesPlaneConsistency()
{
    QVector<pointcloud::Point3D> points{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {2.0f, 2.0f, 0.0f}};
    const QVector<int> reference{0, 1, 2};
    const QVector<int> verification{3, 4, 5};
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 2.0f, 0.0f};
    const auto validate = [&points, &plane, &reference](const QVector<int> &indices) {
        return pointcloud::validatePlaneConsistency(points, plane, reference, indices);
    };
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::Passed,
                "coplanar points should pass consistency validation")) return false;
    const QVector<int> multiReference{0, 1, 2, 6};
    if (!expect(pointcloud::validatePlaneConsistency(points, plane, multiReference, verification).status
                    == pointcloud::PlaneConsistencyStatus::Passed,
                "n-point reference selection should validate three verification points")) return false;
    if (!expect(pointcloud::validatePlaneConsistency(points, plane, multiReference, {3, 4, 6}).status
                    == pointcloud::PlaneConsistencyStatus::ReusedPoint,
                "verification points must not reuse any n-point reference point")) return false;

    const QVector<int> reversed{3, 5, 4};
    if (!expect(validate(reversed).status == pointcloud::PlaneConsistencyStatus::Passed,
                "opposite normal direction should pass consistency validation")) return false;

    const auto setTilt = [&points](float degrees) {
        const float slope = std::tan(degrees * std::acos(-1.0f) / 180.0f);
        points[3] = {0.0f, 0.0f, 0.0f};
        points[4] = {1.0f, 0.0f, slope};
        points[5] = {0.0f, 1.0f, 0.0f};
    };
    setTilt(0.99f);
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::Passed,
                "0.99 degree normal difference should pass")) return false;
    setTilt(1.01f);
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::AngleExceeded,
                "1.01 degree normal difference should fail")) return false;

    points[3] = {0.0f, 0.0f, 0.4f};
    points[4] = {1.0f, 0.0f, 0.4f};
    points[5] = {0.0f, 1.0f, 0.4f};
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::Passed,
                "0.4 mm distance should pass")) return false;
    points[3].z = 0.41f;
    points[4].z = 0.41f;
    points[5].z = 0.41f;
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::DistanceExceeded,
                "distance above 0.4 mm should fail")) return false;

    points[3] = {0.0f, 0.0f, 0.0f};
    points[4] = {1.0f, 0.0f, 0.0f};
    points[5] = {2.0f, 0.0f, 0.0f};
    if (!expect(validate(verification).status == pointcloud::PlaneConsistencyStatus::Collinear,
                "collinear verification points should fail")) return false;
    if (!expect(validate({0, 4, 5}).status == pointcloud::PlaneConsistencyStatus::ReusedPoint,
                "cross-group reused point should fail")) return false;
    if (!expect(validate({3, 4, 8}).status == pointcloud::PlaneConsistencyStatus::InvalidInput,
                "out-of-range point should fail")) return false;
    return expect(pointcloud::validatePlaneConsistency({}, plane, reference, verification).status
                      == pointcloud::PlaneConsistencyStatus::InvalidInput,
                  "empty input should fail");
}

bool fitsPlaneFromMultipleControlPoints()
{
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x)
            points.push_back({float(x), float(y), 0.01f * float((x + 2 * y) % 3 - 1)});
    }
    pointcloud::ThreePointPlaneOptions options;
    options.initialTolerance = 1.0f;
    options.surfaceTolerance = 0.1f;
    options.minInliers = 3;
    options.ransacIterations = 100;
    const QVector<int> controls{0, 7, 56, 63, 27};
    const auto result = pointcloud::extractPlaneFromPoints(points, controls, options);
    if (!expect(result.ok, "n-point controls should fit a plane")) return false;
    if (!expect(result.controlPoints.size() == controls.size()
                    && result.model.c > 0.99f,
                "n-point fit should retain every control point and orient normal toward +Z")) return false;
    if (!expect(pointcloud::extractPlaneFromThreePoints(points, {0, 7, 56}, options).ok,
                "three-point compatibility entry point should remain available")) return false;

    const auto tooFew = pointcloud::extractPlaneFromPoints(points, {0, 1}, options);
    const auto duplicate = pointcloud::extractPlaneFromPoints(points, {0, 1, 1}, options);
    const auto outOfRange = pointcloud::extractPlaneFromPoints(points, {0, 1, 64}, options);
    const auto collinear = pointcloud::extractPlaneFromPoints(points, {0, 1, 2}, options);
    if (!expect(!tooFew.ok && !duplicate.ok && !outOfRange.ok && !collinear.ok,
                "invalid and collinear n-point controls should be rejected")) return false;

    QVector<pointcloud::Point3D> invalidPoints = points;
    invalidPoints[2].x = std::numeric_limits<float>::quiet_NaN();
    const auto nonFinite = pointcloud::extractPlaneFromPoints(invalidPoints, {0, 1, 2}, options);
    if (!expect(!nonFinite.ok, "non-finite n-point controls should be rejected")) return false;

    QVector<pointcloud::Point3D> incompatible = points;
    incompatible[63].z = 2.0f;
    const auto outsideTolerance = pointcloud::extractPlaneFromPoints(
        incompatible, {0, 7, 56, 63}, options);
    return expect(!outsideTolerance.ok,
                  "n-point controls outside the final tolerance should reject the model");
}

bool calculatesBoundsAndBuildsWorkpieceCoordinate()
{
    const QVector<pointcloud::Point3D> boundsPoints{
        {-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};
    const auto center = pointcloud::calculatePlaneBoundsCenter(boundsPoints, {0, 1});
    if (!expect(center.ok && center.center == QVector3D(0.0f, 0.0f, 0.0f),
                "bounds center at the world origin must remain valid")) return false;

    QVector<pointcloud::Point3D> points{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
        {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {4.0f, 0.0f, 0.0f}};
    const auto frame = pointcloud::buildWorkpieceCoordinateSystem(
        points, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
        6, 7, true);
    if (!expect(frame.valid && frame.originInRobotBase == QVector3D(0.0f, 0.0f, 0.0f)
                && frame.axisZInRobotBase == QVector3D(0.0f, 0.0f, 1.0f),
                "independent axis points should build WObj1 at the bounds center")) return false;
    const QMatrix4x4 identity = frame.workpieceToRobotBase * frame.robotBaseToWorkpiece;
    if (!expect(std::abs(identity(0, 0) - 1.0f) < 1.0e-5f
                && std::abs(identity(1, 1) - 1.0f) < 1.0e-5f
                && std::abs(identity(2, 2) - 1.0f) < 1.0e-5f,
                "WObj1 transform inverse should be valid")) return false;
    const auto coincident = pointcloud::buildWorkpieceCoordinateSystem(
        points, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
        0, 7, true);
    const auto collinear = pointcloud::buildWorkpieceCoordinateSystem(
        points, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
        6, 8, true);
    return expect(!coincident.valid, "axis point at origin should be rejected")
        && expect(!collinear.valid, "collinear axis points should be rejected");
}

bool buildsRobotAxisWorkpieceCoordinate()
{
    const auto frame = pointcloud::buildWorkpieceCoordinateSystemFromRobotAxes(
        QVector3D(10.0f, 20.0f, 30.0f), QVector3D(0.0f, 0.0f, 1.0f));
    if (!expect(frame.valid, "robot-axis WObj1 should build on a horizontal plane"))
        return false;
    if (!expect(frame.originInRobotBase == QVector3D(10.0f, 20.0f, 30.0f),
                "robot-axis WObj1 must preserve the plane center")) return false;
    if (!expect(frame.axisXInRobotBase == QVector3D(1.0f, 0.0f, 0.0f)
                && frame.axisYInRobotBase == QVector3D(0.0f, 1.0f, 0.0f)
                && frame.axisZInRobotBase == QVector3D(0.0f, 0.0f, 1.0f),
                "horizontal WObj1 axes must follow robot-base axes")) return false;
    if (!expect(std::abs(frame.poseA) < 1.0e-5f && std::abs(frame.poseB) < 1.0e-5f
                    && std::abs(frame.poseC) < 1.0e-5f,
                "horizontal WObj1 pose must map to JSON XYZABC zero angles")) return false;
    const auto tilted = pointcloud::buildWorkpieceCoordinateSystemFromRobotAxes(
        QVector3D(), QVector3D(0.3f, 0.0f, 1.0f));
    if (!expect(tilted.valid && QVector3D::dotProduct(
                    tilted.axisZInRobotBase, QVector3D(0.0f, 0.0f, 1.0f)) > 0.0f,
                "tilted WObj1 Z must follow robot-base +Z")) return false;
    if (!expect(std::abs(QVector3D::dotProduct(tilted.axisXInRobotBase,
                                               tilted.axisYInRobotBase)) < 1.0e-5f
                && std::abs(QVector3D::dotProduct(tilted.axisXInRobotBase,
                                                   tilted.axisZInRobotBase)) < 1.0e-5f
                && std::abs(QVector3D::dotProduct(tilted.axisYInRobotBase,
                                                   tilted.axisZInRobotBase)) < 1.0e-5f,
                "tilted WObj1 axes must remain orthogonal")) return false;
    const auto invalid = pointcloud::buildWorkpieceCoordinateSystemFromRobotAxes(
        QVector3D(), QVector3D());
    if (!expect(!invalid.valid, "zero plane normal must reject robot-axis WObj1")) return false;
    const auto degenerateX = pointcloud::buildWorkpieceCoordinateSystemFromRobotAxes(
        QVector3D(), QVector3D(1.0f, 0.0f, 0.0f));
    return expect(!degenerateX.valid, "parallel robot-base X projection must be rejected");
}

bool selectsAutomaticWorkpieceAxisPoints()
{
    const QVector<pointcloud::Point3D> points{
        {-10.0f, -10.0f, 0.0f}, {10.0f, 10.0f, 0.0f},
        {5.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f},
        {0.0f, 5.0f, 0.0f}, {0.0f, 6.0f, 0.0f}};
    const auto selected = pointcloud::selectAutomaticWorkpieceAxisPoints(
        points, {0, 1, 2, 3, 4, 5},
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
    if (!expect(selected.ok, "automatic WObj1 helper points should be selected")) return false;
    if (!expect(selected.targetDistanceMm == 5.0f
                && selected.xPointIndex == 2 && selected.yPointIndex == 4,
                "automatic helper points should use the adaptive target distance")) return false;
    const auto finalFrame = pointcloud::buildWorkpieceCoordinateSystemFromThreePoints(
        points, QVector3D(0.0f, 0.0f, 0.0f),
        selected.xPointIndex, selected.yPointIndex);
    if (!expect(finalFrame.valid
                    && finalFrame.originInRobotBase == QVector3D(0.0f, 0.0f, 0.0f)
                    && finalFrame.axisXInRobotBase == QVector3D(1.0f, 0.0f, 0.0f)
                    && finalFrame.axisYInRobotBase == QVector3D(0.0f, 1.0f, 0.0f),
                "JSON frame must come from the final O/X+/Y+ three-point result")) return false;
    const QVector<pointcloud::Point3D> shortPoints{
        {-2.0f, -2.0f, 0.0f}, {2.0f, 2.0f, 0.0f},
        {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}};
    const auto fallback = pointcloud::selectAutomaticWorkpieceAxisPoints(
        shortPoints, {0, 1, 2, 3},
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
    if (!expect(fallback.ok && fallback.xUsedFallback && fallback.yUsedFallback,
                "automatic helper points should fall back to the farthest positive points")) return false;

    const QVector<pointcloud::Point3D> negativeOnly{{-2.0f, -2.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}};
    const auto invalid = pointcloud::selectAutomaticWorkpieceAxisPoints(
        negativeOnly, {0, 1},
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
    return expect(!invalid.ok, "missing positive robot-axis points should be diagnosed");
}

bool segmentsEdgesInWorkpieceFrame()
{
    QVector<pointcloud::Point3D> points;
    QVector<int> indices;
    for (int y = 0; y <= 3; ++y) {
        for (int x = 0; x <= 3; ++x) {
            indices.push_back(points.size());
            points.push_back({float(x), float(y), 0.0f});
        }
    }
    pointcloud::PlaneEdgeOptions options;
    options.edgeGridSize = 1.0f;
    options.morphologyCloseRadius = 0;
    options.morphologyOpenRadius = 0;
    options.useImageFrame = true;
    options.imageOrigin = QVector3D(0.0f, 0.0f, 0.0f);
    options.imageAxisU = QVector3D(0.0f, 1.0f, 0.0f);
    options.imageAxisV = QVector3D(-1.0f, 0.0f, 0.0f);
    const auto result = pointcloud::segmentPlaneEdges(
        points, indices, pointcloud::PlaneModel{0.0f, 0.0f, 1.0f, 0.0f}, options);
    if (!expect(result.ok, "edge segmentation should accept a workpiece frame")) return false;
    if (!expect((result.axisU - QVector3D(0.0f, 1.0f, 0.0f)).length() < 1.0e-5f
                && (result.axisV - QVector3D(-1.0f, 0.0f, 0.0f)).length() < 1.0e-5f,
                "edge raster axes must follow the workpiece frame")) return false;
    return expect((result.origin - options.imageOrigin).length() < 1.0e-5f,
                  "edge raster origin must follow the workpiece frame");
}

} // namespace

int main()
{
    const bool ok = rasterizesCompleteEdgeMask() && extractsRealPointOccupancy()
        && extractsAutomaticRectangularRoi()
        && validatesPlaneConsistency() && fitsPlaneFromMultipleControlPoints()
        && calculatesBoundsAndBuildsWorkpieceCoordinate()
        && buildsRobotAxisWorkpieceCoordinate()
        && selectsAutomaticWorkpieceAxisPoints()
        && segmentsEdgesInWorkpieceFrame();
    if (ok) std::cout << "pointcloudprocessor_obstacle_tests: PASS\n";
    return ok ? 0 : 1;
}
