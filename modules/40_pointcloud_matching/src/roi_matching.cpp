#include <pcv/matching/roi_matching.h>

#include <cmath>

namespace pcv::matching {
namespace {

bool finitePoint(const pointcloud::Point3D &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)
        && std::isfinite(point.nx) && std::isfinite(point.ny) && std::isfinite(point.nz);
}

bool finitePlane(const QVector4D &plane)
{
    return std::isfinite(plane.x()) && std::isfinite(plane.y())
        && std::isfinite(plane.z()) && std::isfinite(plane.w())
        && QVector3D(plane.x(), plane.y(), plane.z()).lengthSquared() > 1.0e-12f;
}

RoiMatchResult failure(const QString &message)
{
    RoiMatchResult result;
    result.error = message;
    return result;
}

} // namespace

RoiMatchResult matchRoiToRobotBase(const RoiMatchInput &input)
{
    RoiMatchResult result;
    result.inputPointCount = input.sourcePoints.size();
    result.requestedPointCount = input.roiIndices.size();

    if (input.sourcePoints.isEmpty()) return failure(QStringLiteral("ROI 匹配输入点云为空"));
    if (input.planeIndices.size() < 3)
        return failure(QStringLiteral("ROI 匹配需要至少三个平面点索引"));
    if (input.roiIndices.isEmpty()) return failure(QStringLiteral("ROI 点索引为空"));
    if (!finitePlane(input.planeModel))
        return failure(QStringLiteral("ROI 匹配平面模型无效"));

    QVector<char> planeSeen(input.sourcePoints.size(), 0);
    for (const int index : input.planeIndices) {
        if (index < 0 || index >= input.sourcePoints.size())
            return failure(QStringLiteral("平面点索引越界"));
        if (planeSeen[index]) return failure(QStringLiteral("平面点索引重复"));
        planeSeen[index] = 1;
        if (!finitePoint(input.sourcePoints[index]))
            return failure(QStringLiteral("平面点包含非有限坐标"));
    }

    QVector<char> roiSeen(input.sourcePoints.size(), 0);
    for (const int index : input.roiIndices) {
        if (index < 0 || index >= input.sourcePoints.size())
            return failure(QStringLiteral("ROI 点索引越界"));
        if (roiSeen[index]) return failure(QStringLiteral("ROI 点索引重复"));
        roiSeen[index] = 1;
    }

    pointcloud::CloudTransformOptions transformOptions = input.transformOptions;
    const auto callerCancelled = transformOptions.isCancelled;
    transformOptions.isCancelled = [cancelled = input.isCancelled,
                                     callerCancelled]() {
        return (cancelled && cancelled()) || (callerCancelled && callerCancelled());
    };
    const pointcloud::RobotCloudResult transformed =
        pointcloud::transformLineScanToRobotBase(
            input.sourcePoints, input.calibration,
            input.startBaseFromFlange, input.endBaseFromFlange,
            transformOptions);
    result.rejectedInvalidPointCount = transformed.rejectedInvalid;
    result.rejectedRangePointCount = transformed.rejectedRange;
    if (transformed.cancelled) {
        result.cancelled = true;
        result.error = QStringLiteral("已取消");
        return result;
    }
    if (!transformed.ok) {
        result.error = transformed.error;
        return result;
    }

    QVector<qsizetype> convertedLookup(input.sourcePoints.size(), -1);
    for (qsizetype convertedIndex = 0;
         convertedIndex < transformed.sourceIndices.size(); ++convertedIndex) {
        const qsizetype sourceIndex = transformed.sourceIndices[convertedIndex];
        if (sourceIndex < 0 || sourceIndex >= convertedLookup.size()) {
            return failure(QStringLiteral("坐标转换返回了无效源点索引"));
        }
        convertedLookup[sourceIndex] = convertedIndex;
    }

    result.robotBasePoints.reserve(input.roiIndices.size());
    result.sourceIndices.reserve(input.roiIndices.size());
    for (const int sourceIndex : input.roiIndices) {
        const qsizetype convertedIndex = convertedLookup[sourceIndex];
        if (convertedIndex < 0) {
            ++result.missingPointCount;
            continue;
        }
        const pointcloud::Point3D &point = transformed.points[convertedIndex];
        if (!finitePoint(point)) {
            ++result.missingPointCount;
            continue;
        }
        result.robotBasePoints.push_back(point);
        result.sourceIndices.push_back(sourceIndex);
    }

    result.matchedPointCount = result.robotBasePoints.size();
    result.coverage = result.requestedPointCount > 0
        ? float(result.matchedPointCount) / float(result.requestedPointCount) : 0.0f;
    if (result.missingPointCount > 0
        || result.matchedPointCount != result.requestedPointCount) {
        result.error = QStringLiteral("ROI 点未能完整回传 robot_base（%1/%2）")
            .arg(result.matchedPointCount).arg(result.requestedPointCount);
        result.robotBasePoints.clear();
        result.sourceIndices.clear();
        result.matchedPointCount = 0;
        result.coverage = 0.0f;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace pcv::matching
