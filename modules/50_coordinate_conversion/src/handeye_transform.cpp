#include <pcv/registration/handeye_transform.h>

#include <QFile>
#include <QElapsedTimer>
#include <QQuaternion>
#include <QVector3D>
#include <QXmlStreamReader>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <future>
#include <vector>
#include <utility>
#include <system_error>

namespace pointcloud {
namespace {

constexpr int RotationLutResolution = 4096;

bool finitePoint(const Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool finiteRotation(const Point3D &point) {
    return std::isfinite(point.nx) && std::isfinite(point.ny) && std::isfinite(point.nz);
}

bool nonzeroPoint(const Point3D &point) {
    return std::abs(point.x) > 1.0e-12f
        || std::abs(point.y) > 1.0e-12f
        || std::abs(point.z) > 1.0e-12f;
}

QVector3D translationOf(const QMatrix4x4 &matrix) {
    return {matrix(0, 3), matrix(1, 3), matrix(2, 3)};
}

QMatrix3x3 rotationOf(const QMatrix4x4 &matrix) {
    QMatrix3x3 result;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result(row, column) = matrix(row, column);
    return result;
}

bool sameRotation(const QMatrix4x4 &left, const QMatrix4x4 &right) {
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            if (std::abs(left(row, column) - right(row, column)) > 1.0e-5f)
                return false;
    return true;
}

float determinant3x3(const QMatrix4x4 &matrix) {
    return matrix(0, 0) * (matrix(1, 1) * matrix(2, 2) - matrix(1, 2) * matrix(2, 1))
        - matrix(0, 1) * (matrix(1, 0) * matrix(2, 2) - matrix(1, 2) * matrix(2, 0))
        + matrix(0, 2) * (matrix(1, 0) * matrix(2, 1) - matrix(1, 1) * matrix(2, 0));
}

QMatrix4x4 lerpTranslation(const QMatrix4x4 &start,
                           const QMatrix4x4 &end,
                           float ratio) {
    QMatrix4x4 result = start;
    const QVector3D translation = translationOf(start) * (1.0f - ratio)
        + translationOf(end) * ratio;
    result(0, 3) = translation.x();
    result(1, 3) = translation.y();
    result(2, 3) = translation.z();
    return result;
}

bool finiteVector3(const QVector3D &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

void setFailure(RobotCloudResult *result, RobotCloudStatus status, const QString &message) {
    result->status = status;
    result->error = message;
}

// Single mapping used by both the per-point entry point and the full-scan
// entry point: the position is transformed, the normal only rotated.
bool applyBaseFromDepth(const Point3D &depthPoint,
                        const QMatrix4x4 &baseFromDepth,
                        Point3D *result) {
    if (!result) return false;
    const QVector3D position = baseFromDepth.map(
        QVector3D(depthPoint.x, depthPoint.y, depthPoint.z));
    if (!finiteVector3(position)) return false;
    Point3D converted = depthPoint;
    converted.x = position.x();
    converted.y = position.y();
    converted.z = position.z();

    const QVector3D normal(depthPoint.nx, depthPoint.ny, depthPoint.nz);
    if (normal.lengthSquared() > 1.0e-12f) {
        // The normal uses the same rigid rotation but must never be translated.
        const QVector3D rotated = baseFromDepth.mapVector(normal).normalized();
        if (!finiteVector3(rotated)) return false;
        converted.nx = rotated.x();
        converted.ny = rotated.y();
        converted.nz = rotated.z();
    }
    *result = converted;
    return true;
}

} // namespace

QString robotCloudStatusCode(RobotCloudStatus status) {
    switch (status) {
    case RobotCloudStatus::Ok: return QStringLiteral("PCV_M50_OK");
    case RobotCloudStatus::InvalidCalibration: return QStringLiteral("PCV_M50_CALIBRATION_INVALID");
    case RobotCloudStatus::InvalidStartPose: return QStringLiteral("PCV_M50_START_POSE_INVALID");
    case RobotCloudStatus::InvalidEndPose: return QStringLiteral("PCV_M50_END_POSE_INVALID");
    case RobotCloudStatus::NoScanTravel: return QStringLiteral("PCV_M50_NO_SCAN_TRAVEL");
    case RobotCloudStatus::EmptyInput: return QStringLiteral("PCV_M50_EMPTY_INPUT");
    case RobotCloudStatus::ProgressOutOfRange: return QStringLiteral("PCV_M50_PROGRESS_OUT_OF_RANGE");
    case RobotCloudStatus::LineProfileContractViolation:
        return QStringLiteral("PCV_M50_LINE_PROFILE_CONTRACT");
    case RobotCloudStatus::Cancelled: return QStringLiteral("PCV_M50_CANCELLED");
    }
    return QStringLiteral("PCV_M50_UNKNOWN");
}

QString robotCloudStatusText(RobotCloudStatus status) {
    switch (status) {
    case RobotCloudStatus::Ok: return QStringLiteral("转换成功");
    case RobotCloudStatus::InvalidCalibration: return QStringLiteral("手眼标定矩阵无效");
    case RobotCloudStatus::InvalidStartPose: return QStringLiteral("Start 位姿无效");
    case RobotCloudStatus::InvalidEndPose: return QStringLiteral("End 位姿无效");
    case RobotCloudStatus::NoScanTravel: return QStringLiteral("Start/End 没有有效行程");
    case RobotCloudStatus::EmptyInput: return QStringLiteral("输入点云为空");
    case RobotCloudStatus::ProgressOutOfRange:
        return QStringLiteral("LineProfileXz 扫描进度超出允许范围");
    case RobotCloudStatus::LineProfileContractViolation:
        return QStringLiteral("输入点全部无效或不符合 LineProfileXz 契约");
    case RobotCloudStatus::Cancelled: return QStringLiteral("已取消");
    }
    return QStringLiteral("未知状态");
}

bool validateRigidTransform(const QMatrix4x4 &matrix, QString *error) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!std::isfinite(matrix(row, column))) {
                if (error) *error = QStringLiteral("变换矩阵包含非有限数");
                return false;
            }
        }
    }
    if (std::abs(matrix(3, 0)) > 1.0e-5f || std::abs(matrix(3, 1)) > 1.0e-5f
        || std::abs(matrix(3, 2)) > 1.0e-5f || std::abs(matrix(3, 3) - 1.0f) > 1.0e-5f) {
        if (error) *error = QStringLiteral("变换矩阵最后一行不是 [0,0,0,1]");
        return false;
    }
    for (int column = 0; column < 3; ++column) {
        float lengthSquared = 0.0f;
        for (int row = 0; row < 3; ++row)
            lengthSquared += matrix(row, column) * matrix(row, column);
        if (std::abs(lengthSquared - 1.0f) > 2.0e-3f) {
            if (error) *error = QStringLiteral("旋转矩阵列向量不是单位向量");
            return false;
        }
    }
    for (int first = 0; first < 3; ++first) {
        for (int second = first + 1; second < 3; ++second) {
            float dot = 0.0f;
            for (int row = 0; row < 3; ++row)
                dot += matrix(row, first) * matrix(row, second);
            if (std::abs(dot) > 2.0e-3f) {
                if (error) *error = QStringLiteral("旋转矩阵列向量不正交");
                return false;
            }
        }
    }
    if (std::abs(determinant3x3(matrix) - 1.0f) > 2.0e-3f) {
        if (error) *error = QStringLiteral("旋转矩阵行列式不接近 +1");
        return false;
    }
    return true;
}

