#include <pcv/registration/handeye_transform.h>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>

#include <cmath>
#include <iostream>
#include <limits>

namespace {

bool near(float actual, float expected, float tolerance = 1.0e-3f) {
    return std::abs(actual - expected) <= tolerance;
}

bool nearPoint(const pointcloud::Point3D &actual, float x, float y, float z,
               float tolerance = 1.0e-3f) {
    return near(actual.x, x, tolerance) && near(actual.y, y, tolerance)
        && near(actual.z, z, tolerance);
}

bool require(bool condition, const char *message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

bool requireStatus(const pointcloud::RobotCloudResult &result,
                   pointcloud::RobotCloudStatus expected,
                   const char *message) {
    if (result.status == expected) return true;
    std::cerr << message << " (status="
              << pointcloud::robotCloudStatusCode(result.status).toLocal8Bit().constData()
              << ", error=" << result.error.toLocal8Bit().constData() << ")\n";
    return false;
}

// Writes a calibration XML with the given row-major rotation and translation.
bool writeCalibration(const QString &path,
                      const char *r00, const char *r01, const char *r02,
                      const char *r10, const char *r11, const char *r12,
                      const char *r20, const char *r21, const char *r22,
                      const char *t0, const char *t1, const char *t2) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    // [n] tokens cannot collide with each other (unlike %1 inside %10).
    const QByteArray xml = QByteArrayLiteral(
        "<ArithConfig><RTmatDepth2robot>"
        "<RotMat r00=\"[1]\" r01=\"[2]\" r02=\"[3]\" r10=\"[4]\" r11=\"[5]\" r12=\"[6]\""
        " r20=\"[7]\" r21=\"[8]\" r22=\"[9]\"/>"
        "<TVec t0=\"[10]\" t1=\"[11]\" t2=\"[12]\"/>"
        "</RTmatDepth2robot></ArithConfig>")
        .replace("[1]", r00).replace("[2]", r01).replace("[3]", r02)
        .replace("[4]", r10).replace("[5]", r11).replace("[6]", r12)
        .replace("[7]", r20).replace("[8]", r21).replace("[9]", r22)
        .replace("[10]", t0).replace("[11]", t1).replace("[12]", t2);
    return file.write(xml) == xml.size();
}

bool loadCalibration(const QString &path, pointcloud::HandEyeCalibration *calibration,
                     QString *error) {
    return pointcloud::loadHandEyeCalibration(path, calibration, error);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);

