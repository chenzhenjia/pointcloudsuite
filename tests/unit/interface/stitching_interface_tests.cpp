#include <pcv/interface/stitching_interface.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

bool isUnchanged(const QString &path, const QByteArray &expected)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll() == expected;
}

pcv::interface::StitchingOptions validShape(const QString &root)
{
    pcv::interface::StitchingOptions options;
    options.calibrationPath = QDir(root).filePath(QStringLiteral("handeye.xml"));
    options.outputDirectory = QDir(root).filePath(QStringLiteral("output"));
    options.frames = {
        {QDir(root).filePath(QStringLiteral("first.ply")), {}, {}},
        {QDir(root).filePath(QStringLiteral("second.ply")), {}, {}}
    };
    return options;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory creation failed")) return 1;
    const QString outputDirectory = QDir(temporary.path()).filePath(QStringLiteral("output"));
    if (!require(QDir().mkpath(outputDirectory), "output directory creation failed")) return 1;
    const QString outputPly = QDir(outputDirectory).filePath(QStringLiteral("stitched_robot_base.ply"));
    const QByteArray oldOutput("existing formal output must survive failures\n");
    if (!require(writeFile(outputPly, oldOutput), "old formal output creation failed")) return 1;
    const QByteArray calibration = R"(<ArithConfig><RTmatDepth2robot>
        <RotMat r00="1" r01="0" r02="0" r10="0" r11="1" r12="0" r20="0" r21="0" r22="1"/>
        <TVec t0="0" t1="0" t2="0"/>
        </RTmatDepth2robot></ArithConfig>)";
    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("handeye.xml")), calibration),
                 "calibration creation failed")) return 1;

    auto missingCalibration = validShape(temporary.path());
    missingCalibration.calibrationPath = QDir(temporary.path()).filePath(QStringLiteral("missing.xml"));
    const auto missingResult = pcv::interface::stitchRawLineProfiles(missingCalibration);
    if (!require(!missingResult.success && !missingResult.cancelled
                 && missingResult.errorCode == QStringLiteral("PCV_INPUT_001"),
                 "missing calibration must be rejected")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "missing calibration replaced formal output")) return 1;

    auto disabledSeam = validShape(temporary.path());
    disabledSeam.seamEnabled = true;
    const auto disabledSeamResult = pcv::interface::stitchRawLineProfiles(disabledSeam);
    if (!require(!disabledSeamResult.success && !disabledSeamResult.cancelled
                 && disabledSeamResult.errorCode == QStringLiteral("PCV_STITCH_001"),
                 "temporarily disabled seam must be rejected")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "disabled seam replaced formal output")) return 1;

    auto cancelled = validShape(temporary.path());
    cancelled.isCancelled = [] { return true; };
    const auto cancelledResult = pcv::interface::stitchRawLineProfiles(cancelled);
    if (!require(!cancelledResult.success && cancelledResult.cancelled,
                 "cancelled operation must return cancelled")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "cancelled operation replaced formal output")) return 1;

    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("first.ply")), QByteArray("not a ply")),
                 "invalid PLY creation failed")) return 1;
    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("second.ply")), QByteArray("not a ply")),
                 "invalid PLY creation failed")) return 1;
    const auto invalidInput = pcv::interface::stitchRawLineProfiles(validShape(temporary.path()));
    if (!require(!invalidInput.success && !invalidInput.cancelled
                 && invalidInput.errorCode == QStringLiteral("PCV_INPUT_002"),
                 "invalid PLY must be rejected")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "input failure replaced formal output")) return 1;

    std::cout << "Stitching interface failure-safety tests passed.\n";
    return 0;
}