bool loadHandEyeCalibration(const QString &xmlPath,
                            HandEyeCalibration *calibration,
                            QString *error) {
    if (!calibration) {
        if (error) *error = QStringLiteral("标定输出对象为空");
        return false;
    }
    *calibration = {};
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开标定文件：%1").arg(xmlPath);
        return false;
    }

    QXmlStreamReader xml(&file);
    bool insideDepthToRobot = false;
    QVector<double> rotation;
    QVector<double> translation;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QStringLiteral("RTmatDepth2robot")) {
            insideDepthToRobot = false;
            continue;
        }
        if (!xml.isStartElement()) continue;
        if (xml.name() == QStringLiteral("RTmatDepth2robot")) {
            insideDepthToRobot = true;
        } else if (insideDepthToRobot && xml.name() == QStringLiteral("RotMat")) {
            const auto attributes = xml.attributes();
            rotation.resize(9);
            bool valid = true;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    bool ok = false;
                    rotation[row * 3 + column] = attributes
                        .value(QStringLiteral("r%1%2").arg(row).arg(column)).toDouble(&ok);
                    valid = valid && ok;
                }
            }
            if (!valid) rotation.clear();
        } else if (insideDepthToRobot && xml.name() == QStringLiteral("TVec")) {
            translation.resize(3);
            bool valid = true;
            for (int index = 0; index < 3; ++index) {
                bool ok = false;
                translation[index] = xml.attributes()
                    .value(QStringLiteral("t%1").arg(index)).toDouble(&ok);
                valid = valid && ok;
            }
            if (!valid) translation.clear();
        }
    }
    if (xml.hasError() || rotation.size() != 9 || translation.size() != 3) {
        if (error) *error = xml.hasError() ? xml.errorString()
            : QStringLiteral("XML 中 RTmatDepth2robot/RotMat/TVec 不完整");
        return false;
    }

    QMatrix4x4 matrix;
    matrix.setToIdentity();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix(row, column) = float(rotation[row * 3 + column]);
        matrix(row, 3) = float(translation[row]);
    }
    QString validationError;
    if (!validateRigidTransform(matrix, &validationError)) {
        if (error) *error = QStringLiteral("RTmatDepth2robot 无效：%1").arg(validationError);
        return false;
    }
    // RTmatDepth2robot is T_flange_depth (flange_from_depth). Keep the parsed
    // direction: callers must never invert it by default.
    calibration->flangeFromDepth = matrix;
    calibration->sourceFile = xmlPath;
    calibration->valid = true;
    return true;
}

