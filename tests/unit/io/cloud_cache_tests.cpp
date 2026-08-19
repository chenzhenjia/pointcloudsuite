#include <pcv/io/cloud_cache.h>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

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
    const pcv::detail::io::CachedCloudResult second = pcv::detail::io::readPlyCached(source, cache);
    if (!second.ok || !second.usedCache)
        return fail("second read did not use cache");

    QThread::msleep(20);
    if (!writeCloud(source, 9.0f)) return fail("fixture update failed");
    const pcv::detail::io::CachedCloudResult changed = pcv::detail::io::readPlyCached(source, cache);
    if (!changed.ok || changed.usedCache || changed.points[0].x != 9.0f)
        return fail("changed source did not invalidate cache");
    return 0;
}
