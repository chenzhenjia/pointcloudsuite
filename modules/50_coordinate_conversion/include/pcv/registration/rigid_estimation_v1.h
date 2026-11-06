#pragma once
#include <QMatrix4x4>
#include <QVector>
#include <QVector3D>
#include <QString>
namespace pointcloud {
struct RigidEstimateV1 { bool ok=false; QMatrix4x4 targetFromSource; double rmsMm=0; QString error; };
// Equal-weight Horn fit. Rejects collinear correspondences; planar sets are valid.
RigidEstimateV1 estimateRigidV1(const QVector<QVector3D> &source, const QVector<QVector3D> &target);
QMatrix4x4 composeRigidV1(const QMatrix4x4 &targetFromMiddle, const QMatrix4x4 &middleFromSource);
double rotationDifferenceDegreesV1(const QMatrix4x4 &a, const QMatrix4x4 &b);
// Rank of normalized 6-DOF point-to-plane constraints; 6 means locally observable.
int rigidConstraintRankV1(const QVector<QVector3D> &positions,const QVector<QVector3D> &normals);
// Orthonormal frames constructed from two nonparallel directions, plus origins.
RigidEstimateV1 alignFramesV1(QVector3D sourceOrigin,QVector3D sourceX,QVector3D sourceY,
                             QVector3D targetOrigin,QVector3D targetX,QVector3D targetY);
RigidEstimateV1 alignPlanePairV1(QVector3D sourceCenter,QVector3D sourceNormal,QVector3D sourceSecondCenter,QVector3D sourceSecondNormal,
    QVector3D targetCenter,QVector3D targetNormal,QVector3D targetSecondCenter,QVector3D targetSecondNormal);
}