QMatrix4x4 robotPoseToMatrix(const RobotPose &pose) {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    // [X,Y,Z,A,B,C] with A=Rx, B=Ry, C=Rz and R = Rz(C)*Ry(B)*Rx(A).
    matrix.rotate(float(pose.rz), 0.0f, 0.0f, 1.0f);
    matrix.rotate(float(pose.ry), 0.0f, 1.0f, 0.0f);
    matrix.rotate(float(pose.rx), 1.0f, 0.0f, 0.0f);
    matrix(0, 3) = float(pose.x);
    matrix(1, 3) = float(pose.y);
    matrix(2, 3) = float(pose.z);
    return matrix;
}

RobotPose matrixToRobotPose(const QMatrix4x4 &matrix) {
    RobotPose pose;
    pose.x = matrix(0, 3);
    pose.y = matrix(1, 3);
    pose.z = matrix(2, 3);
    const double ry = std::asin(std::clamp(-double(matrix(2, 0)), -1.0, 1.0));
    const double cosRy = std::cos(ry);
    double rx = 0.0;
    double rz = 0.0;
    if (std::abs(cosRy) > 1.0e-8) {
        rx = std::atan2(double(matrix(2, 1)), double(matrix(2, 2)));
        rz = std::atan2(double(matrix(1, 0)), double(matrix(0, 0)));
    } else {
        rz = std::atan2(-double(matrix(0, 1)), double(matrix(1, 1)));
    }
    pose.rx = qRadiansToDegrees(rx);
    pose.ry = qRadiansToDegrees(ry);
    pose.rz = qRadiansToDegrees(rz);
    return pose;
}

QMatrix4x4 interpolateRobotTransform(const QMatrix4x4 &start,
                                     const QMatrix4x4 &end,
                                     float ratio) {
    ratio = qBound(0.0f, ratio, 1.0f);
    QQuaternion startRotation = QQuaternion::fromRotationMatrix(rotationOf(start)).normalized();
    QQuaternion endRotation = QQuaternion::fromRotationMatrix(rotationOf(end)).normalized();
    const QQuaternion rotation = QQuaternion::slerp(startRotation, endRotation, ratio).normalized();
    const QVector3D translation = translationOf(start) * (1.0f - ratio)
        + translationOf(end) * ratio;
    QMatrix4x4 result(rotation.toRotationMatrix());
    result(0, 3) = translation.x();
    result(1, 3) = translation.y();
    result(2, 3) = translation.z();
    result(3, 3) = 1.0f;
    return result;
}

