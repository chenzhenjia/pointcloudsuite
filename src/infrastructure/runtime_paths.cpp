#include <pcv/infrastructure/runtime_paths.h>
#include <pcv/infrastructure/application_config.h>

#include <QDir>
#include <QStandardPaths>

namespace {
QString configuredDataDirectory;

QString ensureDirectory(const QString &path) {
    if (!path.isEmpty())
        QDir().mkpath(path);
    return QDir::cleanPath(path);
}
}

namespace pcv::runtime {

void configureDataDirectory(const QString &path) {
    configuredDataDirectory = path.trimmed().isEmpty()
        ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString dataDirectory() {
    return configuredDataDirectory.isEmpty()
        ? pcv::config::defaultDataDirectory() : configuredDataDirectory;
}

QString applicationDataDirectory() {
    return ensureDirectory(dataDirectory());
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
