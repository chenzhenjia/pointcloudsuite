#include <pcv/matching/roi_matching.h>

#include <QCoreApplication>

#include <limits>
#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

pcv::matching::RoiMatchInput validInput()
{
    pcv::matching::RoiMatchInput input;
    input.sourcePoints = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {2.0f, 1.0f, 0.5f},
        {3.0f, 2.0f, 0.25f},
    };
    input.planeIndices = {0, 1, 2};
    input.roiIndices = {4, 2, 3};
    input.planeModel = QVector4D(0.0f, 0.0f, 1.0f, 0.0f);
    input.calibration.flangeFromDepth.setToIdentity();
    input.calibration.valid = true;
    input.startBaseFromFlange.setToIdentity();
    input.endBaseFromFlange.setToIdentity();
    input.transformOptions.layout = pointcloud::DepthPointLayout::FullXyz;
    input.transformOptions.sampleStride = 1;
    return input;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    {
        const auto result = pcv::matching::matchRoiToRobotBase(validInput());
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        if (!require(result.matchedPointCount == 3 && result.coverage == 1.0f,
                     "ROI matching did not return the complete ROI")) return 1;
        if (!require(result.sourceIndices == QVector<qsizetype>({4, 2, 3}),
                     "ROI source order was not preserved")) return 1;
        if (!require(result.robotBasePoints[0].x == 3.0f
                     && result.robotBasePoints[1].x == 0.0f
                     && result.robotBasePoints[2].x == 2.0f,
                     "ROI robot_base point order was not preserved")) return 1;
    }

    {
        auto input = validInput();
        input.roiIndices = {4, 4};
        const auto result = pcv::matching::matchRoiToRobotBase(input);
        if (!require(!result.ok && result.error.contains(QStringLiteral("重复")),
                     "duplicate ROI indices were accepted")) return 1;
    }

    {
        auto input = validInput();
        input.roiIndices = {4, 99};
        const auto result = pcv::matching::matchRoiToRobotBase(input);
        if (!require(!result.ok && result.error.contains(QStringLiteral("越界")),
                     "out-of-range ROI indices were accepted")) return 1;
    }

    {
        auto input = validInput();
        input.sourcePoints[4].x = std::numeric_limits<float>::quiet_NaN();
        const auto result = pcv::matching::matchRoiToRobotBase(input);
        if (!require(!result.ok && result.missingPointCount == 1
                     && result.robotBasePoints.isEmpty(),
                     "incomplete ROI conversion was published")) return 1;
    }

    std::cout << "ROI matching tests passed.\n";
    return 0;
}
