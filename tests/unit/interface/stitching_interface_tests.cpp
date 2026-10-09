#include <pcv/interface/stitching_interface.h>
#include <pcv/io/ply_reader.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
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

    auto seamWithInvalidInput = validShape(temporary.path());
    seamWithInvalidInput.seamEnabled = true;
    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("first.ply")),
                           QByteArray("not a ply")),
                 "invalid PLY creation failed")) return 1;
    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("second.ply")),
                           QByteArray("not a ply")),
                 "invalid PLY creation failed")) return 1;
    const auto seamWithInvalidInputResult =
        pcv::interface::stitchRawLineProfiles(seamWithInvalidInput);
    if (!require(!seamWithInvalidInputResult.success && !seamWithInvalidInputResult.cancelled
                 && seamWithInvalidInputResult.errorCode == QStringLiteral("PCV_INPUT_002"),
                 "enabled seam must still reject invalid PLY input")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "seam input failure replaced formal output")) return 1;

    auto cancelled = validShape(temporary.path());
    cancelled.isCancelled = [] { return true; };
    const auto cancelledResult = pcv::interface::stitchRawLineProfiles(cancelled);
    if (!require(!cancelledResult.success && cancelledResult.cancelled,
                 "cancelled operation must return cancelled")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "cancelled operation replaced formal output")) return 1;

    const auto invalidInput = pcv::interface::stitchRawLineProfiles(validShape(temporary.path()));
    if (!require(!invalidInput.success && !invalidInput.cancelled
                 && invalidInput.errorCode == QStringLiteral("PCV_INPUT_002"),
                 "invalid PLY must be rejected")) return 1;
    if (!require(isUnchanged(outputPly, oldOutput), "input failure replaced formal output")) return 1;

    QByteArray ply("ply\nformat ascii 1.0\nelement vertex 900\nproperty float x\nproperty float y\nproperty float z\nend_header\n");
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x)
            ply += QByteArray::number(x) + " " + QByteArray::number(y) + " 100\n";
    if (!require(writeFile(QDir(temporary.path()).filePath(QStringLiteral("first.ply")), ply)
        && writeFile(QDir(temporary.path()).filePath(QStringLiteral("second.ply")), ply), "valid input creation failed")) return 1;
    pcv::interface::StitchingRequest request;
    request.input.calibrationPath = QDir(temporary.path()).filePath(QStringLiteral("handeye.xml"));
    request.input.frames = {
        {QDir(temporary.path()).filePath(QStringLiteral("first.ply")), {0, 0, 0, 0, 0, 0}, {0, 30, 0, 0, 0, 0}},
        {QDir(temporary.path()).filePath(QStringLiteral("second.ply")), {0, 0, 0.2, 0, 0, 0}, {0, 30, 0.2, 0, 0, 0}}
    };
    request.registration.options.profile = pointcloud::RegistrationProfile::PoseOverlapFine;
    request.registration.sampleStride = 1;
    request.registration.parallelEnabled = false;
    request.artifacts.outputDirectory = QDir(temporary.path()).filePath(QStringLiteral("fine_output"));
    // Keep the compatible default true: the fast profile must suppress preview
    // at the module boundary, even when a caller forgets to disable it.
    request.seam.enabled = true; // fast profile must suppress even an explicit seam request
    const auto refined = pcv::interface::stitch(request);
    if (!refined.success || refined.icpDiagnostics.size() < 2 || !refined.icpDiagnostics.at(1).fineApplied)
        std::cerr << "fine interface: " << refined.message.toStdString() << "\n" << refined.diagnostics.toStdString() << '\n';
    if (!require(refined.success && refined.points.size() == 1800 && refined.icpDiagnostics.at(1).fineApplied
        && refined.seamDiagnostics.isEmpty() && refined.manifest.previewPly.isEmpty()
        && !QFileInfo::exists(QDir(request.artifacts.outputDirectory).filePath(QStringLiteral("stitched_robot_base_preview.ply"))),
        "fine mode must publish full points without seam/preview despite quality degeneracy")) return 1;
    QFile manifest(refined.manifest.reportJson);
    if (!require(manifest.open(QIODevice::ReadOnly), "manifest not readable")) return 1;
    const auto json = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    const auto pair = json.value(QStringLiteral("registration_pairs")).toArray().at(0).toObject();
    if (!require(json.value(QStringLiteral("processing_mode")).toString() == QStringLiteral("pose_overlap_fine")
        && json.value(QStringLiteral("strict_validation_status")).toString() == QStringLiteral("not_run")
        && json.value(QStringLiteral("fine_applied_count")).toInt() == 1
        && pair.value(QStringLiteral("fine_status")).toString() == QStringLiteral("fine_applied")
        && !pair.value(QStringLiteral("accepted")).toBool()
        && pair.value(QStringLiteral("structural_metrics_status")).toString() == QStringLiteral("not_computed"),
        "formal output must not imply strict quality acceptance")) return 1;
    const auto read = pcv::detail::io::readPly(refined.outputPly);
    if (!require(read.ok && read.points.size() == 1800 && QFileInfo::exists(refined.manifest.mappingFile),
        "formal PLY/mapping must be committed")) return 1;
    // Failed overwrite and cancellation cannot damage any member of the set.
    const auto forbidden = pcv::interface::stitch(request);
    if (!require(!forbidden.success && pcv::detail::io::readPly(refined.outputPly).points.size() == 1800,
        "unapproved overwrite must preserve formal outputs")) return 1;
    request.artifacts.allowOverwrite = true;
    request.isCancelled = [] { return true; };
    if (!require(pcv::interface::stitch(request).cancelled
        && pcv::detail::io::readPly(refined.outputPly).points.size() == 1800, "cancel must preserve output")) return 1;
    request.isCancelled = {};
    request.input.frames[1].startPose.x = 1000;
    request.input.frames[1].endPose.x = 1000;
    const auto fallback = pcv::interface::stitch(request);
    if (!fallback.success) std::cerr << "fallback interface: " << fallback.message.toStdString() << '\n';
    if (!require(fallback.success && fallback.icpDiagnostics.at(1).poseFallback && fallback.points.size() == 1800,
        "no overlap must publish robot pose with a fallback diagnostic")) return 1;
    std::cout << "Stitching interface failure-safety tests passed.\n";
    return 0;
}
