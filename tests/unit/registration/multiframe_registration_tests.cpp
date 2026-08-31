#include <pcv/registration/multiframe_registration.h>

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QStringList>

#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

pointcloud::RobotBaseFrame validFrame(const QString &sourceFile, float offsetX = 0.0f)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    frame.fullPoints.reserve(400);
    frame.samplePoints.reserve(400);
    frame.sourceIndices.reserve(400);
    frame.scanRatios.reserve(400);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const pointcloud::Point3D point{
                offsetX + float(x), float(y), float((x + y) % 3) * 0.25f};
            frame.fullPoints.push_back(point);
            frame.samplePoints.push_back(point);
            frame.sourceIndices.push_back(frame.sourceIndices.size());
            frame.scanRatios.push_back(0.5f);
        }
    }
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setToIdentity();
    return frame;
}

pointcloud::IcpOptions simpleIcpOptions()
{
    pointcloud::IcpOptions options;
    options.planePrealignmentEnabled = false;
    options.planeIdentityTrackingEnabled = false;
    options.structuralValidationEnabled = false;
    options.structuralIcpEnabled = false;
    options.tangentCoarseAlignmentEnabled = false;
    options.wideStructureDiagnosticEnabled = false;
    return options;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    {
        auto referenceOnly = validFrame(QStringLiteral("reference.ply"));
        auto second = validFrame(QStringLiteral("second.ply"));
        pointcloud::IcpOptions options = simpleIcpOptions();
        options.enabled = false;
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(referenceOnly), std::move(second)}, options);
        if (!require(result.ok, "reference-only registration must succeed")) return 1;
        if (!require(result.icpDiagnostics.size() == 2,
                     "reference-only registration must report both frames")) return 1;
        if (!require(result.icpDiagnostics[0].reason == QString::fromUtf8("参考点云"),
                     "reference diagnostic reason mismatch")) return 1;
    }

    {
        auto first = validFrame(QStringLiteral("first.ply"));
        auto second = validFrame(QStringLiteral("second.ply"));
        pointcloud::IcpOptions options = simpleIcpOptions();
        QStringList progressMessages;
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(first), std::move(second)}, options,
            [&progressMessages](float, const QString &message) {
                progressMessages.push_back(message);
            });
        if (!require(!progressMessages.isEmpty(),
                     "ICP registration must report progress before pair processing")) return 1;
        bool sawPairMessage = false;
        for (const QString &message : progressMessages) {
            if (message.contains(QString::fromUtf8("配准 scan 2 到相邻点云 scan 1"))) {
                sawPairMessage = true;
                break;
            }
        }
        if (!require(sawPairMessage, "ICP pair progress message was not emitted")) return 1;
        if (!require(result.icpDiagnostics.size() >= 2,
                     "ICP registration must report the reference and pair diagnostics")) return 1;
    }

    {
        const auto empty = pointcloud::registerRobotBaseFrames({});
        if (!require(!empty.ok && !empty.error.isEmpty(),
                     "empty registration input must be rejected")) return 1;
    }

    {
        auto malformed = validFrame(QStringLiteral("malformed.ply"));
        malformed.scanRatios.removeLast();
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(malformed), validFrame(QStringLiteral("second.ply"))});
        if (!require(!result.ok && result.error.contains(QStringLiteral("长度不一致")),
                     "malformed source mapping must be rejected")) return 1;
    }

    {
        pointcloud::IcpOptions options = simpleIcpOptions();
        options.enabled = false;
        options.isCancelled = [] { return true; };
        const auto result = pointcloud::registerRobotBaseFrames(
            {validFrame(QStringLiteral("cancelled.ply"))}, options);
        if (!require(!result.ok && result.cancelled,
                     "cancelled registration must report cancellation")) return 1;
    }

    std::cout << "Multiframe registration tests passed.\n";
    return 0;
}
