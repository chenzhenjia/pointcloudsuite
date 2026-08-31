#include <pcv/interface/stitching_interface.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

namespace {

pointcloud::RobotPose makePose(double x, double y, double z)
{
    pointcloud::RobotPose pose;
    pose.x = x;
    pose.y = y;
    pose.z = z;
    pose.rz = 180.0;
    return pose;
}

void printDiagnostics(const pcv::interface::StitchingResult &result,
                      const QString &label,
                      QTextStream &out)
{
    out << label << " success=" << result.success
        << " cancelled=" << result.cancelled
        << " points=" << result.points.size()
        << " error_code=" << result.errorCode
        << " message=" << result.message << "\n";
    if (!result.outputPly.isEmpty())
        out << "formal_output=" << result.outputPly << "\n";
    for (const auto &diagnostic : result.icpDiagnostics) {
        out << "icp file=" << diagnostic.filePath
            << " attempted=" << diagnostic.attempted
            << " accepted=" << diagnostic.accepted
            << " converged=" << diagnostic.converged
            << " correspondences=" << diagnostic.correspondences
            << " fitness=" << diagnostic.fitness
            << " rmse=" << diagnostic.rmse
            << " overlap=" << diagnostic.overlapRatio
            << " reason=" << diagnostic.reason << "\n";
    }
    for (const auto &diagnostic : result.seamDiagnostics) {
        out << "seam pair=" << diagnostic.cloudA + 1 << "-" << diagnostic.cloudB + 1
            << " applied=" << diagnostic.applied
            << " actual_overlap=" << diagnostic.actualOverlapValid
            << " band_points=" << diagnostic.bandPoints
            << " mutual_pairs=" << diagnostic.mutualPairs
            << " interpolated=" << diagnostic.interpolatedPoints
            << " unmatched_preserved=" << diagnostic.unmatchedPreserved
            << " reason=" << diagnostic.reason << "\n";
    }
    if (!result.diagnostics.isEmpty())
        out << result.diagnostics;
}

pcv::interface::StitchingResult runCase(const QString &inputDirectory,
                                        const QString &calibrationPath,
                                        const QString &outputDirectory,
                                        bool seamEnabled)
{
    pcv::interface::StitchingOptions options;
    options.calibrationPath = calibrationPath;
    options.outputDirectory = outputDirectory;
    options.sampleStride = 8;
    options.seamEnabled = seamEnabled;
    options.seamHalfWidthMm = 8.0f;
    const QStringList names = {
        QStringLiteral("Point_Cloud_A01.ply"),
        QStringLiteral("Point_Cloud_A02.ply"),
        QStringLiteral("Point_Cloud_A03.ply"),
        QStringLiteral("Point_Cloud_A04.ply")};
    const double x[] = {500.0, 600.0, 700.0, 800.0};
    for (int index = 0; index < names.size(); ++index) {
        pcv::interface::StitchingFrameInput frame;
        frame.plyPath = QDir(inputDirectory).filePath(names[index]);
        frame.startPose = makePose(x[index], 150.0, 700.0);
        frame.endPose = makePose(x[index], -150.0, 700.0);
        options.frames.push_back(std::move(frame));
    }
    return pcv::interface::stitchRawLineProfiles(options);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PointCloudSuite registration diagnostic"));
    parser.addHelpOption();
    const QCommandLineOption inputOption(
        QStringList{QStringLiteral("input-dir")},
        QStringLiteral("Directory containing Point_Cloud_A01.ply through Point_Cloud_A04.ply."),
        QStringLiteral("directory"));
    const QCommandLineOption calibrationOption(
        QStringList{QStringLiteral("calibration")},
        QStringLiteral("Eye-in-hand calibration XML."), QStringLiteral("xml"));
    const QCommandLineOption outputOption(
        QStringList{QStringLiteral("output-dir")},
        QStringLiteral("Directory for formal stitching output and diagnostics."),
        QStringLiteral("directory"));
    parser.addOption(inputOption);
    parser.addOption(calibrationOption);
    parser.addOption(outputOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (!parser.isSet(inputOption) || !parser.isSet(calibrationOption)
        || !parser.isSet(outputOption)) {
        err << "Usage: registration_diagnostic --input-dir <directory>"
            << " --calibration <xml> --output-dir <directory>\n";
        return 2;
    }
    const QString inputDirectory = QFileInfo(parser.value(inputOption)).absoluteFilePath();
    const QString calibrationPath = QFileInfo(parser.value(calibrationOption)).absoluteFilePath();
    const QString outputDirectory = QFileInfo(parser.value(outputOption)).absoluteFilePath();
    if (!QDir(inputDirectory).exists()) {
        err << "Input directory does not exist: " << inputDirectory << '\n';
        return 2;
    }
    if (!QFileInfo::exists(calibrationPath)) {
        err << "Calibration XML does not exist: " << calibrationPath << '\n';
        return 2;
    }
    if (!QDir().mkpath(outputDirectory)) {
        err << "Could not create output directory: " << outputDirectory << '\n';
        return 2;
    }

    const auto noSeam = runCase(inputDirectory, calibrationPath,
                                QDir(outputDirectory).filePath(QStringLiteral("no-seam")), false);
    printDiagnostics(noSeam, QStringLiteral("NO_SEAM"), out);
    const auto seam = runCase(inputDirectory, calibrationPath,
                              QDir(outputDirectory).filePath(QStringLiteral("seam")), true);
    printDiagnostics(seam, QStringLiteral("SEAM"), out);
    return noSeam.success && seam.success ? 0 : 2;
}

