#include <pcv/infrastructure/application_config.h>
#include <pcv/infrastructure/runtime_paths.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>

namespace {

void writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    assert(file.write(bytes) == bytes.size());
}

void testCommandLinePrecedence()
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString commandConfig = directory.filePath(QStringLiteral("command.json"));
    const QString environmentConfig = directory.filePath(QStringLiteral("environment.json"));
    writeJson(commandConfig, {{QStringLiteral("schema_version"), QStringLiteral("pointcloudsuite-config-v1")},
                              {QStringLiteral("data_directory"), directory.filePath(QStringLiteral("command_data"))}});
    writeJson(environmentConfig, {{QStringLiteral("schema_version"), QStringLiteral("pointcloudsuite-config-v1")},
                                  {QStringLiteral("data_directory"), directory.filePath(QStringLiteral("environment_data"))}});
    qputenv("PCV_CONFIG_FILE", environmentConfig.toUtf8());
    const auto config = pcv::config::loadApplicationConfig(
        {QStringLiteral("pointcloudview"), QStringLiteral("--config"), commandConfig}, directory.path());
    qunsetenv("PCV_CONFIG_FILE");
    assert(config.loadedFromFile);
    assert(config.dataDirectory == QDir::cleanPath(directory.filePath(QStringLiteral("command_data"))));
}

void testFieldFallback()
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString configPath = directory.filePath(QStringLiteral("invalid.json"));
    writeJson(configPath, {{QStringLiteral("schema_version"), QStringLiteral("pointcloudsuite-config-v1")},
                           {QStringLiteral("data_directory"), QStringLiteral("relative")},
                           {QStringLiteral("pointcloudview"), QJsonObject{{QStringLiteral("noise"),
                            QJsonObject{{QStringLiteral("mean_k"), 999},
                                        {QStringLiteral("voxel_enabled"), false}}}}}});
    const auto config = pcv::config::loadApplicationConfig(
        {QStringLiteral("pointcloudview"), QStringLiteral("--config=") + configPath}, directory.path());
    assert(config.loadedFromFile);
    assert(config.dataDirectory == pcv::config::defaultDataDirectory());
    assert(config.pointcloudview.meanK == 45);
    assert(!config.pointcloudview.voxelEnabled);
    assert(!config.warnings.isEmpty());
}

void testRuntimeDirectory()
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString root = directory.filePath(QStringLiteral("runtime_root"));
    pcv::runtime::configureDataDirectory(root);
    assert(pcv::runtime::applicationDataDirectory() == QDir::cleanPath(root));
    assert(pcv::runtime::cacheDirectory() == QDir(root).filePath(QStringLiteral("cache")));
    assert(QFileInfo::exists(pcv::runtime::logDirectory()));
    pcv::runtime::configureDataDirectory({});
}

} // namespace

int main()
{
    testCommandLinePrecedence();
    testFieldFallback();
    testRuntimeDirectory();
    return 0;
}
