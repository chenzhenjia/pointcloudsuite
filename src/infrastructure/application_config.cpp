#include <pcv/infrastructure/application_config.h>

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace {

constexpr auto kSchemaVersion = "pointcloudsuite-config-v1";

QString cleanAbsolutePath(const QString &path)
{
    if (path.trimmed().isEmpty() || !QDir::isAbsolutePath(path)) return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

void warn(QStringList *warnings, const QString &field, const QString &reason)
{
    if (warnings) warnings->push_back(QStringLiteral("%1: %2; using the built-in default")
                                      .arg(field, reason));
}

bool readBool(const QJsonObject &object, const QString &key, bool *value,
              QStringList *warnings, const QString &field)
{
    const QJsonValue json = object.value(key);
    if (json.isUndefined()) return false;
    if (!json.isBool()) {
        warn(warnings, field, QStringLiteral("must be a boolean"));
        return false;
    }
    *value = json.toBool();
    return true;
}

template <typename T>
bool readNumber(const QJsonObject &object, const QString &key, T *value, T minimum,
                T maximum, QStringList *warnings, const QString &field)
{
    const QJsonValue json = object.value(key);
    if (json.isUndefined()) return false;
    if (!json.isDouble()) {
        warn(warnings, field, QStringLiteral("must be a number"));
        return false;
    }
    const double parsed = json.toDouble();
    if (!std::isfinite(parsed) || parsed < static_cast<double>(minimum)
        || parsed > static_cast<double>(maximum)) {
        warn(warnings, field, QStringLiteral("is outside the supported range"));
        return false;
    }
    *value = static_cast<T>(parsed);
    return true;
}

QJsonObject templateObject()
{
    QJsonObject render{{QStringLiteral("point_size"), 1},
                       {QStringLiteral("color_mode"), 0},
                       {QStringLiteral("overlay"), 1.0}};
    QJsonObject noise{{QStringLiteral("voxel_enabled"), true},
                      {QStringLiteral("voxel_size_mm"), 0.25},
                      {QStringLiteral("statistical_enabled"), true},
                      {QStringLiteral("mean_k"), 45},
                      {QStringLiteral("stddev_multiplier"), 1.3}};
    QJsonObject edge{{QStringLiteral("grid_size_mm"), 0.2},
                     {QStringLiteral("close_radius"), 1},
                     {QStringLiteral("open_radius"), 1}};
    QJsonObject validation{{QStringLiteral("angle_tolerance_deg"), 1.0},
                           {QStringLiteral("distance_tolerance_mm"), 0.4}};
    QJsonObject image{{QStringLiteral("pixel_size_mm"), 0.05},
                      {QStringLiteral("margin_mm"), 50.0},
                      {QStringLiteral("round_increment_mm"), 10.0},
                      {QStringLiteral("maximum_image_pixels"), 100000000}};
    return {{QStringLiteral("schema_version"), QString::fromLatin1(kSchemaVersion)},
            {QStringLiteral("data_directory"), pcv::config::defaultDataDirectory()},
            {QStringLiteral("pointcloudview"),
             QJsonObject{{QStringLiteral("render"), render},
                         {QStringLiteral("noise"), noise},
                         {QStringLiteral("edge"), edge},
                         {QStringLiteral("plane_validation"), validation},
                         {QStringLiteral("plane_image"), image}}}};
}

bool createTemplate(const QString &filePath, QStringList *warnings)
{
    const QString parent = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(parent)) {
        warn(warnings, QStringLiteral("configuration"),
             QStringLiteral("could not create configuration directory"));
        return false;
    }
    QSaveFile file(filePath);
    const QByteArray data = QJsonDocument(templateObject()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        warn(warnings, QStringLiteral("configuration"),
             QStringLiteral("could not create default configuration file"));
        return false;
    }
    return true;
}

void applyObject(const QJsonObject &root, pcv::config::ApplicationConfig *config)
{
    const QJsonValue schema = root.value(QStringLiteral("schema_version"));
    if (!schema.isString() || schema.toString() != QString::fromLatin1(kSchemaVersion)) {
        warn(&config->warnings, QStringLiteral("schema_version"),
             QStringLiteral("must be %1").arg(QString::fromLatin1(kSchemaVersion)));
    }
    const QJsonValue directory = root.value(QStringLiteral("data_directory"));
    if (!directory.isUndefined()) {
        const QString clean = directory.isString() ? cleanAbsolutePath(directory.toString()) : QString();
        if (clean.isEmpty()) warn(&config->warnings, QStringLiteral("data_directory"),
                                  QStringLiteral("must be an absolute path"));
        else config->dataDirectory = clean;
    }

    const QJsonObject pointcloudview = root.value(QStringLiteral("pointcloudview")).toObject();
    if (pointcloudview.isEmpty()) return;
    const QJsonObject render = pointcloudview.value(QStringLiteral("render")).toObject();
    const QJsonObject noise = pointcloudview.value(QStringLiteral("noise")).toObject();
    const QJsonObject edge = pointcloudview.value(QStringLiteral("edge")).toObject();
    const QJsonObject validation = pointcloudview.value(QStringLiteral("plane_validation")).toObject();
    const QJsonObject image = pointcloudview.value(QStringLiteral("plane_image")).toObject();
    auto &defaults = config->pointcloudview;
    readNumber(render, QStringLiteral("point_size"), &defaults.pointSize, 1, 8, &config->warnings, QStringLiteral("pointcloudview.render.point_size"));
    readNumber(render, QStringLiteral("color_mode"), &defaults.colorMode, 0, 2, &config->warnings, QStringLiteral("pointcloudview.render.color_mode"));
    readNumber(render, QStringLiteral("overlay"), &defaults.overlay, 0.0, 1.0, &config->warnings, QStringLiteral("pointcloudview.render.overlay"));
    readBool(noise, QStringLiteral("voxel_enabled"), &defaults.voxelEnabled, &config->warnings, QStringLiteral("pointcloudview.noise.voxel_enabled"));
    readNumber(noise, QStringLiteral("voxel_size_mm"), &defaults.voxelSizeMm, 0.001, 1000.0, &config->warnings, QStringLiteral("pointcloudview.noise.voxel_size_mm"));
    readBool(noise, QStringLiteral("statistical_enabled"), &defaults.statisticalEnabled, &config->warnings, QStringLiteral("pointcloudview.noise.statistical_enabled"));
    readNumber(noise, QStringLiteral("mean_k"), &defaults.meanK, 1, 128, &config->warnings, QStringLiteral("pointcloudview.noise.mean_k"));
    readNumber(noise, QStringLiteral("stddev_multiplier"), &defaults.stddevMultiplier, 0.1, 5.0, &config->warnings, QStringLiteral("pointcloudview.noise.stddev_multiplier"));
    readNumber(edge, QStringLiteral("grid_size_mm"), &defaults.edgeGridSizeMm, 0.0001, 1000.0, &config->warnings, QStringLiteral("pointcloudview.edge.grid_size_mm"));
    readNumber(edge, QStringLiteral("close_radius"), &defaults.edgeCloseRadius, 0, 4, &config->warnings, QStringLiteral("pointcloudview.edge.close_radius"));
    readNumber(edge, QStringLiteral("open_radius"), &defaults.edgeOpenRadius, 0, 4, &config->warnings, QStringLiteral("pointcloudview.edge.open_radius"));
    readNumber(validation, QStringLiteral("angle_tolerance_deg"), &defaults.planeAngleToleranceDeg, 0.01, 45.0, &config->warnings, QStringLiteral("pointcloudview.plane_validation.angle_tolerance_deg"));
    readNumber(validation, QStringLiteral("distance_tolerance_mm"), &defaults.planeDistanceToleranceMm, 0.001, 100.0, &config->warnings, QStringLiteral("pointcloudview.plane_validation.distance_tolerance_mm"));
    readNumber(image, QStringLiteral("pixel_size_mm"), &defaults.imagePixelSizeMm, 0.001, 10.0, &config->warnings, QStringLiteral("pointcloudview.plane_image.pixel_size_mm"));
    readNumber(image, QStringLiteral("margin_mm"), &defaults.imageMarginMm, 0.0, 10000.0, &config->warnings, QStringLiteral("pointcloudview.plane_image.margin_mm"));
    readNumber(image, QStringLiteral("round_increment_mm"), &defaults.imageRoundIncrementMm, 0.001, 10000.0, &config->warnings, QStringLiteral("pointcloudview.plane_image.round_increment_mm"));
    readNumber(image, QStringLiteral("maximum_image_pixels"), &defaults.maximumImagePixels, qint64(1), qint64(1000000000), &config->warnings, QStringLiteral("pointcloudview.plane_image.maximum_image_pixels"));
}

} // namespace

