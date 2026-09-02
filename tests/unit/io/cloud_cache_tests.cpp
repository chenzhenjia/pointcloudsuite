#include <pcv/io/cloud_cache.h>
#include <pcv/io/ply_writer.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool writeCloud(const QString &path, float value) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = QByteArray("ply\nformat ascii 1.0\nelement vertex 1\n"
                                      "property float x\nproperty float y\n"
                                      "property float z\nend_header\n")
        + QByteArray::number(value) + " 2 3\n";
    return file.write(data) == data.size();
}
int fail(const char *message) {
    std::cerr << message << '\n';
    return 1;
}
bool writeLargeCloud(const QString &path, int count) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray header = QByteArray("ply\nformat ascii 1.0\nelement vertex ")
        + QByteArray::number(count)
        + "\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
    if (file.write(header) != header.size()) return false;
    for (int i = 0; i < count; ++i) {
        if (file.write("1.0 2.0 3.0\n") != 12) return false;
    }
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary directory creation failed");
    const QString source = directory.filePath(QStringLiteral("source.ply"));
    const QString cache = directory.filePath(QStringLiteral("cache"));
    if (!writeCloud(source, 1.0f)) return fail("fixture write failed");

    const pcv::detail::io::CachedCloudResult first = pcv::detail::io::readPlyCached(source, cache);
    if (!first.ok || first.usedCache || first.points.size() != 1)
        return fail("first read did not parse source");
    const QString canonical = pcv::detail::io::canonicalPlyFilePath(source, cache);
    QFile canonicalFile(canonical);
    if (!canonicalFile.open(QIODevice::ReadOnly)) return fail("canonical PLY was not written");
    const QByteArray canonicalHeader = canonicalFile.read(256);
    if (!canonicalHeader.contains("format binary_little_endian 1.0")
        || !canonicalHeader.contains("property float nx"))
        return fail("canonical PLY header is not binary with normals");
    const pcv::detail::io::CachedCloudResult second = pcv::detail::io::readPlyCached(source, cache);
    if (!second.ok || !second.usedCache)
        return fail("second read did not use cache");

    const QString largeSource = directory.filePath(QStringLiteral("large_source.ply"));
    if (!writeLargeCloud(largeSource, 12000)) return fail("large fixture write failed");
    const auto largeFirst = pcv::detail::io::readPlyCached(largeSource, cache);
    if (!largeFirst.ok || largeFirst.usedCache) return fail("large first read did not parse source");
    const QDateTime originalTime = QFileInfo(largeSource).lastModified();
    QFile largeFile(largeSource);
    if (!largeFile.open(QIODevice::ReadWrite)) return fail("large fixture reopen failed");
    const QByteArray needle("1.0 2.0 3.0\n");
    const qint64 offset = largeFile.size() / 2;
    if (!largeFile.seek(offset)) return fail("large fixture seek failed");
    const QByteArray tail = largeFile.readAll();
    const int local = tail.indexOf(needle);
    if (local < 0 || !largeFile.seek(offset + local)
        || largeFile.write("9.0 2.0 3.0\n") != needle.size())
        return fail("large fixture same-size edit failed");
    if (!largeFile.setFileTime(originalTime, QFileDevice::FileModificationTime))
        return fail("large fixture mtime restore failed");
    largeFile.close();
    const auto largeSecond = pcv::detail::io::readPlyCached(largeSource, cache);
    if (!largeSecond.ok || largeSecond.usedCache)
        return fail("same-size same-mtime source edit incorrectly used cache");
    bool foundChangedPoint = false;
    for (const auto &point : largeSecond.points) {
        if (point.x == 9.0f) { foundChangedPoint = true; break; }
    }
    if (!foundChangedPoint) return fail("cache miss did not read modified source content");

    QVector<pointcloud::Point3D> binaryPoints{{1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f}};
    const QString binaryWithNormals = directory.filePath(QStringLiteral("with_normals.ply"));
    QString binaryError;
    if (!pcv::detail::io::writeBinaryPly(binaryWithNormals, binaryPoints, &binaryError)
        || !pcv::detail::io::readPly(binaryWithNormals).ok)
        return fail("binary PLY with normals write/read failed");
    const auto withNormals = pcv::detail::io::readPly(binaryWithNormals);
    if (withNormals.points.size() != 1 || withNormals.points[0].nz != 0.0f
        || withNormals.points[0].ny != 1.0f)
        return fail("binary PLY normals fields did not round trip");
    pcv::detail::io::BinaryPlyWriteOptions noNormals;
    noNormals.includeNormals = false;
    const QString binaryWithoutNormals = directory.filePath(QStringLiteral("without_normals.ply"));
    if (!pcv::detail::io::writeBinaryPly(binaryWithoutNormals, binaryPoints, &binaryError, noNormals))
        return fail("binary PLY without normals write failed");
    const auto withoutNormals = pcv::detail::io::readPly(binaryWithoutNormals);
    if (!withoutNormals.ok || withoutNormals.points.size() != 1
        || withoutNormals.points[0].x != 1.0f || withoutNormals.points[0].y != 2.0f
        || withoutNormals.points[0].z != 3.0f)
        return fail("binary PLY xyz fields did not round trip");

    return 0;
}
