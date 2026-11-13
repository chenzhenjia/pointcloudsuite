#include <pcv/planefitting/plane_mask_v1.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

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

// Canonicalize geometry rather than implementation-specific vertex numbering.
static bool sameRegion(const PlaneMaskMeshV1 &a,const PlaneMaskMeshV1 &b) {
    using Point=std::array<float,3>;
    const auto key=[](const QVector3D &v){return Point{v.x(),v.y(),v.z()};};
    const auto triangles=[&](const PlaneMaskMeshV1 &m) {
        std::vector<std::array<Point,3>> out;
        for(qsizetype i=0;i<m.triangleIndices.size();i+=3) {
            std::array<Point,3> t{key(m.vertices.at(m.triangleIndices.at(i))),key(m.vertices.at(m.triangleIndices.at(i+1))),key(m.vertices.at(m.triangleIndices.at(i+2)))};
            std::sort(t.begin(),t.end());out.push_back(t);
        }
        std::sort(out.begin(),out.end());return out;
    };
    const auto edges=[&](const PlaneMaskMeshV1 &m) {
        std::vector<std::array<Point,2>> out;
        for(const auto &range:m.boundaryRanges)for(int i=1;i<range.second;++i) {
            std::array<Point,2> e{key(m.boundaryVertices.at(range.first+i-1)),key(m.boundaryVertices.at(range.first+i))};
            std::sort(e.begin(),e.end());out.push_back(e);
        }
        std::sort(out.begin(),out.end());return out;
    };
    return closedLoops(a)&&closedLoops(b)&&triangles(a)==triangles(b)&&edges(a)==edges(b);
}
static bool denseSparseEquivalent(const QVector<pointcloud::Point3D> &points,const QVector<int> &indices,
    const PlaneModel &model,PlaneMaskOptionsV1 options) {
    options.preferDenseGrid=true;options.processingBudget=std::make_shared<pcv::ProcessingBudget>(16u*1024u*1024u);
    const auto dense=buildPlaneMaskV1(points,indices,model,options);
    options.preferDenseGrid=false;const auto sparse=buildPlaneMaskV1(points,indices,model,options);
    return dense.ok&&sparse.ok&&dense.performance.value("mask_grid_strategy")=="dense"
        &&sparse.performance.value("mask_grid_strategy")=="sparse"&&sameRegion(dense.mesh,sparse.mesh)&&options.processingBudget->used()==0;
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
    if(!denseSparseEquivalent(points,indices,flat,options)||!denseSparseEquivalent(concavePoints,concaveIndices,flat,options))return 10;
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
    if(!denseSparseEquivalent(tiltedPoints,tiltedIndices,tilted,options))return 11;

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

    if(!denseSparseEquivalent(anisotropicPoints,anisotropicIndices,tilted,options))return 11;
    // Tight budget admits the legacy sparse reservation, but not the dense addition.
    PlaneMaskOptionsV1 tight; tight.spacingUMm=tight.spacingVMm=1;
    tight.processingBudget=std::make_shared<pcv::ProcessingBudget>(size_t(indices.size())*256);
    const auto fallback=buildPlaneMaskV1(points,indices,flat,tight);
    if(!fallback.ok||fallback.performance.value("mask_dense_fallback")!="budget"||tight.processingBudget->used()!=0)return 12;
    QVector<pointcloud::Point3D> wide{{0,0,0},{1000000,0,0},{0,1000000,0}};
    const auto sparseWide=buildPlaneMaskV1(wide,{0,1,2},flat,tight);
    if(!sparseWide.ok||sparseWide.performance.value("mask_grid_strategy")!="sparse"||!closedLoops(sparseWide.mesh))return 13;
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