namespace pcv::config {

QString defaultDataDirectory()
{
    return QStringLiteral("D:/Scraping_Robot_Project");
}

QString defaultConfigFilePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("pointcloudview.json"));
}

QString resolveConfigPath(const QStringList &arguments, const QString &applicationDirectory)
{
    for (int index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments[index];
        if (argument == QStringLiteral("--config") && index + 1 < arguments.size())
            return cleanAbsolutePath(arguments[index + 1]);
        if (argument.startsWith(QStringLiteral("--config=")))
            return cleanAbsolutePath(argument.mid(9));
    }
    const QString environmentPath = cleanAbsolutePath(qEnvironmentVariable("PCV_CONFIG_FILE"));
    if (!environmentPath.isEmpty()) return environmentPath;
    const QString directory = applicationDirectory.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath() : applicationDirectory;
    const QString adjacent = QDir(directory).filePath(QStringLiteral("config/pointcloudview.json"));
    if (QFileInfo::exists(adjacent)) return QDir::cleanPath(adjacent);
    return defaultConfigFilePath();
}

ApplicationConfig loadApplicationConfig(const QStringList &arguments, const QString &applicationDirectory)
{
    ApplicationConfig config;
    config.configFilePath = resolveConfigPath(arguments, applicationDirectory);
    if (config.configFilePath.isEmpty()) {
        config.configFilePath = defaultConfigFilePath();
        warn(&config.warnings, QStringLiteral("configuration"), QStringLiteral("configuration path is invalid"));
    }
    QFile file(config.configFilePath);
    if (!file.exists()) {
        const QString templatePath = defaultConfigFilePath();
        config.templateCreated = createTemplate(templatePath, &config.warnings);
        if (config.configFilePath == templatePath && config.templateCreated) {
            file.setFileName(templatePath);
        } else {
            warn(&config.warnings, QStringLiteral("configuration"),
                 QStringLiteral("file was not found: %1").arg(config.configFilePath));
            return config;
        }
    }
    if (!file.open(QIODevice::ReadOnly)) {
        warn(&config.warnings, QStringLiteral("configuration"),
             QStringLiteral("could not read %1").arg(config.configFilePath));
        return config;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        warn(&config.warnings, QStringLiteral("configuration"),
             QStringLiteral("invalid JSON: %1").arg(error.errorString()));
        return config;
    }
    config.loadedFromFile = true;
    applyObject(document.object(), &config);
    return config;
}

} // namespace pcv::config