    // ------------------------------------------------------------------
    // Fixtures: identity and a golden T_flange_depth (Rz(90 deg), t=(10,20,30)).
    // ------------------------------------------------------------------
    QTemporaryFile identityXml;
    if (!require(identityXml.open(), "temporary identity XML creation failed")) return 1;
    const QByteArray identityCalibrationXml = R"(<ArithConfig><RTmatDepth2robot>
        <RotMat r00="1" r01="0" r02="0" r10="0" r11="1" r12="0" r20="0" r21="0" r22="1"/>
        <TVec t0="10" t1="20" t2="30"/>
        </RTmatDepth2robot></ArithConfig>)";
    if (!require(identityXml.write(identityCalibrationXml) == identityCalibrationXml.size(),
                 "temporary identity XML write failed")) return 1;
    identityXml.flush();

    QTemporaryFile goldenXml;
    if (!require(goldenXml.open(), "temporary golden XML creation failed")) return 1;
    // Rz(90): (x,y,z) -> (-y,x,z); translation (10,20,30).
    if (!require(writeCalibration(goldenXml.fileName(),
                                  "0", "-1", "0", "1", "0", "0", "0", "0", "1",
                                  "10", "20", "30"),
                 "temporary golden XML write failed")) return 1;
    goldenXml.flush();

    pointcloud::HandEyeCalibration translated;   // identity rotation, t=(10,20,30)
    QString error;
    if (!require(loadCalibration(identityXml.fileName(), &translated, &error),
                 error.toLocal8Bit().constData())) return 1;
    pointcloud::HandEyeCalibration golden;       // Rz(90), t=(10,20,30)
    if (!require(loadCalibration(goldenXml.fileName(), &golden, &error),
                 error.toLocal8Bit().constData())) return 1;

    pointcloud::HandEyeCalibration identityCalibration;
    identityCalibration.flangeFromDepth.setToIdentity();
    identityCalibration.valid = true;

    pointcloud::CloudTransformOptions options;
    options.sampleStride = 1;

    const pointcloud::RobotPose origin{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    // Pure translation scan used by the LineProfileXz contract checks.
    const pointcloud::RobotPose plainEnd{0.0, 10.0, 0.0, 0.0, 0.0, 0.0};
    // Golden scan for the rotation chain: Rz(90) on both poses.
    const pointcloud::RobotPose scanStart{100.0, 0.0, 0.0, 0.0, 0.0, 90.0};
    const pointcloud::RobotPose scanEnd{100.0, 10.0, 0.0, 0.0, 0.0, 90.0};

    // ------------------------------------------------------------------
    // 1. T_base_depth = T_base_flange * T_flange_depth (multiplication order).
    // ------------------------------------------------------------------
    {
        QMatrix4x4 baseFromFlange;
        baseFromFlange.setToIdentity();
        baseFromFlange(0, 3) = 100.0f;   // pure translation, identity rotation
        const pointcloud::Point3D camera{1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f};
        QString pointError;
        const pointcloud::Point3D converted = pointcloud::transformPointToRobotBase(
            camera, baseFromFlange, golden, &pointError);
        if (!require(pointError.isEmpty(), "valid transform reported an error")) return 1;
        // flange_from_depth first, then base_from_flange.
        if (!require(nearPoint(converted, 110.0f, 21.0f, 33.0f),
                     "T_base_flange * T_flange_depth order is incorrect")) return 1;
        // The reversed product must not be what the conversion applies.
        const QVector3D reversed = QVector3D((golden.flangeFromDepth * baseFromFlange)
                                                 .map(QVector3D(1.0f, 0.0f, 3.0f)));
        if (!require(!near(reversed.x(), converted.x, 1.0e-2f),
                     "conversion silently inverted the hand-eye chain")) return 1;
        const QMatrix4x4 composed = pointcloud::composeBaseFromDepth(baseFromFlange, golden);
        const QVector3D composedPoint = QVector3D(composed.map(QVector3D(1.0f, 0.0f, 3.0f)));
        if (!require(near(composedPoint.x(), 110.0f) && near(composedPoint.y(), 21.0f)
                     && near(composedPoint.z(), 33.0f),
                     "composeBaseFromDepth changed the published chain")) return 1;
    }

    // ------------------------------------------------------------------
    // 2. Golden JSON chain: translation and rotation through module 50.
    // ------------------------------------------------------------------
    {
        const QVector<pointcloud::Point3D> source = {
            {1.0f, 0.0f, 3.0f, 1.0f, 0.0f, 0.0f},
            {1.0f, 10.0f, 3.0f, 1.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
            source, golden, scanStart, scanEnd, options);
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        if (!require(result.points.size() == 2
                     && result.sourceIndices == QVector<qsizetype>({0, 1}),
                     "source index mapping changed")) return 1;
        if (!require(near(result.scanRatios.at(0), 0.0f)
                     && near(result.scanRatios.at(1), 1.0f),
                     "scan ratio mapping changed")) return 1;
        // T_flange_depth(1,0,3) = Rz90 * (1,0,3) + (10,20,30) = (10,21,33);
        // T_base_flange(Start) = Rz90 * (10,21,33) + (100,0,0) = (79,10,33).
        if (!require(nearPoint(result.points.at(0), 79.0f, 10.0f, 33.0f),
                     "golden start point transform is incorrect")) return 1;
        if (!require(nearPoint(result.points.at(1), 79.0f, 20.0f, 33.0f),
                     "golden end point transform is incorrect")) return 1;
    }

    // ------------------------------------------------------------------
    // 3./4. LineProfileXz: Y only carries the scan ratio, the local point is
    // [X,0,Z] and source.y is never added again in camera space.
    // ------------------------------------------------------------------
    {
        const pointcloud::Point3D camera{1.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f};
        const pointcloud::Point3D contracted = pointcloud::lineProfileCameraPoint(camera);
        if (!require(nearPoint(contracted, 1.0f, 0.0f, 3.0f),
                     "lineProfileCameraPoint must return [X,0,Z]")) return 1;
        if (!require(near(pointcloud::lineProfileScanRatio(5.0f, 10.0f), 0.5f)
                     && near(pointcloud::lineProfileScanRatio(-5.0f, -10.0f), 0.5f)
                     && near(pointcloud::lineProfileScanRatio(5.0f, 0.0f), 0.0f),
                     "lineProfileScanRatio contract changed")) return 1;

        const QVector<pointcloud::Point3D> source = {
            {1.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {2.0f, 4.0f, 1.0f, 0.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
            source, identityCalibration, origin, plainEnd, options);
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        // y=5 -> ratio 0.5 -> flange translation (0,5,0); camera y is zeroed.
        if (!require(nearPoint(result.points.at(0), 1.0f, 5.0f, 3.0f),
                     "LineProfileXz must not keep the source y as a camera coordinate")) return 1;
        // A second copy of source.y would give y = 4 + 4 = 8 here.
        if (!require(nearPoint(result.points.at(1), 2.0f, 4.0f, 1.0f),
                     "source.y was added twice into robot_base")) return 1;
    }

    // ------------------------------------------------------------------
    // 5./6. Positive and negative scan travel, forward and reverse scan order.
    // ------------------------------------------------------------------
    {
        const QVector<pointcloud::Point3D> forward = {
            {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 10.0f, 3.0f, 0.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult forwardResult =
            pointcloud::transformLineScanToRobotBase(forward, identityCalibration,
                                                     origin, plainEnd, options);
        if (!require(forwardResult.ok, forwardResult.error.toLocal8Bit().constData())) return 1;
        if (!require(near(forwardResult.signedTravel, 10.0f)
                     && forwardResult.dominantTravelAxis == 1,
                     "positive scan travel diagnostics changed")) return 1;
        if (!require(nearPoint(forwardResult.points.at(0), 1.0f, 0.0f, 3.0f)
                     && nearPoint(forwardResult.points.at(1), 1.0f, 5.0f, 3.0f)
                     && nearPoint(forwardResult.points.at(2), 1.0f, 10.0f, 3.0f),
                     "Start -> End point order or world coordinates changed")) return 1;

        // Reverse scan: Start is at y=+10 and End at y=0, so the signed travel
        // is negative and the camera profile coordinate runs 0 -> -10.
        const pointcloud::RobotPose reverseStart{0.0, 10.0, 0.0, 0.0, 0.0, 0.0};
        const QVector<pointcloud::Point3D> reverse = {
            {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, -5.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, -10.0f, 3.0f, 0.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult reverseResult =
            pointcloud::transformLineScanToRobotBase(reverse, identityCalibration,
                                                     reverseStart, origin, options);
        if (!require(reverseResult.ok, reverseResult.error.toLocal8Bit().constData())) return 1;
        if (!require(near(reverseResult.signedTravel, -10.0f)
                     && reverseResult.dominantTravelAxis == 1,
                     "negative scan travel must stay signed")) return 1;
        if (!require(nearPoint(reverseResult.points.at(0), 1.0f, 10.0f, 3.0f)
                     && nearPoint(reverseResult.points.at(1), 1.0f, 5.0f, 3.0f)
                     && nearPoint(reverseResult.points.at(2), 1.0f, 0.0f, 3.0f),
                     "reverse scan world coordinates changed")) return 1;

        // A positive profile coordinate on a negative travel is out of range.
        const QVector<pointcloud::Point3D> mismatched = {
            {1.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f}};
        const pointcloud::RobotCloudResult mismatchedResult =
            pointcloud::transformLineScanToRobotBase(mismatched, identityCalibration,
                                                     reverseStart, origin, options);
        if (!requireStatus(mismatchedResult, pointcloud::RobotCloudStatus::ProgressOutOfRange,
                           "a positive profile coordinate on a negative travel must be rejected")) return 1;
    }

    // ------------------------------------------------------------------
    // 7. FullXyz keeps every camera coordinate.
    // ------------------------------------------------------------------
    {
        pointcloud::CloudTransformOptions fullOptions = options;
        fullOptions.layout = pointcloud::DepthPointLayout::FullXyz;
        const pointcloud::RobotPose zEnd{0.0, 0.0, 10.0, 0.0, 0.0, 0.0};
        const QVector<pointcloud::Point3D> source = {
            {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            {4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
            source, identityCalibration, origin, zEnd, fullOptions);
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        if (!require(nearPoint(result.points.at(0), 1.0f, 2.0f, 3.0f)
                     && nearPoint(result.points.at(1), 4.0f, 5.0f, 16.0f),
                     "FullXyz must keep the complete camera XYZ")) return 1;
        // FullXyz does not require a scan travel at all.
        const pointcloud::RobotCloudResult noTravelFull =
            pointcloud::transformLineScanToRobotBase(source, identityCalibration,
                                                     origin, origin, fullOptions);
        if (!require(noTravelFull.ok,
                     "FullXyz must not require a LineProfileXz scan travel")) return 1;
    }

    // ------------------------------------------------------------------
    // 8. Normals share the rigid rotation and are never translated.
    // ------------------------------------------------------------------
    {
        const pointcloud::Point3D camera{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        QMatrix4x4 flange;
        flange.setToIdentity();
        const pointcloud::Point3D converted = pointcloud::transformPointToRobotBase(
            camera, flange, translated);
        if (!require(nearPoint(converted, 10.0f, 20.0f, 30.0f),
                     "translation was not applied to the position")) return 1;
        if (!require(near(converted.nx, 0.0f) && near(converted.ny, 0.0f)
                     && near(converted.nz, 1.0f),
                     "the normal must not be translated")) return 1;

        const QVector<pointcloud::Point3D> source = {
            {1.0f, 0.0f, 3.0f, 1.0f, 0.0f, 0.0f}};
        const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
            source, golden, scanStart, scanEnd, options);
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        // Rz90 (flange) then Rz90 (base) -> Rz180: (1,0,0) -> (-1,0,0).
        if (!require(near(result.points.at(0).nx, -1.0f) && near(result.points.at(0).ny, 0.0f)
                     && near(result.points.at(0).nz, 0.0f),
                     "the normal must use the same rigid rotation as the point")) return 1;
    }

    // ------------------------------------------------------------------
    // 9. Rz(C)*Ry(B)*Rx(A) convention and matrix round trip.
    // ------------------------------------------------------------------
    {
        const QMatrix4x4 rotation = pointcloud::robotPoseToMatrix(
            {0.0, 0.0, 0.0, 0.0, 0.0, 90.0});
        const QVector3D rotated = QVector3D(rotation.mapVector(QVector3D(1.0f, 0.0f, 0.0f)));
        if (!require(near(rotated.x(), 0.0f) && near(rotated.y(), 1.0f),
                     "Rz * Ry * Rx convention is incorrect")) return 1;

        // R = Ry(90)*Rx(90) maps (0,0,1) to (0,-1,0); Rx*Ry would map it to (1,0,0).
        const QMatrix4x4 ordered = pointcloud::robotPoseToMatrix(
            {0.0, 0.0, 0.0, 90.0, 90.0, 0.0});
        const QVector3D orderedPoint = QVector3D(ordered.mapVector(QVector3D(0.0f, 0.0f, 1.0f)));
        if (!require(near(orderedPoint.x(), 0.0f, 2.0e-3f)
                     && near(orderedPoint.y(), -1.0f, 2.0e-3f)
                     && near(orderedPoint.z(), 0.0f, 2.0e-3f),
                     "rotation order must be Rz(C)*Ry(B)*Rx(A)")) return 1;

        const pointcloud::RobotPose pose{12.5, -3.25, 7.75, 10.0, -20.0, 30.0};
        const pointcloud::RobotPose recovered =
            pointcloud::matrixToRobotPose(pointcloud::robotPoseToMatrix(pose));
        if (!require(near(float(recovered.x), 12.5f) && near(float(recovered.y), -3.25f)
                     && near(float(recovered.z), 7.75f)
                     && near(float(recovered.rx), 10.0f, 1.0e-2f)
                     && near(float(recovered.ry), -20.0f, 1.0e-2f)
                     && near(float(recovered.rz), 30.0f, 1.0e-2f),
                     "robot pose matrix round trip changed")) return 1;

        // SLERP midpoint used by the per-point interpolation.
        const pointcloud::RobotPose rotatingEnd{0.0, 10.0, 0.0, 0.0, 0.0, 90.0};
        const QMatrix4x4 middle = pointcloud::interpolateRobotTransform(
            pointcloud::robotPoseToMatrix(origin),
            pointcloud::robotPoseToMatrix(rotatingEnd), 0.5f);
        const QVector3D middlePoint = QVector3D(middle.map(QVector3D(1.0f, 0.0f, 0.0f)));
        const float halfSqrtTwo = std::sqrt(0.5f);
        if (!require(near(middlePoint.x(), halfSqrtTwo, 2.0e-3f)
                     && near(middlePoint.y(), 5.0f + halfSqrtTwo, 2.0e-3f),
                     "Start/End rotation was not interpolated with SLERP")) return 1;
    }

    // The published validation contract: non-finite, non-unit, non-orthogonal
    // and determinant != +1 matrices are rejected before any conversion.
    {
        QMatrix4x4 matrix;
        matrix.setToIdentity();
        if (!require(pointcloud::validateRigidTransform(matrix, &error),
                     "an identity matrix must be accepted")) return 1;
        matrix(3, 3) = 2.0f;
        if (!require(!pointcloud::validateRigidTransform(matrix, &error),
                     "a bottom row other than [0,0,0,1] must be rejected")) return 1;
        matrix.setToIdentity();
        matrix(1, 1) = 2.0f;
        if (!require(!pointcloud::validateRigidTransform(matrix, &error),
                     "a non-unit rotation column must be rejected")) return 1;
        matrix.setToIdentity();
        matrix(0, 1) = 0.5f;
        if (!require(!pointcloud::validateRigidTransform(matrix, &error),
                     "a non-orthogonal rotation must be rejected")) return 1;
        matrix.setToIdentity();
        matrix(2, 2) = -1.0f;   // Z mirror: determinant -1
        if (!require(!pointcloud::validateRigidTransform(matrix, &error),
                     "a determinant of -1 (mirror) must be rejected")) return 1;
        matrix.setToIdentity();
        matrix(0, 0) = std::numeric_limits<float>::quiet_NaN();
        if (!require(!pointcloud::validateRigidTransform(matrix, &error),
                     "a non-finite matrix must be rejected")) return 1;
    }

    // ------------------------------------------------------------------
    // 10. Calibration XML with an invalid matrix is refused.
    // ------------------------------------------------------------------
    {
        QTemporaryFile mirrorXml;
        if (!require(mirrorXml.open(), "temporary mirror XML creation failed")) return 1;
        if (!require(writeCalibration(mirrorXml.fileName(),
                                      "1", "0", "0", "0", "1", "0", "0", "0", "-1",
                                      "0", "0", "0"),
                     "temporary mirror XML write failed")) return 1;
        mirrorXml.flush();
        pointcloud::HandEyeCalibration mirror;
        if (!require(!loadCalibration(mirrorXml.fileName(), &mirror, &error),
                     "a mirror calibration must be rejected")) return 1;
        if (!require(!mirror.valid && !error.isEmpty(),
                     "a rejected mirror calibration must report a reason")) return 1;

        QTemporaryFile scaledXml;
        if (!require(scaledXml.open(), "temporary scaled XML creation failed")) return 1;
        if (!require(writeCalibration(scaledXml.fileName(),
                                      "2", "0", "0", "0", "1", "0", "0", "0", "1",
                                      "0", "0", "0"),
                     "temporary scaled XML write failed")) return 1;
        scaledXml.flush();
        pointcloud::HandEyeCalibration scaled;
        if (!require(!loadCalibration(scaledXml.fileName(), &scaled, &error),
                     "a non-unit calibration must be rejected")) return 1;

        QTemporaryFile skewXml;
        if (!require(skewXml.open(), "temporary skew XML creation failed")) return 1;
        if (!require(writeCalibration(skewXml.fileName(),
                                      "1", "0.5", "0", "0", "0.8660254", "0", "0", "0", "1",
                                      "0", "0", "0"),
                     "temporary skew XML write failed")) return 1;
        skewXml.flush();
        pointcloud::HandEyeCalibration skew;
        if (!require(!loadCalibration(skewXml.fileName(), &skew, &error),
                     "a non-orthogonal calibration must be rejected")) return 1;

        QTemporaryFile incompleteXml;
        if (!require(incompleteXml.open(), "temporary incomplete XML creation failed")) return 1;
        const QByteArray incomplete = "<ArithConfig><Other>1</Other></ArithConfig>";
        if (!require(incompleteXml.write(incomplete) == incomplete.size(),
                     "incomplete XML write failed")) return 1;
        incompleteXml.flush();
        pointcloud::HandEyeCalibration incompleteCalibration;
        QString incompleteError;
        if (!require(!loadCalibration(incompleteXml.fileName(), &incompleteCalibration,
                                      &incompleteError),
                     "calibration XML without RTmatDepth2robot must be rejected")) return 1;
        if (!require(!incompleteCalibration.valid && !incompleteError.isEmpty(),
                     "rejected calibration must report a reason")) return 1;

        // A hand-built invalid calibration must never be applied: the status
        // stays distinguishable and no Z mirror is patched in.
        pointcloud::HandEyeCalibration badManual;
        badManual.valid = true;
        badManual.flangeFromDepth.setToIdentity();
        badManual.flangeFromDepth(2, 2) = -1.0f;
        const QVector<pointcloud::Point3D> source = {
            {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f}};
        const pointcloud::RobotCloudResult badResult =
            pointcloud::transformLineScanToRobotBase(source, badManual, origin, plainEnd, options);
        if (!requireStatus(badResult, pointcloud::RobotCloudStatus::InvalidCalibration,
                           "an invalid hand-eye matrix must be rejected")) return 1;
        if (!require(!badResult.ok && badResult.points.isEmpty(),
                     "a rejected calibration must not produce points")) return 1;
    }

    // ------------------------------------------------------------------
    // 11. Distinguishable failure classes.
    // ------------------------------------------------------------------
    {
        const QVector<pointcloud::Point3D> source = {
            {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f},
            {1.0f, 10.0f, 3.0f, 0.0f, 0.0f, 1.0f}
        };

        // No scan travel.
        const pointcloud::RobotCloudResult noTravel = pointcloud::transformLineScanToRobotBase(
            source, translated, origin, origin, options);
        if (!requireStatus(noTravel, pointcloud::RobotCloudStatus::NoScanTravel,
                           "Start == End must report NoScanTravel")) return 1;
        if (!require(!noTravel.ok && !noTravel.error.isEmpty(),
                     "a rejected scan must report a reason")) return 1;

        // Empty cloud.
        const pointcloud::RobotCloudResult emptyCloud = pointcloud::transformLineScanToRobotBase(
            QVector<pointcloud::Point3D>(), translated, origin, plainEnd, options);
        if (!requireStatus(emptyCloud, pointcloud::RobotCloudStatus::EmptyInput,
                           "an empty input cloud must report EmptyInput")) return 1;

        // Invalid calibration object.
        pointcloud::HandEyeCalibration unloaded;
        const pointcloud::RobotCloudResult unloadedResult =
            pointcloud::transformLineScanToRobotBase(source, unloaded, origin, plainEnd, options);
        if (!requireStatus(unloadedResult, pointcloud::RobotCloudStatus::InvalidCalibration,
                           "an unloaded calibration must report InvalidCalibration")) return 1;

        // Invalid Start / End poses.
        pointcloud::RobotPose nonFiniteEnd = scanEnd;
        nonFiniteEnd.z = std::numeric_limits<double>::infinity();
        const pointcloud::RobotCloudResult nonFinite = pointcloud::transformLineScanToRobotBase(
            source, translated, origin, nonFiniteEnd, options);
        if (!requireStatus(nonFinite, pointcloud::RobotCloudStatus::InvalidEndPose,
                           "a non-finite End pose must report InvalidEndPose")) return 1;
        pointcloud::RobotPose nonFiniteStart = origin;
        nonFiniteStart.x = std::numeric_limits<double>::quiet_NaN();
        const pointcloud::RobotCloudResult nonFiniteStartResult =
            pointcloud::transformLineScanToRobotBase(source, translated, nonFiniteStart,
                                                     plainEnd, options);
        if (!requireStatus(nonFiniteStartResult, pointcloud::RobotCloudStatus::InvalidStartPose,
                           "a non-finite Start pose must report InvalidStartPose")) return 1;

        // Every point outside the accepted progress window.
        const QVector<pointcloud::Point3D> outsideTravel = {
            {1.0f, 500.0f, 3.0f, 0.0f, 0.0f, 0.0f}};
        const pointcloud::RobotCloudResult allRejected = pointcloud::transformLineScanToRobotBase(
            outsideTravel, translated, origin, plainEnd, options);
        if (!requireStatus(allRejected, pointcloud::RobotCloudStatus::ProgressOutOfRange,
                           "points outside the travel window must report ProgressOutOfRange")) return 1;
        if (!require(allRejected.points.isEmpty() && allRejected.rejectedRange > 0,
                     "out-of-range points must be counted and dropped")) return 1;

        // Every point invalid or violating the LineProfileXz contract.
        const QVector<pointcloud::Point3D> invalidPoints = {
            // Non-finite coordinate.
            {std::numeric_limits<float>::quiet_NaN(), 5.0f, 3.0f, 0.0f, 0.0f, 0.0f},
            // All-zero camera point: not a valid [X,0,Z] LineProfileXz sample.
            {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}
        };
        const pointcloud::RobotCloudResult invalidResult =
            pointcloud::transformLineScanToRobotBase(invalidPoints, translated,
                                                     origin, plainEnd, options);
        if (!requireStatus(invalidResult,
                           pointcloud::RobotCloudStatus::LineProfileContractViolation,
                           "invalid points must report LineProfileContractViolation")) return 1;

        // Cancellation is reported separately and never becomes a success.
        pointcloud::CloudTransformOptions cancelledOptions = options;
        cancelledOptions.isCancelled = []() { return true; };
        const pointcloud::RobotCloudResult cancelled = pointcloud::transformLineScanToRobotBase(
            source, translated, origin, plainEnd, cancelledOptions);
        if (!requireStatus(cancelled, pointcloud::RobotCloudStatus::Cancelled,
                           "cancellation must report Cancelled")) return 1;
        if (!require(!cancelled.ok && cancelled.cancelled,
                     "cancellation must be flagged and must not succeed")) return 1;

        // Stable status codes for callers and UI.
        if (!require(pointcloud::robotCloudStatusCode(pointcloud::RobotCloudStatus::Ok)
                         == QStringLiteral("PCV_M50_OK")
                     && pointcloud::robotCloudStatusCode(
                            pointcloud::RobotCloudStatus::NoScanTravel)
                         == QStringLiteral("PCV_M50_NO_SCAN_TRAVEL"),
                     "status codes must stay stable")) return 1;
    }

    // ------------------------------------------------------------------
    // Scan travel diagnostics used by the plain-PLY line.
    // ------------------------------------------------------------------
    {
        const QVector<pointcloud::Point3D> source = {
            {1.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f},
            {1.0f, 10.0f, 3.0f, 0.0f, 0.0f, 1.0f}
        };
        const pointcloud::RobotCloudResult result = pointcloud::transformLineScanToRobotBase(
            source, translated, origin, plainEnd, options);
        if (!require(result.ok, result.error.toLocal8Bit().constData())) return 1;
        if (!require(near(result.signedTravel, 10.0f) && result.dominantTravelAxis == 1,
                     "scan travel diagnostics changed")) return 1;
        if (!require(near(result.inputYMinimum, 0.0f) && near(result.inputYMaximum, 10.0f),
                     "input profile range diagnostics changed")) return 1;
        if (!require(result.samplePoints.size() == 2,
                     "sample stride mapping changed")) return 1;
        const pointcloud::ScanTravelInfo travel = pointcloud::computeScanTravel(
            pointcloud::robotPoseToMatrix(origin), pointcloud::robotPoseToMatrix(plainEnd));
        if (!require(travel.valid && near(travel.signedTravel, 10.0f)
                     && travel.dominantAxis == 1,
                     "computeScanTravel contract changed")) return 1;
        const pointcloud::ScanTravelInfo degenerate = pointcloud::computeScanTravel(
            pointcloud::robotPoseToMatrix(origin), pointcloud::robotPoseToMatrix(origin));
        if (!require(!degenerate.valid, "a degenerate travel must not be valid")) return 1;
    }

    std::cout << "Hand-eye transform tests passed.\n";
    return 0;
}
