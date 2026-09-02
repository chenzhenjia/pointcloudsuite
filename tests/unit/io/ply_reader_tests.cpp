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
    const auto binaryFastResult = pcv::detail::io::readPly(binaryFastPath);
    if (!binaryFastResult.ok || binaryFastResult.points.size() != 4
        || !closeTo(binaryFastResult.points[3].x, 4.0f)
        || !closeTo(binaryFastResult.points[3].z, 6.0f))
        return fail("parallel compact binary PLY read failed");

    // The uchar intensity column of the generic binary fixture must be
    // decoded when readIntensity is requested.
    pcv::detail::io::PlyReadOptions ucharIntensityOptions;
    ucharIntensityOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult ucharIntensityResult =
        pcv::detail::io::readPly(binaryPath, ucharIntensityOptions);
    if (!ucharIntensityResult.ok
        || ucharIntensityResult.intensity.size() != 1
        || !closeTo(ucharIntensityResult.intensity[0], 42.0f))
        return fail("uchar intensity channel read failed");

    // ASCII path also reports the intensity column when requested.
    pcv::detail::io::PlyReadOptions asciiIntensityOptions;
    asciiIntensityOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult asciiIntensityResult =
        pcv::detail::io::readPly(fastPath, asciiIntensityOptions);
    if (!asciiIntensityResult.ok
        || asciiIntensityResult.intensity.size() != 1
        || !closeTo(asciiIntensityResult.intensity[0], 99.0f))
        return fail("ASCII intensity channel read failed");

    // Four-channel float x,y,z,intensity (little endian) uses the parallel
    // fixed-stride fast path and fills the intensity channel.
    const QString fourChannelPath = directory.filePath(QStringLiteral("binary_4ch.ply"));
    QFile fourChannel(fourChannelPath);
    if (!fourChannel.open(QIODevice::WriteOnly)) return fail("4-channel fixture open failed");
    fourChannel.write("ply\nformat binary_little_endian 1.0\nelement vertex 2\n"
                      "property float x\nproperty float y\nproperty float z\n"
                      "property float intensity\nend_header\n");
    QDataStream fourStream(&fourChannel);
    fourStream.setByteOrder(QDataStream::LittleEndian);
    fourStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    fourStream << 1.0f << 2.0f << 3.0f << 10.5f
               << 4.0f << 5.0f << 6.0f << 20.25f;
    fourChannel.close();
    pcv::detail::io::PlyReadOptions fourOptions;
    fourOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult fourResult =
        pcv::detail::io::readPly(fourChannelPath, fourOptions);
    if (!fourResult.ok || fourResult.points.size() != 2
        || fourResult.intensity.size() != 2
        || !fourResult.fastPath
        || !closeTo(fourResult.points[0].z, 3.0f)
        || !closeTo(fourResult.points[1].y, 5.0f)
        || !closeTo(fourResult.intensity[0], 10.5f)
        || !closeTo(fourResult.intensity[1], 20.25f))
        return fail("4-channel float LE parallel read failed");

    // Four-channel float big-endian variant.
    const QString fourChannelBEPath = directory.filePath(QStringLiteral("binary_4ch_be.ply"));
    QFile fourChannelBE(fourChannelBEPath);
    if (!fourChannelBE.open(QIODevice::WriteOnly)) return fail("4-channel BE fixture open failed");
    fourChannelBE.write("ply\nformat binary_big_endian 1.0\nelement vertex 1\n"
                        "property float x\nproperty float y\nproperty float z\n"
                        "property float intensity\nend_header\n");
    QDataStream beStream(&fourChannelBE);
    beStream.setByteOrder(QDataStream::BigEndian);
    beStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    beStream << 7.0f << 8.0f << 9.0f << 42.0f;
    fourChannelBE.close();
    pcv::detail::io::PlyReadOptions beOptions;
    beOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult beResult =
        pcv::detail::io::readPly(fourChannelBEPath, beOptions);
    if (!beResult.ok || beResult.points.size() != 1
        || !beResult.fastPath
        || !closeTo(beResult.points[0].x, 7.0f)
        || !closeTo(beResult.points[0].z, 9.0f)
        || !closeTo(beResult.intensity[0], 42.0f))
        return fail("4-channel float big-endian read failed");

    // Reordered attributes (z,x,y,intensity) still map correctly.
    const QString reorderedPath = directory.filePath(QStringLiteral("binary_reordered.ply"));
    QFile reordered(reorderedPath);
    if (!reordered.open(QIODevice::WriteOnly)) return fail("reordered fixture open failed");
    reordered.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\n"
                    "property float z\nproperty float x\nproperty float y\n"
                    "property float intensity\nend_header\n");
    QDataStream reorderedStream(&reordered);
    reorderedStream.setByteOrder(QDataStream::LittleEndian);
    reorderedStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    reorderedStream << 30.0f << 10.0f << 20.0f << 5.0f; // z, x, y, intensity
    reordered.close();
    pcv::detail::io::PlyReadOptions reorderedOptions;
    reorderedOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult reorderedResult =
        pcv::detail::io::readPly(reorderedPath, reorderedOptions);
    if (!reorderedResult.ok || reorderedResult.points.size() != 1
        || !reorderedResult.fastPath
        || !closeTo(reorderedResult.points[0].x, 10.0f)
        || !closeTo(reorderedResult.points[0].y, 20.0f)
        || !closeTo(reorderedResult.points[0].z, 30.0f)
        || !closeTo(reorderedResult.intensity[0], 5.0f))
        return fail("reordered binary property mapping failed");

    // Six-float layout (x,y,z,nx,ny,nz) now also uses the parallel fast path.
    const QString normalPath = directory.filePath(QStringLiteral("binary_normals.ply"));
    QFile normalFile(normalPath);
    if (!normalFile.open(QIODevice::WriteOnly)) return fail("normals fixture open failed");
    normalFile.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\n"
                     "property float x\nproperty float y\nproperty float z\n"
                     "property float nx\nproperty float ny\nproperty float nz\n"
                     "end_header\n");
    QDataStream normalStream(&normalFile);
    normalStream.setByteOrder(QDataStream::LittleEndian);
    normalStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    normalStream << 1.0f << 2.0f << 3.0f << 0.0f << 1.0f << 0.0f;
    normalFile.close();
    const pcv::detail::io::PlyReadResult normalResult =
        pcv::detail::io::readPly(normalPath);
    if (!normalResult.ok || normalResult.points.size() != 1
        || !normalResult.fastPath
        || !closeTo(normalResult.points[0].x, 1.0f)
        || !closeTo(normalResult.points[0].ny, 1.0f)
        || !closeTo(normalResult.points[0].nz, 0.0f))
        return fail("6-float normals parallel read failed");

    // Compact 3-float x,y,z uses the contiguous block-decode fast path.
    if (!binaryFastResult.fastPath)
        return fail("compact 3-float fast path flag missing");

    // Double x,y,z (non-float32) still takes the parallel fixed-stride path.
    const QString doublePath = directory.filePath(QStringLiteral("binary_double.ply"));
    QFile doubleFile(doublePath);
    if (!doubleFile.open(QIODevice::WriteOnly)) return fail("double fixture open failed");
    doubleFile.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\n"
                     "property double x\nproperty double y\nproperty double z\n"
                     "property float intensity\nend_header\n");
    QDataStream doubleStream(&doubleFile);
    doubleStream.setByteOrder(QDataStream::LittleEndian);
    doubleStream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    doubleStream << 1.5 << 2.5 << 3.5;
    doubleStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    doubleStream << 7.5f;
    doubleFile.close();
    pcv::detail::io::PlyReadOptions doubleOptions;
    doubleOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult doubleResult =
        pcv::detail::io::readPly(doublePath, doubleOptions);
    if (!doubleResult.ok || doubleResult.points.size() != 1
        || !doubleResult.fastPath
        || !closeTo(doubleResult.points[0].x, 1.5f)
        || !closeTo(doubleResult.points[0].y, 2.5f)
        || !closeTo(doubleResult.points[0].z, 3.5f)
        || !closeTo(doubleResult.intensity[0], 7.5f))
        return fail("double xyz parallel read failed");

    // Explicit worker count is applied and reported for the binary fast path.
    const QString largePath = directory.filePath(QStringLiteral("binary_large.ply"));
    const int largeCount = 100000;
    QFile largeFile(largePath);
    if (!largeFile.open(QIODevice::WriteOnly)) return fail("large fixture open failed");
    largeFile.write(QByteArray("ply\nformat binary_little_endian 1.0\nelement vertex ")
        .append(QByteArray::number(largeCount))
        .append("\nproperty float x\nproperty float y\nproperty float z\n"
                "property float intensity\nend_header\n"));
    QDataStream largeStream(&largeFile);
    largeStream.setByteOrder(QDataStream::LittleEndian);
    largeStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (int i = 0; i < largeCount; ++i)
        largeStream << float(i) << float(i + 1) << float(i + 2) << float(i) * 0.5f;
    largeFile.close();
    pcv::detail::io::PlyReadOptions largeOptions;
    largeOptions.binaryWorkerCount = 2;
    largeOptions.readIntensity = true;
    const pcv::detail::io::PlyReadResult largeResult =
        pcv::detail::io::readPly(largePath, largeOptions);
    if (!largeResult.ok || largeResult.points.size() != largeCount
        || !largeResult.fastPath
        || largeResult.binaryWorkerCount != 2)
        return fail("binary parallel worker count was not applied");
    if (!closeTo(largeResult.points[largeCount - 1].x, float(largeCount - 1))
        || !closeTo(largeResult.intensity[largeCount - 1], float(largeCount - 1) * 0.5f)
        || !closeTo(largeResult.points[12345].y, 12346.0f)
        || !closeTo(largeResult.intensity[12345], 12345.0f * 0.5f))
        return fail("large parallel binary decode mismatch");

    // Adaptive worker selection engages parallelism for a large enough
    // fixed-stride binary payload (250k x 6 floats = 6 MB).
    const QString adaptivePath = directory.filePath(QStringLiteral("binary_adaptive.ply"));
    const int adaptiveCount = 250000;
    QFile adaptiveFile(adaptivePath);
    if (!adaptiveFile.open(QIODevice::WriteOnly)) return fail("adaptive fixture open failed");
    adaptiveFile.write(QByteArray("ply\nformat binary_little_endian 1.0\nelement vertex ")
        .append(QByteArray::number(adaptiveCount))
        .append("\nproperty float x\nproperty float y\nproperty float z\n"
                "property float nx\nproperty float ny\nproperty float nz\n"
                "end_header\n"));
    QDataStream adaptiveStream(&adaptiveFile);
    adaptiveStream.setByteOrder(QDataStream::LittleEndian);
    adaptiveStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (int i = 0; i < adaptiveCount; ++i) {
        adaptiveStream << float(i) << float(i) * 2.0f << float(i) * 0.5f
                       << 0.0f << 0.0f << 1.0f;
    }
    adaptiveFile.close();
    const pcv::detail::io::PlyReadResult adaptiveResult =
        pcv::detail::io::readPly(adaptivePath);
    if (!adaptiveResult.ok || adaptiveResult.points.size() != adaptiveCount
        || !adaptiveResult.fastPath
        || adaptiveResult.binaryWorkerCount < 2)
        return fail("adaptive binary parallel worker selection failed");
    if (!closeTo(adaptiveResult.points[adaptiveCount - 1].x, float(adaptiveCount - 1))
        || !closeTo(adaptiveResult.points[adaptiveCount - 1].y,
                    float(adaptiveCount - 1) * 2.0f)
        || !closeTo(adaptiveResult.points[123].nz, 1.0f))
        return fail("adaptive parallel binary decode mismatch");

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
