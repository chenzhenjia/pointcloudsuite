#include <pcv/io/ply_reader.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
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
    const pcv::detail::io::PlyReadResult asciiResult = pcv::detail::io::readPly(asciiPath);
    if (!asciiResult.ok || asciiResult.points.size() != 2)
        return fail("ASCII PLY read failed");
    if (!closeTo(asciiResult.points[0].x, 1.0f)
        || !closeTo(asciiResult.points[0].z, 3.0f)
        || !closeTo(asciiResult.points[1].nx, 1.0f))
        return fail("ASCII property mapping failed");

    const QString binaryPath = directory.filePath(QStringLiteral("binary.ply"));
    QFile binary(binaryPath);
    if (!binary.open(QIODevice::WriteOnly)) return fail("binary fixture open failed");
    binary.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\n"
                 "property float x\nproperty float y\nproperty float z\nend_header\n");
    QDataStream stream(&binary);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << 7.0f << 8.0f << 9.0f;
    binary.close();
    const pcv::detail::io::PlyReadResult binaryResult = pcv::detail::io::readPly(binaryPath);
    if (!binaryResult.ok || binaryResult.points.size() != 1
        || !closeTo(binaryResult.points[0].y, 8.0f))
        return fail("binary PLY read failed");

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
    return 0;
}
