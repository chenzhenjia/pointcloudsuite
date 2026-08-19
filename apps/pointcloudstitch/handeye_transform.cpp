#include "handeye_transform.h"

#include <QFile>
#include <QQuaternion>
#include <QVector3D>
#include <QXmlStreamReader>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pointcloud {
namespace {

constexpr int RotationLutResolution = 4096;

bool finitePoint(const Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
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

} // namespace

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
    calibration->flangeFromDepth = matrix;
    calibration->sourceFile = xmlPath;
    calibration->valid = true;
    return true;
}

QMatrix4x4 robotPoseToMatrix(const RobotPose &pose) {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
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

Point3D transformPointToRobotBase(const Point3D &depthPoint,
                                  const QMatrix4x4 &baseFromFlange,
                                  const HandEyeCalibration &calibration) {
    const QMatrix4x4 baseFromDepth = baseFromFlange * calibration.flangeFromDepth;
    const QVector3D position = baseFromDepth.map(
        QVector3D(depthPoint.x, depthPoint.y, depthPoint.z));
    Point3D result;
    result.x = position.x();
    result.y = position.y();
    result.z = position.z();
    QVector3D normal(depthPoint.nx, depthPoint.ny, depthPoint.nz);
    if (normal.lengthSquared() > 1.0e-12f) {
        normal = baseFromDepth.mapVector(normal).normalized();
        result.nx = normal.x();
        result.ny = normal.y();
        result.nz = normal.z();
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
    if (!calibration.valid
        || !validateRigidTransform(calibration.flangeFromDepth, &validationError)) {
        result.error = QStringLiteral("手眼标定无效：%1").arg(validationError);
        return result;
    }
    if (!validateRigidTransform(startBaseFromFlange, &validationError)) {
        result.error = QStringLiteral("Start 位姿无效：%1").arg(validationError);
        return result;
    }
    if (!validateRigidTransform(endBaseFromFlange, &validationError)) {
        result.error = QStringLiteral("End 位姿无效：%1").arg(validationError);
        return result;
    }
    if (source.isEmpty()) {
        result.error = QStringLiteral("输入点云为空");
        return result;
    }

    const QVector3D start = translationOf(startBaseFromFlange);
    const QVector3D end = translationOf(endBaseFromFlange);
    const QVector3D delta = end - start;
    int dominantAxis = 0;
    if (std::abs(delta.y()) > std::abs(delta.x())) dominantAxis = 1;
    if (std::abs(delta.z()) > std::abs(dominantAxis == 0 ? delta.x() : delta.y()))
        dominantAxis = 2;
    const float signedTravel = dominantAxis == 0 ? delta.x()
        : dominantAxis == 1 ? delta.y() : delta.z();
    if (options.layout == DepthPointLayout::LineProfileXz
        && std::abs(signedTravel) <= 1.0e-9f) {
        result.error = QStringLiteral("Start/End 无有效扫描行程");
        return result;
    }
    result.signedTravel = signedTravel;
    result.dominantTravelAxis = dominantAxis;

    const int sampleStride = qMax(1, options.sampleStride);
    result.points.reserve(source.size());
    result.sourceIndices.reserve(source.size());
    result.scanRatios.reserve(source.size());
    result.samplePoints.reserve((source.size() + sampleStride - 1) / sampleStride);
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

    qsizetype validSeen = 0;
    for (qsizetype sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
        if ((sourceIndex % 150000) == 0 && options.isCancelled && options.isCancelled()) {
            result.cancelled = true;
            result.error = QStringLiteral("已取消");
            return result;
        }
        const Point3D &input = source[sourceIndex];
        if (!finitePoint(input) || !nonzeroPoint(input)) {
            ++result.rejectedInvalid;
            continue;
        }

        float ratio = source.size() > 1
            ? float(sourceIndex) / float(source.size() - 1) : 0.0f;
        Point3D depthPoint = input;
        if (options.layout == DepthPointLayout::LineProfileXz) {
            ratio = input.y / signedTravel;
            if (ratio < options.progressMinimum || ratio > options.progressMaximum) {
                ++result.rejectedRange;
                continue;
            }
            depthPoint.y = 0.0f;
        }
        ratio = qBound(0.0f, ratio, 1.0f);

        QMatrix4x4 baseFromFlange;
        if (rotationChanges) {
            const int lutIndex = qBound(0, int(std::lround(ratio * RotationLutResolution)),
                                        RotationLutResolution);
            baseFromFlange = transformLut[lutIndex];
        } else {
            baseFromFlange = startBaseFromFlange;
            const QVector3D translation = start * (1.0f - ratio) + end * ratio;
            baseFromFlange(0, 3) = translation.x();
            baseFromFlange(1, 3) = translation.y();
            baseFromFlange(2, 3) = translation.z();
        }

        const Point3D converted = transformPointToRobotBase(
            depthPoint, baseFromFlange, calibration);
        if (!finitePoint(converted)) {
            ++result.rejectedInvalid;
            continue;
        }
        result.points.push_back(converted);
        result.sourceIndices.push_back(sourceIndex);
        result.scanRatios.push_back(ratio);
        if ((validSeen % sampleStride) == 0) result.samplePoints.push_back(converted);
        ++validSeen;
        result.inputYMinimum = qMin(result.inputYMinimum, input.y);
        result.inputYMaximum = qMax(result.inputYMaximum, input.y);
    }

    if (result.points.isEmpty()) {
        result.error = QStringLiteral("转换后没有有效点；请检查扫描方向、单位和 Start/End");
        return result;
    }
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
