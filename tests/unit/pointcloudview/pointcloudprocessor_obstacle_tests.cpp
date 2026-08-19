#include "pointcloudprocessor.h"

#include <cmath>
#include <iostream>
#include <QTemporaryDir>
#include <QFile>

namespace {

bool expect(bool condition, const char *message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

QVector<pointcloud::Point3D> makeReferencePlane(QVector<int> *planeIndices) {
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y <= 10; ++y) {
        for (int x = 0; x <= 10; ++x) {
            planeIndices->push_back(points.size());
            points.push_back({float(x), float(y), 10.0f, 0.0f, 0.0f, 1.0f});
        }
    }
    return points;
}

bool detectsConnectedObstacle() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            points.push_back({3.0f + 0.4f * x, 3.0f + 0.4f * y, 12.0f});
    points.push_back({8.0f, 8.0f, 15.0f});

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.6f;
    options.minimumPointCount = 10;
    options.minimumArea = 1.0f;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    return expect(result.ok, "connected obstacle detection should succeed")
        && expect(result.regions.size() == 1, "one connected obstacle should be retained")
        && expect(result.obstacleIndices.size() == 25, "isolated high point should be filtered")
        && expect(std::abs(result.regions[0].meanHeight - 2.0f) < 1.0e-5f,
                  "mean obstacle height should use signed plane distance");
}

bool ignoresLowAndOutsidePoints() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int i = 0; i < 20; ++i) {
        points.push_back({2.0f + 0.05f * i, 2.0f, 10.5f});
        points.push_back({20.0f + 0.05f * i, 20.0f, 14.0f});
    }

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.5f;
    options.minimumPointCount = 5;
    options.minimumArea = 0.0f;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    return expect(result.ok, "empty obstacle result should still succeed")
        && expect(result.regions.isEmpty(), "low and out-of-footprint points should be ignored")
        && expect(result.obstacleIndices.isEmpty(), "no obstacle points should be returned");
}

bool detectsBothPlaneSidesSeparately() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float px = 4.0f + 0.3f * x;
            const float py = 4.0f + 0.3f * y;
            points.push_back({px, py, 12.0f});
            points.push_back({px, py, 8.0f});
        }
    }

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.5f;
    options.minimumPointCount = 8;
    options.minimumArea = 0.0f;
    options.connectivityRadiusCells = 2;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    int positiveRegions = 0;
    int negativeRegions = 0;
    for (const auto &region : result.regions) {
        if (region.sideSign > 0) ++positiveRegions;
        if (region.sideSign < 0) ++negativeRegions;
    }
    return expect(result.ok, "two-sided obstacle detection should succeed")
        && expect(result.regions.size() == 2, "overlapping plane sides must remain separate")
        && expect(positiveRegions == 1, "positive-side obstacle should be retained")
        && expect(negativeRegions == 1, "negative-side obstacle should be retained")
        && expect(result.positiveCandidatePointCount == 16,
                  "positive candidate count should be reported")
        && expect(result.negativeCandidatePointCount == 16,
                  "negative candidate count should be reported")
        && expect(result.obstacleIndices.size() == 32,
                  "all significant points on both sides should be marked");
}

bool reportsDisconnectedPlaneSurface() {
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            points.push_back({float(x), float(y), 10.0f});
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            points.push_back({30.0f + float(x), float(y), 10.0f});

    pointcloud::ThreePointPlaneOptions options;
    options.initialTolerance = 0.5f;
    options.surfaceTolerance = 0.1f;
    options.connectivityRadius = 1.5f;
    options.ransacIterations = 20;
    options.minInliers = 20;
    options.minimumDisconnectedComponentPoints = 20;
    options.minimumDisconnectedComponentRatio = 0.05f;
    const QVector<int> seeds{0, 9, 90};
    const auto result = pointcloud::extractPlaneFromThreePoints(points, seeds, options);

    return expect(result.ok, "disconnected plane extraction should succeed")
        && expect(result.connectedComponentCount == 2,
                  "all classified plane components should be reported")
        && expect(result.significantComponentCount == 2,
                  "both large plane components should be significant")
        && expect(result.planeIndices.size() == 100,
                  "the seed component should remain the extracted plane")
        && expect(result.disconnectedPlaneIndices.size() == 100,
                  "the separated component should be retained as occlusion evidence");
}

