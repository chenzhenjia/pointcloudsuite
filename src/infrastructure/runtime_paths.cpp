#include <pcv/infrastructure/runtime_paths.h>

#include <QDir>
#include <QStandardPaths>

namespace {
QString ensureDirectory(const QString &path) {
    if (!path.isEmpty())
        QDir().mkpath(path);
    return QDir::cleanPath(path);
}
}

namespace pcv::runtime {

QString applicationDataDirectory() {
    return ensureDirectory(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

QString cacheDirectory() {
    return ensureDirectory(QDir(applicationDataDirectory()).filePath(QStringLiteral("cache")));
}

QString logDirectory() {
    return ensureDirectory(QDir(applicationDataDirectory()).filePath(QStringLiteral("logs")));
}

QString exportDirectory() {
    return ensureDirectory(QDir(applicationDataDirectory()).filePath(QStringLiteral("exports")));
}

} // namespace pcv::runtime
