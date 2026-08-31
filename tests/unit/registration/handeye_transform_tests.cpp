#include <pcv/registration/handeye_transform.h>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>

#include <cmath>
#include <iostream>

namespace {

bool near(float actual, float expected, float tolerance = 1.0e-3f) {
    return std::abs(actual - expected) <= tolerance;
}

bool require(bool condition, const char *message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);

    QTemporaryFile xmlFile;
    if (!require(xmlFile.open(), "temporary XML creation failed")) return 1;
    const QByteArray xml = R"(<ArithConfig><RTmatDepth2robot>
        <RotMat r00="1" r01="0" r02="0" r10="0" r11="1" r12="0" r20="0" r21="0" r22="1"/>
        <TVec t0="10" t1="20" t2="30"/>
        </RTmatDepth2robot></ArithConfig>)";
    if (!require(xmlFile.write(xml) == xml.size(), "temporary XML write failed")) return 1;
    xmlFile.flush();

    pointcloud::HandEyeCalibration calibration;
    QString error;
    if (!require(pointcloud::loadHandEyeCalibration(xmlFile.fileName(), &calibration, &error),
                 error.toLocal8Bit().constData())) return 1;

    const pointcloud::RobotPose start{100.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const pointcloud::RobotPose end{100.0, 10.0, 0.0, 0.0, 0.0, 0.0};
    QVector<pointcloud::Point3D> source = {
        {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 10.0f, 3.0f, 0.0f, 0.0f, 1.0f}
    };
    pointcloud::CloudTransformOptions options;
    options.sampleStride = 1;
    const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
        source, calibration, start, end, options);
    if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
    if (!require(result.points.size() == 2 && result.sourceIndices == QVector<qsizetype>({0, 1}),
                 "source index mapping changed")) return 1;
    if (!require(result.scanRatios.size() == 2
                 && near(result.scanRatios[0], 0.0f)
                 && near(result.scanRatios[1], 1.0f),
                 "scan ratio mapping changed")) return 1;
    if (!require(near(result.points[0].x, 111.0f)
                 && near(result.points[0].y, 20.0f)
                 && near(result.points[0].z, 33.0f),
                 "start point transform is incorrect")) return 1;
    if (!require(near(result.points[1].x, 111.0f)
                 && near(result.points[1].y, 30.0f)
                 && near(result.points[1].z, 33.0f),
                 "end point transform is incorrect")) return 1;

    const QMatrix4x4 rotation = pointcloud::robotPoseToMatrix(
        {0.0, 0.0, 0.0, 0.0, 0.0, 90.0});
    const QVector3D rotated = rotation.mapVector(QVector3D(1.0f, 0.0f, 0.0f));
    if (!require(near(rotated.x(), 0.0f) && near(rotated.y(), 1.0f),
                 "Rz * Ry * Rx convention is incorrect")) return 1;

    const pointcloud::RobotPose recovered = pointcloud::matrixToRobotPose(rotation);
    if (!require(std::abs(recovered.rz - 90.0) < 1.0e-3,
                 "matrix to robot pose conversion is incorrect")) return 1;

    pointcloud::HandEyeCalibration identityCalibration;
    identityCalibration.flangeFromDepth.setToIdentity();
    identityCalibration.valid = true;
    const pointcloud::RobotPose rotatingStart{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const pointcloud::RobotPose rotatingEnd{0.0, 10.0, 0.0, 0.0, 0.0, 90.0};
    const QVector<pointcloud::Point3D> rotatingSource = {
        {1.0f, 5.0f, 0.0f, 1.0f, 0.0f, 0.0f}
    };
    const pointcloud::RobotCloudResult rotatingResult =
        pointcloud::transformLineScanToRobotBase(
            rotatingSource, identityCalibration, rotatingStart, rotatingEnd, options);
    if (!require(rotatingResult.ok && rotatingResult.points.size() == 1,
                 "rotating scan transform failed")) return 1;
    const float halfSqrtTwo = std::sqrt(0.5f);
    if (!require(near(rotatingResult.points[0].x, halfSqrtTwo, 2.0e-3f)
                 && near(rotatingResult.points[0].y, 5.0f + halfSqrtTwo, 2.0e-3f),
                 "Start/End rotation was not interpolated with SLERP")) return 1;
    if (!require(near(rotatingResult.points[0].nx, halfSqrtTwo, 2.0e-3f)
                 && near(rotatingResult.points[0].ny, halfSqrtTwo, 2.0e-3f),
                 "normal rotation interpolation is incorrect")) return 1;

    std::cout << "Hand-eye transform tests passed.\n";
    return 0;
}