bool buildsRightHandedWorkpieceFrame() {
    const QVector<pointcloud::Point3D> points{
        {100.0f, 200.0f, 300.0f},
        {110.0f, 200.0f, 300.0f},
        {100.0f, 210.0f, 300.0f}
    };
    const auto frame = pointcloud::buildWorkpieceCoordinateSystem(
        points, {0, 1, 2});
    const QVector3D local = frame.robotBaseToWorkpiece.map(
        QVector3D(103.0f, 204.0f, 305.0f));
    return expect(frame.valid, "workpiece frame should be valid")
        && expect((frame.axisXInRobotBase - QVector3D(1, 0, 0)).length() < 1.0e-6f,
                  "P1 to P2 should define workpiece X")
        && expect((frame.axisYInRobotBase - QVector3D(0, 1, 0)).length() < 1.0e-6f,
                  "workpiece Y should complete a right-handed frame")
        && expect((frame.axisZInRobotBase - QVector3D(0, 0, 1)).length() < 1.0e-6f,
                  "workpiece Z should point to positive robot Z")
        && expect((local - QVector3D(3, 4, 5)).length() < 1.0e-4f,
                  "robot-base to workpiece inverse should be correct")
        && expect(std::abs(frame.poseA) < 1.0e-5f
                  && std::abs(frame.poseB) < 1.0e-5f
                  && std::abs(frame.poseC) < 1.0e-5f,
                  "identity workpiece frame should have zero ABC");
}

bool rejectsCollinearWorkpieceFrame() {
    const QVector<pointcloud::Point3D> points{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f}
    };
    const auto frame = pointcloud::buildWorkpieceCoordinateSystem(
        points, {0, 1, 2});
    return expect(!frame.valid, "collinear seeds must be rejected")
        && expect(!frame.error.isEmpty(), "collinear rejection should explain the error");
}

bool mapsRobotBasePlaneToWorkpieceImage() {
    // Deliberately place the workpiece far from the robot-base origin.  The
    // image must use local coordinates around O, not absolute base values.
    const QVector<pointcloud::Point3D> points{
        {1000.0f, 2000.0f, 500.0f}, {1001.0f, 2000.0f, 500.0f},
        {1000.0f, 2001.0f, 500.0f}, {1001.0f, 2001.0f, 500.0f},
        {1000.5f, 2000.5f, 501.0f}};
    const QVector<int> planeIndices{0, 1, 2, 3, 4};
    pointcloud::PlaneEdgeOptions options;
    options.useImageFrame = true;
    options.imageOrigin = QVector3D(1000.0f, 2000.0f, 500.0f);
    options.imageAxisU = QVector3D(1, 0, 0);
    options.imageAxisV = QVector3D(0, 1, 0);
    options.imageCropWidth = 2.0f;
    options.imageCropHeight = 2.0f;
    options.edgeGridSize = 0.5f;
    options.planeDistanceTolerance = 0.1f;
    options.imageMargin = 1.0f;
    options.imagePixelSize = 0.5f;
    options.imageRoundIncrement = 1.0f;
    options.maximumImagePixels = 1000000;
    const auto result = pointcloud::extractPlaneImage(
        points, planeIndices, {0, 0, 1, -500}, options);
    return expect(result.ok, "workpiece plane image should be generated")
        && expect(result.usedWorkpieceFrame, "mapping should use workpiece frame")
        && expect(result.mappedPlanePointCount == 4,
                  "off-plane point must not be mapped")
        && expect(result.rejectedNonPlanePointCount == 1,
                  "off-plane point rejection should be reported")
        && expect(result.workpieceMinimum.x() == 0.0f
                  && result.workpieceMaximum.x() == 1.0f,
                  "mapped X bounds should be local coordinates")
        && expect(result.automaticBounds && std::abs(result.pixelSize - 0.5f) < 1.0e-6f,
                  "automatic image should report fixed pixel pitch")
        && expect(std::fmod(result.width, 1.0f) < 1.0e-5f
                  && std::fmod(result.height, 1.0f) < 1.0e-5f,
                  "automatic image bounds should follow the requested increment");
}

bool exportsRobotBasePlanePly() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("plane.ply"));
    const QVector<pointcloud::Point3D> points{
        {100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 1.0f},
        {101.0f, 200.0f, 300.0f, 0.0f, 0.0f, 1.0f},
        {100.0f, 201.0f, 300.0f, 0.0f, 0.0f, 1.0f}};
    const auto frame = pointcloud::buildWorkpieceCoordinateSystem(points, {0, 1, 2});
    const auto result = pointcloud::exportPlanePly(
        path, points, {0, 1, 2}, {0, 0, 1, -300}, frame,
        QStringLiteral("source.ply"));
    QFile file(path);
    const bool readable = file.open(QIODevice::ReadOnly);
    const QByteArray header = readable ? file.read(256) : QByteArray();
    return expect(result.ok && result.exportedPointCount == 3,
                  "plane PLY export should write all valid points")
        && expect(header.contains("binary_little_endian"),
                  "plane PLY should be binary little endian")
        && expect(header.contains("source_frame robot_base"),
                  "plane PLY should declare robot-base source frame");
}

} // namespace

int main() {
    const bool ok = detectsConnectedObstacle()
        && ignoresLowAndOutsidePoints()
        && detectsBothPlaneSidesSeparately()
        && reportsDisconnectedPlaneSurface()
        && buildsRightHandedWorkpieceFrame()
        && rejectsCollinearWorkpieceFrame()
        && mapsRobotBasePlaneToWorkpieceImage()
        && exportsRobotBasePlanePly();
    if (ok) std::cout << "Obstacle detection tests passed\n";
    return ok ? 0 : 1;
}
