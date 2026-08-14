#include "pointcloudprocessor.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>
#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool closeTo(float actual, float expected, float epsilon = 0.0001f) {
    return std::fabs(actual - expected) <= epsilon;
}

QString writeAsciiFixture(const QString &directory) {
    const QString path = directory + QStringLiteral("/ascii_with_face.ply");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n"
        "12.34 56.78 90.12 0 0 1\n"
        "1.01 2.02 3.03 0 1 0\n"
        "-4.04 -5.05 -6.06 1 0 0\n"
        "3 0 1 2\n");
    return path;
}

QString writeBinaryFixture(const QString &directory) {
    const QString path = directory + QStringLiteral("/binary_normals.ply");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 2\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "property uchar red\n"
        "property uchar green\n"
        "property uchar blue\n"
        "end_header\n");

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << 10.01f << 20.02f << 30.03f << 0.1f << 0.2f << 0.3f;
    stream << quint8(10) << quint8(20) << quint8(30);
    stream << -1.25f << 2.50f << -3.75f << -0.4f << 0.5f << -0.6f;
    stream << quint8(40) << quint8(50) << quint8(60);
    return path;
}

void runFixtureTests(const QString &directory) {
    QVector<pointcloud::Point3D> points;
    QString error;

    const QString asciiPath = writeAsciiFixture(directory);
    expect(!asciiPath.isEmpty(), "create ASCII fixture");
    expect(pointcloud::loadPly(asciiPath, points, &error), "load ASCII PLY with face element");
    expect(points.size() == 3, "ASCII vertex count");
    if (points.size() == 3) {
        expect(closeTo(points[0].x, 12.34f), "0.01 mm position precision");
        expect(closeTo(points[0].nz, 1.0f), "ASCII nz value");
        expect(closeTo(points[1].ny, 1.0f), "ASCII ny value");
        expect(closeTo(points[2].nx, 1.0f), "ASCII nx value");
    }

    QVector<pointcloud::Point3D> cachedPoints;
    bool usedCache = false;
    error.clear();
    expect(pointcloud::loadPlyCached(asciiPath, cachedPoints, &error, &usedCache),
           "create binary cache from ASCII PLY");
    expect(!usedCache && cachedPoints.size() == 3, "first cached load parses source");
    cachedPoints.clear();
    usedCache = false;
    expect(pointcloud::loadPlyCached(asciiPath, cachedPoints, &error, &usedCache),
           "load from binary cache");
    expect(usedCache && cachedPoints.size() == 3, "second cached load uses cache");

    QVector<pointcloud::Point3D> many;
    for (int i = 0; i < 1000; ++i)
        many.push_back({float(i % 20), float((i / 20) % 20), float(i / 400), 0, 0, 1});
    const QVector<pointcloud::Point3D> lod = pointcloud::octreeLod(many, 64);
    expect(lod.size() <= 64 && !lod.isEmpty(), "octree LOD target count");

    QVector<pointcloud::Point3D> noisy;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        noisy.push_back({float(x) * 0.1f, float(y) * 0.1f, 0.0f, 0, 0, 1});
    noisy.push_back({50.0f, 50.0f, 50.0f, 0, 0, 1});
    pointcloud::NoiseOptions noiseOptions;
    noiseOptions.voxelEnabled = false;
    noiseOptions.statisticalEnabled = true;
    noiseOptions.meanK = 8;
    noiseOptions.stddevMultiplier = 1.0f;
    const pointcloud::NoiseResult noiseResult = pointcloud::removeNoise(noisy, noiseOptions);
    expect(noiseResult.ok && noiseResult.points.size() < noisy.size(), "statistical outlier removal");
    expect(noiseResult.points.size() >= 50, "preserve dense surface points");

    QVector<pointcloud::Point3D> voxelizedSurface;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            voxelizedSurface.push_back({float(x) * 0.25f, float(y) * 0.25f,
                                        float((x + y) % 3) * 0.002f, 0, 0, 1});
    voxelizedSurface.push_back({50.0f, 50.0f, 50.0f, 0, 0, 1});
    pointcloud::NoiseOptions defaultNoise;
    defaultNoise.voxelSize = 0.25f;
    const pointcloud::NoiseResult voxelizedNoise =
        pointcloud::removeNoise(voxelizedSurface, defaultNoise);
    expect(voxelizedNoise.ok && voxelizedNoise.points.size() >= 300
               && voxelizedNoise.points.size() < voxelizedSurface.size(),
           "default statistical filter preserves voxelized 2.5D surface");
    expect(!voxelizedNoise.error.contains(QStringLiteral("参数过严")),
           "default statistical filter does not report overly strict parameters");

    QVector<pointcloud::Point3D> largeScaleSurface;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            largeScaleSurface.push_back({float(x) * 10.0f, float(y) * 10.0f,
                                         0.0f, 0, 0, 1});
    largeScaleSurface.push_back({10000.0f, 10000.0f, 10000.0f, 0, 0, 1});
    defaultNoise.voxelEnabled = false;
    const pointcloud::NoiseResult scaledNoise =
        pointcloud::removeNoise(largeScaleSurface, defaultNoise);
    expect(scaledNoise.ok && scaledNoise.points.size() == voxelizedNoise.points.size(),
           "statistical threshold adapts to point-cloud scale");

    const QVector<pointcloud::Point3D> half = pointcloud::proportionalDownsample(points, 2);
    expect(half.size() == 2, "proportional downsample count");
    if (half.size() == 2)
        expect(closeTo(half[1].x, -4.04f), "proportional downsample ordering");

    const QString binaryPath = writeBinaryFixture(directory);
    expect(!binaryPath.isEmpty(), "create binary fixture");
    points.clear();
    error.clear();
    expect(pointcloud::loadPly(binaryPath, points, &error), "load binary little-endian PLY");
    expect(points.size() == 2, "binary vertex count");
    if (points.size() == 2) {
        expect(closeTo(points[0].x, 10.01f), "binary float width and precision");
        expect(closeTo(points[0].nz, 0.3f), "binary normal value");
        expect(closeTo(points[1].z, -3.75f), "binary second point alignment");
    }

    const QString truncatedPath = directory + QStringLiteral("/truncated.ply");
    QFile truncated(truncatedPath);
    expect(truncated.open(QIODevice::WriteOnly), "create truncated fixture");
    truncated.write(
        "ply\nformat ascii 1.0\nelement vertex 2\n"
        "property float x\nproperty float y\nproperty float z\n"
        "end_header\n1 2 3\n");
    truncated.close();
    points.clear();
    error.clear();
    expect(!pointcloud::loadPly(truncatedPath, points, &error), "reject truncated PLY");
    expect(points.isEmpty(), "clear partial data after truncated PLY");

    const QString motionPath = directory + QStringLiteral("/motion_progress.ply");
    QFile motionFile(motionPath);
    expect(motionFile.open(QIODevice::WriteOnly), "open motion fixture");
    if (motionFile.isOpen()) {
        motionFile.write("ply\nformat ascii 1.0\nelement vertex 3\n"
                         "property float x\nproperty float y\nproperty float z\n"
                         "end_header\n1 0 0\n1 -1 0\n1 -2 0\n");
        motionFile.close();
        pointcloud::WorldCloudInput motion;
        motion.filePath = motionPath;
        motion.startBaseFromFlange.setToIdentity();
        motion.endBaseFromFlange.setToIdentity();
        motion.endBaseFromFlange(0, 3) = 10.0f;
        motion.flangeFromDepth.setToIdentity();
        motion.scanProgressSource = pointcloud::WorldCloudInput::ScanProgressSource::LocalY;
        motion.voxelDownsample = false;
        motion.applyRobotTransform = true;
        pointcloud::IcpOptions noIcp;
        noIcp.enabled = false;
        const auto transformed = pointcloud::mergePlyCloudsInWorld({motion}, noIcp);
        expect(transformed.ok && transformed.points.size() == 3,
               "local-axis motion fixture merged");
        if (transformed.points.size() == 3) {
            expect(closeTo(transformed.points[0].x, 1.0f)
                       && closeTo(transformed.points[2].x, 11.0f),
                   "descending local axis follows Start to End");
        }
    }
}

