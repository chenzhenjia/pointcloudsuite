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

    if (failures == 0) std::cout << "All point cloud processor tests passed.\n";
    return failures == 0 ? 0 : 1;
}
