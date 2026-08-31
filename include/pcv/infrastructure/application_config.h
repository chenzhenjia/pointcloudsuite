#pragma once

#include <QString>
#include <QStringList>

namespace pcv::config {

struct PointCloudViewDefaults {
    int pointSize = 1;
    int colorMode = 0;
    double overlay = 1.0;
    bool voxelEnabled = true;
    double voxelSizeMm = 0.25;
    bool statisticalEnabled = true;
    int meanK = 45;
    double stddevMultiplier = 1.3;
    double edgeGridSizeMm = 0.2;
    int edgeCloseRadius = 1;
    int edgeOpenRadius = 1;
    double planeAngleToleranceDeg = 1.0;
    double planeDistanceToleranceMm = 0.4;
    double imagePixelSizeMm = 0.05;
    double imageMarginMm = 50.0;
    double imageRoundIncrementMm = 10.0;
    qint64 maximumImagePixels = 100000000;
};

struct ApplicationConfig {
    QString configFilePath;
    QString dataDirectory = QStringLiteral("D:/Scraping_Robot_Project");
    PointCloudViewDefaults pointcloudview;
    QStringList warnings;
    bool loadedFromFile = false;
    bool templateCreated = false;
};

QString defaultDataDirectory();
QString defaultConfigFilePath();
QString resolveConfigPath(const QStringList &arguments, const QString &applicationDirectory = {});
ApplicationConfig loadApplicationConfig(const QStringList &arguments,
                                        const QString &applicationDirectory = {});

} // namespace pcv::config