ScanTravelInfo computeScanTravel(const QMatrix4x4 &startBaseFromFlange,
                                 const QMatrix4x4 &endBaseFromFlange) {
    ScanTravelInfo info;
    const QVector3D start = translationOf(startBaseFromFlange);
    const QVector3D end = translationOf(endBaseFromFlange);
    if (!finiteVector3(start) || !finiteVector3(end)) return info;
    const QVector3D delta = end - start;
    int dominantAxis = 0;
    if (std::abs(delta.y()) > std::abs(delta.x())) dominantAxis = 1;
    const float dominantValue = dominantAxis == 0 ? delta.x() : delta.y();
    if (std::abs(delta.z()) > std::abs(dominantValue)) dominantAxis = 2;
    info.dominantAxis = dominantAxis;
    info.signedTravel = dominantAxis == 0 ? delta.x()
        : dominantAxis == 1 ? delta.y() : delta.z();
    // A negative signed travel is a valid reverse scan; only a degenerate
    // Start/End pair is invalid.
    info.valid = std::abs(info.signedTravel) > 1.0e-9f;
    return info;
}

QMatrix4x4 composeBaseFromDepth(const QMatrix4x4 &baseFromFlange,
                                const HandEyeCalibration &calibration) {
    // T_base_depth = T_base_flange * T_flange_depth. Never invert the parsed
    // hand-eye matrix and never append a Z flip / mirror / 180 deg patch.
    return baseFromFlange * calibration.flangeFromDepth;
}

Point3D lineProfileCameraPoint(const Point3D &input) {
    // LineProfileXz: only [X,0,Z] enters the rigid transform. The input Y is
    // the scan-progress coordinate consumed by lineProfileScanRatio().
    Point3D result = input;
    result.y = 0.0f;
    return result;
}

float lineProfileScanRatio(float inputY, float signedTravel) {
    if (!std::isfinite(inputY) || !std::isfinite(signedTravel)
        || std::abs(signedTravel) <= 1.0e-9f) {
        return 0.0f;
    }
    return inputY / signedTravel;
}

Point3D transformPointToRobotBase(const Point3D &depthPoint,
                                  const QMatrix4x4 &baseFromFlange,
                                  const HandEyeCalibration &calibration,
                                  QString *error) {
    if (!finitePoint(depthPoint) || !finiteRotation(depthPoint)) {
        if (error) *error = QStringLiteral("输入点包含非有限数值");
        return depthPoint;
    }
    QString matrixError;
    if (!validateRigidTransform(baseFromFlange, &matrixError)) {
        if (error) *error = QStringLiteral("Start/End 位姿矩阵无效：%1").arg(matrixError);
        return depthPoint;
    }
    if (!calibration.valid) {
        if (error) *error = QStringLiteral("手眼标定未加载有效的 RTmatDepth2robot");
        return depthPoint;
    }
    if (!validateRigidTransform(calibration.flangeFromDepth, &matrixError)) {
        if (error) *error = QStringLiteral("手眼标定矩阵无效：%1").arg(matrixError);
        return depthPoint;
    }

    const QMatrix4x4 baseFromDepth = composeBaseFromDepth(baseFromFlange, calibration);
    Point3D result;
    if (!applyBaseFromDepth(depthPoint, baseFromDepth, &result)) {
        if (error) *error = QStringLiteral("变换后的点或法向不是有限值");
        return depthPoint;
    }
    return result;
}

