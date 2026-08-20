#include <pcv/io/ply_reader.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

namespace {
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool closeTo(float actual, float expected) {
    return std::abs(actual - expected) < 1.0e-6f;
}
int fail(const char *message) {
    std::cerr << message << '\n';
    return 1;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary directory creation failed");

    const QString asciiPath = directory.filePath(QStringLiteral("ascii.ply"));
    const QByteArray ascii =
        "ply\nformat ascii 1.0\nelement vertex 2\n"
        "property float z\nproperty float x\nproperty float y\n"
        "property float nx\nproperty float ny\nproperty float nz\nend_header\n"
        "3 1 2 0 0 1\n6 4 5 1 0 0\n";
    if (!writeFile(asciiPath, ascii)) return fail("ASCII fixture write failed");
    pcv::detail::io::PlyReadOptions twoWorkerOptions;
    twoWorkerOptions.asciiWorkerCount = 2;
    const pcv::detail::io::PlyReadResult asciiResult =
        pcv::detail::io::readPly(asciiPath, twoWorkerOptions);
    if (!asciiResult.ok || asciiResult.points.size() != 2)
        return fail("ASCII PLY read failed");
    if (asciiResult.asciiWorkerCount != 2)
        return fail("explicit ASCII worker count was not applied");
    if (!closeTo(asciiResult.points[0].x, 1.0f)
        || !closeTo(asciiResult.points[0].z, 3.0f)
        || !closeTo(asciiResult.points[1].nx, 1.0f))
        return fail("ASCII property mapping failed");

    const QString fastPath = directory.filePath(QStringLiteral("ascii_extra.ply"));
    const QByteArray fastAscii =
        "ply\nformat ascii 1.0\nelement vertex 1\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty float intensity\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
        "-1.25e+1 2.5E-1 +3.0 255 99.0\n3 0 0 0\n";
    if (!writeFile(fastPath, fastAscii)) return fail("fast ASCII fixture write failed");
    const auto fastResult = pcv::detail::io::readPly(fastPath);
    if (!fastResult.ok || !closeTo(fastResult.points[0].x, -12.5f)
        || !closeTo(fastResult.points[0].y, 0.25f)
        || !closeTo(fastResult.points[0].z, 3.0f))
        return fail("fast ASCII parser, trailing attribute or face payload handling failed");
    if (fastResult.asciiWorkerCount != 1)
        return fail("small ASCII payload should remain single-threaded");

    const QString binaryPath = directory.filePath(QStringLiteral("binary_generic.ply"));
    QFile binary(binaryPath);
    if (!binary.open(QIODevice::WriteOnly)) return fail("binary fixture open failed");
    binary.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\n"
                 "property float x\nproperty float y\nproperty float z\n"
                 "property uchar intensity\nend_header\n");
    QDataStream stream(&binary);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << 7.0f << 8.0f << 9.0f << quint8(42);
    binary.close();
    const pcv::detail::io::PlyReadResult binaryResult = pcv::detail::io::readPly(binaryPath);
    if (!binaryResult.ok || binaryResult.points.size() != 1
        || binaryResult.binaryWorkerCount != 1
        || !closeTo(binaryResult.points[0].y, 8.0f))
        return fail("binary PLY read failed");

    const QString binaryFastPath = directory.filePath(QStringLiteral("binary_fast.ply"));
    QFile binaryFast(binaryFastPath);
    if (!binaryFast.open(QIODevice::WriteOnly)) return fail("fast binary fixture open failed");
    binaryFast.write("ply\nformat binary_little_endian 1.0\nelement vertex 4\n"
                     "property float x\nproperty float y\nproperty float z\nend_header\n");
    QDataStream fastStream(&binaryFast);
    fastStream.setByteOrder(QDataStream::LittleEndian);
    fastStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (int i = 0; i < 4; ++i)
        fastStream << float(i + 1) << float(i + 2) << float(i + 3);
    binaryFast.close();
    pcv::detail::io::PlyReadOptions binaryTwoWorkerOptions;
    binaryTwoWorkerOptions.binaryWorkerCount = 2;
    const auto binaryFastResult =
        pcv::detail::io::readPly(binaryFastPath, binaryTwoWorkerOptions);
    if (!binaryFastResult.ok || binaryFastResult.points.size() != 4
        || binaryFastResult.binaryWorkerCount != 2
        || !closeTo(binaryFastResult.points[3].x, 4.0f)
        || !closeTo(binaryFastResult.points[3].z, 6.0f))
        return fail("parallel compact binary PLY read failed");

    const QString invalidPath = directory.filePath(QStringLiteral("invalid.ply"));
    if (!writeFile(invalidPath, "ply\nformat ascii 1.0\nelement vertex 1\nend_header\n"))
        return fail("invalid fixture write failed");
    if (pcv::detail::io::readPly(invalidPath).ok)
        return fail("missing xyz properties were accepted");

    pcv::detail::io::PlyReadOptions cancelledOptions;
    cancelledOptions.isCancelled = [] { return true; };
    const pcv::detail::io::PlyReadResult cancelled = pcv::detail::io::readPly(asciiPath, cancelledOptions);
    if (!cancelled.cancelled || cancelled.ok)
        return fail("cancellation was not reported");

    if (argc > 1) {
        const QString samplePath = QString::fromLocal8Bit(argv[1]);
        pcv::detail::io::PlyReadOptions sampleOptions;
        if (argc > 2) {
            const int workers = QByteArray(argv[2]).toInt();
            sampleOptions.asciiWorkerCount = workers;
            sampleOptions.binaryWorkerCount = workers;
        }
        const pcv::detail::io::PlyReadResult sample =
            pcv::detail::io::readPly(samplePath, sampleOptions);
        if (!sample.ok || sample.points.size() != sample.declaredPointCount)
            return fail("external PLY sample read failed");
        const char *format = sample.format == pcv::detail::io::PlyFormat::Ascii
            ? "ascii"
            : sample.format == pcv::detail::io::PlyFormat::BinaryLittleEndian
                ? "binary_little_endian" : "binary_big_endian";
        std::cout << "sample=" << QFileInfo(samplePath).fileName().toStdString()
                  << " format=" << format
                  << " points=" << sample.points.size()
                  << " ascii_workers=" << sample.asciiWorkerCount
                  << " binary_workers=" << sample.binaryWorkerCount
                  << " bounds_min=" << sample.minimum.x << ',' << sample.minimum.y << ','
                  << sample.minimum.z
                  << " bounds_max=" << sample.maximum.x << ',' << sample.maximum.y << ','
                  << sample.maximum.z
                  << " header_ms=" << sample.headerElapsedMs
                  << " boundary_scan_ms=" << sample.boundaryScanElapsedMs
                  << " parse_ms=" << sample.parseElapsedMs
                  << " total_ms=" << sample.totalElapsedMs << '\n';
    }
    return 0;
}
