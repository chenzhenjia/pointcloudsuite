#include <pcv/camera/intensity_image.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool require(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pcv::camera::CameraFrame frame;
    frame.width = 3;
    frame.height = 2;
    frame.frameNumber = 9;
    frame.timeStamp = 1234;
    frame.valid = true;
    frame.completeScan = true;
    frame.intensityData = QByteArray::fromHex("000102a0b0c0");

    const auto extracted = pcv::camera::extractIntensityImage(frame);
    if (!require(extracted.ok && extracted.width == 3 && extracted.height == 2
                     && extracted.frameNumber == 9
                     && extracted.mono8 == frame.intensityData,
                 "intensity image extraction contract failed")) return 1;

    const QImage image(reinterpret_cast<const uchar *>(extracted.mono8.constData()),
                       int(extracted.width), int(extracted.height), int(extracted.width),
                       QImage::Format_Grayscale8);
    if (!require(image.format() == QImage::Format_Grayscale8,
                 "source PNG image must use Grayscale8")) return 1;
    QTemporaryDir directory;
    if (!require(directory.isValid(), "temporary directory creation failed")) return 1;
    const QString path = QDir(directory.path()).filePath(QStringLiteral("intensity.png"));
    if (!require(image.save(path, "PNG"), "PNG save failed")) return 1;
    const QImage roundTrip(path);
    if (!require(!roundTrip.isNull()
                     && roundTrip.size() == QSize(3, 2)
                     && qGray(roundTrip.pixel(0, 0)) == 0
                     && qGray(roundTrip.pixel(0, 1)) == 0xa0,
                 "PNG round-trip contract failed")) return 1;

    std::cout << "intensity_image_tests: PASS\n";
    return 0;
}