RobotCloudResult transformLineScanToRobotBase(
    const QVector<Point3D> &source,
    const HandEyeCalibration &calibration,
    const QMatrix4x4 &startBaseFromFlange,
    const QMatrix4x4 &endBaseFromFlange,
    const CloudTransformOptions &options) {
    RobotCloudResult result;
    QString validationError;
    if (!calibration.valid) {
        setFailure(&result, RobotCloudStatus::InvalidCalibration,
                   QStringLiteral("手眼标定无效：未加载有效的 RTmatDepth2robot"));
        return result;
    }
    if (!validateRigidTransform(calibration.flangeFromDepth, &validationError)) {
        setFailure(&result, RobotCloudStatus::InvalidCalibration,
                   QStringLiteral("手眼标定矩阵无效：%1").arg(validationError));
        return result;
    }
    if (!validateRigidTransform(startBaseFromFlange, &validationError)) {
        setFailure(&result, RobotCloudStatus::InvalidStartPose,
                   QStringLiteral("Start 位姿无效：%1").arg(validationError));
        return result;
    }
    if (!validateRigidTransform(endBaseFromFlange, &validationError)) {
        setFailure(&result, RobotCloudStatus::InvalidEndPose,
                   QStringLiteral("End 位姿无效：%1").arg(validationError));
        return result;
    }

    const ScanTravelInfo travel = computeScanTravel(startBaseFromFlange, endBaseFromFlange);
    if (options.layout == DepthPointLayout::LineProfileXz && !travel.valid) {
        setFailure(&result, RobotCloudStatus::NoScanTravel,
                   QStringLiteral("Start/End 没有有效行程：LineProfileXz 要求非零的扫描位移"));
        return result;
    }
    result.signedTravel = travel.signedTravel;
    result.dominantTravelAxis = travel.dominantAxis;

    if (source.isEmpty()) {
        setFailure(&result, RobotCloudStatus::EmptyInput, QStringLiteral("输入点云为空"));
        return result;
    }
    if (options.isCancelled && options.isCancelled()) {
        result.cancelled = true;
        setFailure(&result, RobotCloudStatus::Cancelled, QStringLiteral("已取消"));
        return result;
    }

    if (options.useRasterRowProgress) {
        const quint64 total = quint64(options.rasterWidth) * options.rasterHeight;
        if (options.layout != DepthPointLayout::LineProfileXz || options.rasterWidth == 0
            || options.rasterHeight < 2 || options.rasterSourceIndices.size() != source.size()) {
            setFailure(&result, RobotCloudStatus::LineProfileContractViolation,
                       QStringLiteral("扫描行进度要求有效宽高及等长原始栅格下标"));
            return result;
        }
        qsizetype previous = -1;
        for (qsizetype index : options.rasterSourceIndices) {
            if (index <= previous || index < 0 || quint64(index) >= total) {
                setFailure(&result, RobotCloudStatus::LineProfileContractViolation,
                           QStringLiteral("原始栅格下标越界或不递增"));
                return result;
            }
            previous = index;
        }
    }
    if (options.useFullXyzRasterRowProgress) {
        const quint64 total = quint64(options.rasterWidth) * options.rasterHeight;
        if (options.layout != DepthPointLayout::FullXyz || options.rasterWidth == 0
            || options.rasterHeight < 2 || options.rasterSourceIndices.size() != source.size()) {
            setFailure(&result, RobotCloudStatus::LineProfileContractViolation,
                       QStringLiteral("FullXyz 栅格行进度要求有效宽高及等长原始栅格下标"));
            return result;
        }
        qsizetype previous = -1;
        for (qsizetype index : options.rasterSourceIndices) {
            if (index <= previous || index < 0 || quint64(index) >= total) {
                setFailure(&result, RobotCloudStatus::LineProfileContractViolation,
                           QStringLiteral("FullXyz 原始栅格下标越界或不递增"));
                return result;
            }
            previous = index;
        }
    }
    const int sampleStride = qMax(1, options.sampleStride);
    result.points.reserve(source.size());
    result.sourceIndices.reserve(source.size());
    result.scanRatios.reserve(source.size());
    if(options.collectSamplePoints)result.samplePoints.reserve((source.size() + sampleStride - 1) / sampleStride);
    result.inputYMinimum = std::numeric_limits<float>::max();
    result.inputYMaximum = std::numeric_limits<float>::lowest();

    const bool rotationChanges = options.interpolateRotation
        && !sameRotation(startBaseFromFlange, endBaseFromFlange);
    QVector<QMatrix4x4> transformLut;
    if (rotationChanges) {
        transformLut.reserve(RotationLutResolution + 1);
        for (int index = 0; index <= RotationLutResolution; ++index) {
            transformLut.push_back(interpolateRobotTransform(
                startBaseFromFlange, endBaseFromFlange,
                float(index) / float(RotationLutResolution)));
        }
    }

    const auto convertRange=[&source,&calibration,&startBaseFromFlange,&endBaseFromFlange,
                             &options,&travel,&transformLut,rotationChanges](qsizetype begin,qsizetype end,RobotCloudResult &chunk) {
    for (qsizetype sourceIndex = begin; sourceIndex < end; ++sourceIndex) {
        if ((sourceIndex % 4096) == 0 && options.isCancelled && options.isCancelled()) {
            chunk.cancelled = true;
            setFailure(&chunk, RobotCloudStatus::Cancelled, QStringLiteral("已取消"));
            return;
        }
        const Point3D &input = source.at(sourceIndex);
        if (!finitePoint(input) || !finiteRotation(input)
            || (options.layout == DepthPointLayout::LineProfileXz
                && !options.useRasterRowProgress && !nonzeroPoint(input))) {
            ++chunk.rejectedInvalid;
            continue;
        }

        float ratio = source.size() > 1
            ? float(sourceIndex) / float(source.size() - 1) : 0.0f;
        if (options.useFullXyzRasterRowProgress) {
            ratio = float(options.rasterSourceIndices.at(sourceIndex) / options.rasterWidth)
                / float(options.rasterHeight - 1);
        }
        Point3D depthPoint = input;
        if (options.layout == DepthPointLayout::LineProfileXz) {
            // Contract: the input Y only expresses the scan progress, the rigid
            // transform receives [X,0,Z].
            ratio = options.useRasterRowProgress
                ? float(options.rasterSourceIndices.at(sourceIndex) / options.rasterWidth)
                    / float(options.rasterHeight - 1)
                : lineProfileScanRatio(input.y, travel.signedTravel);
            if (!std::isfinite(ratio)
                || ratio < options.progressMinimum || ratio > options.progressMaximum) {
                ++chunk.rejectedRange;
                continue;
            }
            depthPoint = lineProfileCameraPoint(input);
        }
        ratio = qBound(0.0f, ratio, 1.0f);

        QMatrix4x4 baseFromFlange;
        if (rotationChanges) {
            const int lutIndex = qBound(0, int(std::lround(ratio * RotationLutResolution)),
                                        RotationLutResolution);
            baseFromFlange = transformLut.at(lutIndex);
        } else {
            baseFromFlange = lerpTranslation(startBaseFromFlange, endBaseFromFlange, ratio);
        }

        Point3D converted;
        if (!applyBaseFromDepth(depthPoint, composeBaseFromDepth(baseFromFlange, calibration),
                                &converted)) {
            ++chunk.rejectedInvalid;
            continue;
        }
        chunk.points.push_back(converted);
        chunk.sourceIndices.push_back(options.useRasterRowProgress || options.useFullXyzRasterRowProgress
            ? options.rasterSourceIndices.at(sourceIndex) : sourceIndex);
        chunk.scanRatios.push_back(ratio);
        chunk.inputYMinimum = qMin(chunk.inputYMinimum, input.y);
        chunk.inputYMaximum = qMax(chunk.inputYMaximum, input.y);
    }
    };
    const auto merge=[&result](const RobotCloudResult &chunk) {
        QElapsedTimer timer;timer.start();
        result.points.append(chunk.points);result.sourceIndices.append(chunk.sourceIndices);
        result.scanRatios.append(chunk.scanRatios);
        result.rejectedInvalid+=chunk.rejectedInvalid;result.rejectedRange+=chunk.rejectedRange;
        result.inputYMinimum=qMin(result.inputYMinimum,chunk.inputYMinimum);
        result.inputYMaximum=qMax(result.inputYMaximum,chunk.inputYMaximum);
        result.mergeMs+=timer.nsecsElapsed()/1e6;
    };
    int workers=options.parallelEnabled?qBound(1,options.maxParallelTasks,4):1;
    // A bounded window avoids a second full converted cloud. The shared lease
    // covers every worker's private points/source/ratio arrays in the window.
    constexpr qsizetype chunkSize=65536;
    std::shared_ptr<pcv::ProcessingBudget::Lease> lease;
    if(options.processingBudget) {
        while(workers>1) {
            lease=options.processingBudget->acquire(size_t(workers)*size_t(chunkSize)
                *(sizeof(Point3D)+sizeof(qsizetype)+sizeof(float)));
            if(lease)break;
            --workers;
        }
    }
    // With no lease use the original serial append path, with no second point
    // buffer. Optional optimization memory never exceeds the shared budget.
    if(options.processingBudget&&!lease)workers=1;
    result.actualWorkerCount=1;
    const auto makeChunk=[&convertRange](qsizetype begin,qsizetype end) {
        RobotCloudResult chunk;
        chunk.points.reserve(end-begin);chunk.sourceIndices.reserve(end-begin);chunk.scanRatios.reserve(end-begin);
        chunk.inputYMinimum=std::numeric_limits<float>::max();chunk.inputYMaximum=std::numeric_limits<float>::lowest();
        convertRange(begin,end,chunk);return chunk;
    };
    bool stopped=false;
    if(workers==1) {
        convertRange(0,source.size(),result);stopped=result.cancelled;
    } else for(qsizetype begin=0;begin<source.size();) {
        std::vector<std::future<RobotCloudResult>> futures;
        qsizetype serialBegin=-1,serialEnd=-1;
        for(int worker=0;worker<workers&&begin<source.size();++worker) {
            const qsizetype end=qMin(source.size(),begin+chunkSize);
            try {
                futures.push_back(std::async(std::launch::async,[&makeChunk,begin,end]{return makeChunk(begin,end);}));
            } catch(const std::system_error &) {
                // Drain the earlier ranges first so a thread-creation failure
                // cannot put the serial range ahead of its predecessors.
                serialBegin=begin;serialEnd=end;workers=1;
            }
            begin=end;
            if(serialBegin>=0)break;
        }
        result.actualWorkerCount=qMax(result.actualWorkerCount,int(futures.size()));
        std::vector<RobotCloudResult> chunks;chunks.reserve(futures.size());
        for(auto &future:futures)chunks.push_back(future.get());
        for(const auto &chunk:chunks) {stopped=stopped||chunk.cancelled;merge(chunk);}
        if(serialBegin>=0) {
            convertRange(serialBegin,serialEnd,result);stopped=stopped||result.cancelled;
            if(!stopped)convertRange(begin,source.size(),result);
            stopped=stopped||result.cancelled;break;
        }
        if(stopped||(options.isCancelled&&options.isCancelled()))break;
    }
    if(stopped||(options.isCancelled&&options.isCancelled())) {
        result.points.clear();result.samplePoints.clear();result.sourceIndices.clear();result.scanRatios.clear();
        result.cancelled=true;setFailure(&result,RobotCloudStatus::Cancelled,QStringLiteral("已取消"));return result;
    }
    if(options.collectSamplePoints)
        for(qsizetype i=0;i<result.points.size();i+=sampleStride)result.samplePoints.push_back(std::as_const(result.points).at(i));

    if (result.points.isEmpty()) {
        // Distinguish "the scan window rejected everything" from "the cloud does
        // not satisfy the LineProfileXz point contract at all".
        if (result.rejectedRange > 0 && result.rejectedRange >= result.rejectedInvalid) {
            setFailure(&result, RobotCloudStatus::ProgressOutOfRange,
                       QStringLiteral("LineProfileXz 扫描进度超出允许范围 [%1, %2]："
                                      "范围内有效点 0，超范围点 %3，无效点 %4")
                           .arg(options.progressMinimum, 0, 'g', 6)
                           .arg(options.progressMaximum, 0, 'g', 6)
                           .arg(result.rejectedRange)
                           .arg(result.rejectedInvalid));
        } else {
            setFailure(&result, RobotCloudStatus::LineProfileContractViolation,
                       QStringLiteral("输入点全部无效或不符合 LineProfileXz 契约："
                                      "无效点 %1，超范围点 %2（相机点必须为 [X,0,Z] 且非零）")
                           .arg(result.rejectedInvalid)
                           .arg(result.rejectedRange));
        }
        return result;
    }
    result.status = RobotCloudStatus::Ok;
    result.ok = true;
    return result;
}

RobotCloudResult transformLineScanToRobotBase(
    const QVector<Point3D> &source,
    const HandEyeCalibration &calibration,
    const RobotPose &startPose,
    const RobotPose &endPose,
    const CloudTransformOptions &options) {
    return transformLineScanToRobotBase(source, calibration,
                                        robotPoseToMatrix(startPose),
                                        robotPoseToMatrix(endPose), options);
}

} // namespace pointcloud