void runGeometryTests() {
    QVector<pointcloud::Point3D> planePoints;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x)
            planePoints.push_back({float(x) * 0.1f, float(y) * 0.1f, 1.0f, 0, 0, 1});
    }
    planePoints.push_back({8.0f, 8.0f, 8.0f, 0, 0, 1});

    pointcloud::PlaneSegmentationOptions planeOptions;
    planeOptions.sampleDenominator = 4;
    planeOptions.minInliers = 100;
    planeOptions.maxPlanes = 1;
    planeOptions.distanceThreshold = 0.01f;
    const pointcloud::PlaneSegmentationResult segmented =
        pointcloud::segmentPlanes(planePoints, planeOptions);
    expect(segmented.ok && !segmented.planes.isEmpty(), "segment horizontal plane");
    if (!segmented.planes.isEmpty()) {
        const auto &plane = segmented.planes.first();
        expect(plane.inlierCount == 256, "plane statistics use complete canvas cache");
        expect(std::fabs(plane.a * 0.4f + plane.b * 0.7f + plane.c + plane.d) < 0.01f,
               "plane equation contains input point");
    }

    QVector<pointcloud::Point3D> twoPlanes;
    for (int z = 0; z < 2; ++z) {
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 12; ++x)
                twoPlanes.push_back({float(x) * 0.1f, float(y) * 0.1f,
                                     float(z) * 2.0f, 0, 0, 1});
        }
    }
    planeOptions.sampleDenominator = 1;
    planeOptions.minInliers = 100;
    planeOptions.maxPlanes = 2;
    const pointcloud::PlaneSegmentationResult multiple =
        pointcloud::segmentPlanes(twoPlanes, planeOptions);
    expect(multiple.ok && multiple.planes.size() == 2, "iteratively segment two planes");
    if (multiple.planes.size() == 2) {
        expect(multiple.planes[0].inlierCount == 144
                   && multiple.planes[1].inlierCount == 144,
               "multi-plane statistics cover complete canvas cache");
    }

    pointcloud::ThreePointPlaneOptions seedOptions;
    seedOptions.initialTolerance = 0.05f;
    seedOptions.surfaceTolerance = 0.02f;
    seedOptions.connectivityRadius = 0.16f;
    seedOptions.minInliers = 100;
    const pointcloud::ThreePointPlaneResult selected = pointcloud::extractPlaneFromThreePoints(
        planePoints, {17, 30, 225}, seedOptions);
    expect(selected.ok && selected.planeIndices.size() == 256,
           "extract complete connected plane from three points");
    expect(selected.candidateIndices.size() == 256, "initial plane candidates exclude outlier");
    expect(selected.rmsError < 0.001f, "refined plane RMS error");
    expect(selected.pcaRefinementCount >= 1 && selected.planarity > 0.99f,
           "PCA refinement reports a planar model");
    pointcloud::PlaneEdgeOptions selectedEdgeOptions;
    selectedEdgeOptions.edgeGridSize = 0.1f;
    const pointcloud::PlaneEdgeResult selectedEdges = pointcloud::segmentPlaneEdges(
        planePoints, selected.planeIndices, selected.model, selectedEdgeOptions);
    expect(selectedEdges.ok && !selectedEdges.edgeIndices.isEmpty(),
           "segment edges after plane extraction");
    expect(selectedEdges.gridSize > 0.0f && !selectedEdges.contours.isEmpty(),
           "extract ordered mask contours in separate edge stage");
    expect(!selectedEdges.image.isNull() && selectedEdges.image.width() > 0
               && selectedEdges.image.height() > 0,
           "render extracted plane as a 2D image");
    {
        bool hasBlackBackground = false;
        bool hasWorkpiecePixel = false;
        for (int y = 0; y < selectedEdges.image.height(); ++y) {
            for (int x = 0; x < selectedEdges.image.width(); ++x) {
                const QRgb pixel = selectedEdges.image.pixel(x, y);
                if (qRed(pixel) == 0 && qGreen(pixel) == 0 && qBlue(pixel) == 0)
                    hasBlackBackground = true;
                else
                    hasWorkpiecePixel = true;
            }
        }
        expect(hasBlackBackground && hasWorkpiecePixel,
               "edge image uses pure black outside the workpiece");
    }
    const pointcloud::PlaneImageResult planeImage = pointcloud::extractPlaneImage(
        planePoints, selected.planeIndices, selected.model, selectedEdgeOptions);
    expect(planeImage.ok && !planeImage.image.isNull(),
           "extract standalone plane image");
    if (planeImage.ok) {
        bool hasBlackBackground = false;
        bool hasWorkpiecePixel = false;
        for (int y = 0; y < planeImage.image.height(); ++y) {
            for (int x = 0; x < planeImage.image.width(); ++x) {
                const QRgb pixel = planeImage.image.pixel(x, y);
                if (qRed(pixel) == 0 && qGreen(pixel) == 0 && qBlue(pixel) == 0)
                    hasBlackBackground = true;
                else
                    hasWorkpiecePixel = true;
            }
        }
        expect(hasBlackBackground && hasWorkpiecePixel,
               "standalone plane image uses pure black outside the workpiece");
    }
    if (!selectedEdges.contours.isEmpty()) {
        expect(selectedEdges.contours.first().points.size() >= 4,
               "ordered contour contains a closed polyline");
        const auto &first = selectedEdges.contours.first().points.first();
        const auto &last = selectedEdges.contours.first().points.last();
        expect(closeTo(first.x, last.x, 0.0001f)
                   && closeTo(first.y, last.y, 0.0001f)
                   && closeTo(first.z, last.z, 0.0001f),
               "Marching Squares contour is closed");
        for (const auto &point : selectedEdges.contours.first().points) {
            expect(std::fabs(selected.model.a * point.x + selected.model.b * point.y
                             + selected.model.c * point.z + selected.model.d) < 0.0001f,
                   "contour vertices lie on fitted plane");
        }
    }

    QVector<pointcloud::Point3D> planeWithHole;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            if (x >= 9 && x <= 14 && y >= 9 && y <= 14) continue;
            planeWithHole.push_back({float(x) * 0.1f, float(y) * 0.1f, 0.5f, 0, 0, 1});
        }
    }
    pointcloud::ThreePointPlaneOptions holeOptions = seedOptions;
    holeOptions.initialTolerance = 0.02f;
    holeOptions.surfaceTolerance = 0.01f;
    holeOptions.connectivityRadius = 0.16f;
    holeOptions.minInliers = 400;
    const pointcloud::ThreePointPlaneResult withHole =
        pointcloud::extractPlaneFromThreePoints(planeWithHole, {0, 23, 539}, holeOptions);
    expect(withHole.ok, "extract plane containing a hole");
    pointcloud::PlaneEdgeOptions holeEdgeOptions;
    holeEdgeOptions.edgeGridSize = 0.1f;
    holeEdgeOptions.morphologyCloseRadius = 1;
    holeEdgeOptions.morphologyOpenRadius = 0;
    const pointcloud::PlaneEdgeResult holeEdges = pointcloud::segmentPlaneEdges(
        planeWithHole, withHole.planeIndices, withHole.model, holeEdgeOptions);
    expect(holeEdges.ok, "run separate edge stage for plane containing a hole");
    expect(std::count_if(holeEdges.contours.cbegin(), holeEdges.contours.cend(),
                         [](const pointcloud::PlaneContour &contour) { return contour.hole; }) >= 1,
           "classify an interior Marching Squares contour as a hole");

    const QVector<pointcloud::Point3D> collinearCloud = {
        {0.0f, 0.0f, 0.0f, 0, 0, 1},
        {1.0f, 0.0f, 0.0f, 0, 0, 1},
        {2.0f, 0.0f, 0.0f, 0, 0, 1}
    };
    seedOptions.minInliers = 3;
    const pointcloud::ThreePointPlaneResult collinear = pointcloud::extractPlaneFromThreePoints(
        collinearCloud, {0, 1, 2}, seedOptions);
    expect(!collinear.ok, "reject collinear three seeds");

    const QVector<pointcloud::Point3D> nearCoincidentCloud = {
        {0.0f, 0.0f, 0.0f, 0, 0, 1},
        {1.0e-9f, 0.0f, 0.0f, 0, 0, 1},
        {0.0f, 1.0e-9f, 0.0f, 0, 0, 1}
    };
    const pointcloud::ThreePointPlaneResult nearCoincident =
        pointcloud::extractPlaneFromThreePoints(nearCoincidentCloud, {0, 1, 2}, seedOptions);
    expect(!nearCoincident.ok, "reject three seeds that are too close");

    QVector<pointcloud::Point3D> noisyTilted;
    for (int y = 0; y < 14; ++y) {
        for (int x = 0; x < 14; ++x) {
            const float px = float(x) * 0.1f;
            const float py = float(y) * 0.1f;
            const float noise = float((x * 7 + y * 11) % 9 - 4) * 0.0005f;
            noisyTilted.push_back({px, py, 0.2f * px - 0.1f * py + 0.3f + noise,
                                   0, 0, 1});
        }
    }
    noisyTilted.push_back({0.5f, 0.5f, 0.8f, 0, 0, 1});
    seedOptions.initialTolerance = 0.04f;
    seedOptions.surfaceTolerance = 0.01f;
    seedOptions.connectivityRadius = 0.16f;
    seedOptions.minInliers = 150;
    const pointcloud::ThreePointPlaneResult noisy = pointcloud::extractPlaneFromThreePoints(
        noisyTilted, {0, 13, 195}, seedOptions);
    expect(noisy.ok && noisy.planeIndices.size() == 196,
           "refine noisy tilted plane and reject nearby outlier");
    if (noisy.ok) {
        float maximumEquationError = 0.0f;
        for (int index : noisy.planeIndices) {
            const auto &point = noisyTilted[index];
            maximumEquationError = std::max(maximumEquationError,
                std::abs(noisy.model.a * point.x + noisy.model.b * point.y
                         + noisy.model.c * point.z + noisy.model.d));
        }
        expect(maximumEquationError <= seedOptions.surfaceTolerance,
               "refined plane equation contains every returned point");
        expect(noisy.model.c > 0.0f, "refined plane normal points toward positive Z");
    }

    pointcloud::ThreePointPlaneOptions heightOptions = seedOptions;
    heightOptions.useZAxisResidual = true;
    heightOptions.maxNormalTiltDegrees = 45.0f;
    const pointcloud::ThreePointPlaneResult heightPlane =
        pointcloud::extractPlaneFromThreePoints(noisyTilted, {0, 13, 195}, heightOptions);
    expect(heightPlane.ok && heightPlane.planeIndices.size() == 196,
           "extract 2.5D height plane using Z-axis residual");

    QVector<pointcloud::Point3D> verticalPlane;
    for (int z = 0; z < 12; ++z)
        for (int y = 0; y < 12; ++y)
            verticalPlane.push_back({0.0f, float(y) * 0.1f, float(z) * 0.1f, 1, 0, 0});
    heightOptions.minInliers = 100;
    const pointcloud::ThreePointPlaneResult vertical =
        pointcloud::extractPlaneFromThreePoints(verticalPlane, {0, 11, 143}, heightOptions);
    expect(!vertical.ok, "reject vertical plane in 2.5D height extraction mode");

    QVector<pointcloud::Point3D> separated;
    for (int region = 0; region < 2; ++region) {
        const float offset = float(region) * 10.0f;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                separated.push_back({offset + float(x) * 0.1f, float(y) * 0.1f,
                                     float((x + y) % 3 - 1) * 0.002f, 0, 0, 1});
    }
    seedOptions.initialTolerance = 0.05f;
    seedOptions.surfaceTolerance = 0.02f;
    seedOptions.connectivityRadius = 0.16f;
    seedOptions.minInliers = 40;
    const pointcloud::ThreePointPlaneResult connected = pointcloud::extractPlaneFromThreePoints(
        separated, {0, 7, 63}, seedOptions);
    expect(connected.ok && connected.planeIndices.size() == 64,
           "keep only seed connected component from coplanar regions");
    bool containsRemote = false;
    for (int index : connected.planeIndices) containsRemote = containsRemote || index >= 64;
    expect(!containsRemote, "exclude remote coplanar component");

    const pointcloud::ThreePointPlaneResult splitSeeds = pointcloud::extractPlaneFromThreePoints(
        separated, {0, 7, 127}, seedOptions);
    expect(!splitSeeds.ok, "reject seeds from different connected components");

    pointcloud::GeometryFeatureOptions featureOptions;
    featureOptions.neighborCount = 12;
    const pointcloud::GeometryFeatureResult features =
        pointcloud::extractGeometryFeatures(planePoints, featureOptions);
    expect(features.ok && features.features.size() == planePoints.size(),
           "extract geometry features through public API");
}

int loadRealPly(const QString &path) {
    QVector<pointcloud::Point3D> points;
    QString error;
    bool usedCache = false;
    if (!pointcloud::loadPlyCached(path, points, &error, &usedCache)) {
        std::cerr << "FAIL: " << error.toStdString() << '\n';
        return 1;
    }
    std::cout << "Loaded points: " << points.size() << '\n';
    std::cout << "Used cache: " << (usedCache ? "yes" : "no") << '\n';
    std::cout << "Bytes per point: " << sizeof(pointcloud::Point3D) << '\n';
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--ply"))
        return loadRealPly(QString::fromLocal8Bit(argv[2]));

    QTemporaryDir temporaryDirectory;
    expect(temporaryDirectory.isValid(), "create temporary test directory");
    if (temporaryDirectory.isValid()) runFixtureTests(temporaryDirectory.path());
    runGeometryTests();

    if (failures == 0) std::cout << "All point cloud processor tests passed.\n";
    return failures == 0 ? 0 : 1;
}
