#include <pcv/planefitting/plane_mask_v1.h>

#include <cmath>

using namespace pcv::planefitting;

static bool closedLoops(const PlaneMaskMeshV1 &mesh)
{
    for (const auto &range : mesh.boundaryRanges) {
        if (range.second < 4 || mesh.boundaryVertices.at(range.first)
                != mesh.boundaryVertices.at(range.first + range.second - 1))
            return false;
    }
    return true;
}

int main()
{
    QVector<pointcloud::Point3D> points;
    QVector<int> indices;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (x == 2 && y == 2) continue;
            indices.append(points.size());
            points.append({float(x), float(y), 0.0f});
        }
    }
    PlaneModel flat;
    flat.c = 1.0f;
    PlaneMaskOptionsV1 options;
    options.spacingUMm = 1.0f;
    options.spacingVMm = 1.0f;
    auto mask = buildPlaneMaskV1(points, indices, flat, options);
    if (!mask.ok || mask.mesh.triangleIndices.size() != indices.size() * 6
        || mask.mesh.boundaryRanges.size() != 2 || !closedLoops(mask.mesh)) return 1;

    QVector<pointcloud::Point3D> concavePoints;
    QVector<int> concaveIndices;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (x >= 2 && y >= 2) continue;
            concaveIndices.append(concavePoints.size());
            concavePoints.append({float(x),float(y),0.0f});
        }
    }
    mask = buildPlaneMaskV1(concavePoints, concaveIndices, flat, options);
    if (!mask.ok || mask.mesh.boundaryRanges.size() != 1 || !closedLoops(mask.mesh)) return 7;

    QVector<pointcloud::Point3D> tiltedPoints;
    QVector<int> tiltedIndices;
    const QVector3D normal = QVector3D(-2.0f,-3.0f,1.0f).normalized();
    const QVector3D axisU = QVector3D::crossProduct(QVector3D(0,0,1),normal).normalized();
    const QVector3D axisV = QVector3D::crossProduct(normal,axisU).normalized();
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            tiltedIndices.append(tiltedPoints.size());
            const QVector3D point=axisU*float(x)+axisV*float(y);
            tiltedPoints.append({point.x(),point.y(),point.z()});
        }
    }
    PlaneModel tilted;
    tilted.a = -2.0f;
    tilted.b = -3.0f;
    tilted.c = 1.0f;
    options.spacingUMm = 1.0f;
    options.spacingVMm = 1.0f;
    mask = buildPlaneMaskV1(tiltedPoints, tiltedIndices, tilted, options);
    if (!mask.ok || mask.mesh.boundaryRanges.size() != 1 || !closedLoops(mask.mesh)) return 2;
    for (const QVector3D &vertex : mask.mesh.vertices) {
        if (std::abs(tilted.a * vertex.x() + tilted.b * vertex.y()
                     + tilted.c * vertex.z() + tilted.d) > 1e-4f) return 3;
    }

    QVector<pointcloud::Point3D> anisotropicPoints;
    QVector<int> anisotropicIndices;
    const QVector3D anisotropicU = QVector3D::crossProduct(normal,QVector3D(0,1,0)).normalized();
    const QVector3D anisotropicV = QVector3D::crossProduct(normal,anisotropicU).normalized();
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            anisotropicIndices.append(anisotropicPoints.size());
            const QVector3D point=anisotropicU*float(x)+anisotropicV*float(y*3);
            anisotropicPoints.append({point.x(),point.y(),point.z()});
        }
    }
    options.samplingAxisU=anisotropicU;
    options.spacingUMm=1.0f;
    options.spacingVMm=3.0f;
    mask=buildPlaneMaskV1(anisotropicPoints,anisotropicIndices,tilted,options);
    if (!mask.ok || mask.mesh.triangleIndices.size()!=anisotropicIndices.size()*6
        || mask.mesh.boundaryRanges.size()!=1 || !closedLoops(mask.mesh)) return 8;

    int cancellationChecks = 0;
    options.isCancelled = [&cancellationChecks] { return ++cancellationChecks >= 8; };
    const auto cancelled = buildPlaneMaskV1(tiltedPoints, tiltedIndices, tilted, options);
    if (!cancelled.cancelled || cancelled.ok || !cancelled.mesh.vertices.isEmpty()) return 4;
    options.isCancelled = {};
    options.samplingAxisU={};
    options.spacingUMm=1.0f;
    options.spacingVMm=1.0f;
    const auto invalid = buildPlaneMaskV1(points, QVector<int>{0, int(points.size())}, flat, options);
    if (invalid.ok || invalid.error.isEmpty()) return 5;
    const auto budget = std::make_shared<pcv::ProcessingBudget>(1024);
    options.processingBudget = budget;
    const auto overBudget = buildPlaneMaskV1(points, indices, flat, options);
    if (overBudget.ok || !overBudget.outOfMemory || !overBudget.mesh.vertices.isEmpty()) return 6;
    return 0;
}
