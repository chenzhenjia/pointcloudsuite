#include <pcv/planefitting/plane_mask_v1.h>

#include <climits>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcv::planefitting {
namespace {
struct GridCell {
    qint64 x = 0, y = 0;
    bool operator==(const GridCell &other) const { return x == other.x && y == other.y; }
};
struct GridCellHash {
    size_t operator()(const GridCell &cell) const {
        return std::hash<quint64>{}(quint64(cell.x) * 73856093u ^ quint64(cell.y) * 19349663u);
    }
};
struct GridVertex {
    qint64 x = 0, y = 0;
    bool operator==(const GridVertex &other) const { return x == other.x && y == other.y; }
};
struct GridVertexHash {
    size_t operator()(const GridVertex &vertex) const {
        return std::hash<quint64>{}(quint64(vertex.x) * 83492791u ^ quint64(vertex.y) * 2654435761u);
    }
};
struct BoundaryEdge {
    GridVertex from, to;
    bool used = false;
};
QVector3D position(const pointcloud::Point3D &point) { return {point.x, point.y, point.z}; }
bool finite(const pointcloud::Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
}

PlaneMaskResultV1 buildPlaneMaskV1(const QVector<pointcloud::Point3D> &points,
    const QVector<int> &planeIndices, const PlaneModel &model, const PlaneMaskOptionsV1 &options) {
    PlaneMaskResultV1 result;QElapsedTimer stage;stage.start();
    const auto cancelled = [&] { return options.isCancelled && options.isCancelled(); };
    const auto fail = [&result](const QString &message) {
        result.mesh = {};
        result.error = message;
        return result;
    };
    if (cancelled()) { result.cancelled = true; result.error = QStringLiteral("cancelled"); return result; }
    QVector3D normal(model.a, model.b, model.c);
    const float normalLength = normal.length();
    if (points.isEmpty() || planeIndices.isEmpty() || !std::isfinite(normalLength)
        || normalLength < 1e-6f || !std::isfinite(model.d)
        || !std::isfinite(options.spacingUMm) || options.spacingUMm < 0
        || !std::isfinite(options.spacingVMm) || options.spacingVMm < 0) {
        return fail(QStringLiteral("平面掩码输入或采样间距无效"));
    }
    qsizetype validationIndex=0;
    for (int index : planeIndices) {
        if((validationIndex++ & 4095)==0&&cancelled()){result.cancelled=true;result.error=QStringLiteral("cancelled");return result;}
        if (index < 0 || index >= points.size() || !finite(points.at(index)))
            return fail(QStringLiteral("平面掩码包含越界或无效的完整点索引"));
    }

    normal /= normalLength;
    QVector3D axisU = options.samplingAxisU;
    if (!std::isfinite(axisU.x()) || !std::isfinite(axisU.y()) || !std::isfinite(axisU.z()))
        return fail(QStringLiteral("平面掩码采样方向无效"));
    if (axisU.lengthSquared() > 1e-12f) {
        axisU -= normal * QVector3D::dotProduct(axisU, normal);
        if (axisU.lengthSquared() <= 1e-12f)
            return fail(QStringLiteral("平面掩码采样方向与拟合平面法向共线"));
        axisU.normalize();
    } else {
        const QVector3D reference = std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
        axisU = QVector3D::crossProduct(reference, normal).normalized();
    }
    const QVector3D axisV = QVector3D::crossProduct(normal, axisU).normalized();
    const QVector3D planeOrigin = -model.d / normalLength * normal;
    QVector<double> projectedU, projectedV;
    double minimumU = std::numeric_limits<double>::max();
    double maximumU = std::numeric_limits<double>::lowest();
    double minimumV = std::numeric_limits<double>::max();
    double maximumV = std::numeric_limits<double>::lowest();
    try {
        std::shared_ptr<pcv::ProcessingBudget::Lease> lease;
        if (options.processingBudget) {
            const size_t count = size_t(planeIndices.size());
            if (count > std::numeric_limits<size_t>::max() / 256
                || !(lease = options.processingBudget->acquire(count * 256))) {
                result.outOfMemory = true;
                return fail(QStringLiteral("平面掩码临时网格超过处理预算"));
            }
        }
        projectedU.reserve(planeIndices.size()); projectedV.reserve(planeIndices.size());
        for (qsizetype i = 0; i < planeIndices.size(); ++i) {
            if ((i & 4095) == 0 && cancelled()) {
                result.mesh = {}; result.cancelled = true; result.error = QStringLiteral("cancelled"); return result;
            }
            const QVector3D delta = position(points.at(planeIndices.at(i))) - planeOrigin;
            const double u = QVector3D::dotProduct(delta, axisU);
            const double v = QVector3D::dotProduct(delta, axisV);
            projectedU.append(u); projectedV.append(v);
            minimumU = qMin(minimumU, u); maximumU = qMax(maximumU, u);
            minimumV = qMin(minimumV, v); maximumV = qMax(maximumV, v);
        }

        double spacingU = options.spacingUMm;
        double spacingV = options.spacingVMm;
        const double area = qMax(0.0, maximumU - minimumU) * qMax(0.0, maximumV - minimumV);
        const double densitySpacing = std::sqrt(area / qMax<qsizetype>(1, planeIndices.size()));
        if (spacingU <= 0) spacingU = densitySpacing;
        if (spacingV <= 0) spacingV = densitySpacing;
        if (!std::isfinite(spacingU) || !std::isfinite(spacingV) || spacingU <= 1e-5 || spacingV <= 1e-5)
            return fail(QStringLiteral("无法从完整选面点集估算平面掩码采样间距"));
        result.spacingUMm=spacingU;result.spacingVMm=spacingV;
        if(options.fixedGridOrigin) {
            if(!std::isfinite(options.gridOriginU)||!std::isfinite(options.gridOriginV)
                ||options.gridOriginU>minimumU||options.gridOriginV>minimumV)
                return fail(QStringLiteral("固定平面掩码网格原点无效"));
            minimumU=options.gridOriginU;minimumV=options.gridOriginV;
        }
        result.gridOriginU=minimumU;result.gridOriginV=minimumV;

        result.performance.insert("mask_projection_ms",stage.nsecsElapsed()/1e6);stage.restart();
        const double widthValue=std::floor((maximumU-minimumU)/spacingU)+1;
        const double heightValue=std::floor((maximumV-minimumV)/spacingV)+1;
        size_t width=0,height=0;
        std::vector<quint8> denseOccupied;
        std::vector<quint32> denseVertexIds;
        std::shared_ptr<pcv::ProcessingBudget::Lease> denseLease;
        QString denseFallback=QStringLiteral("disabled");
        if(options.preferDenseGrid) {
            denseFallback=QStringLiteral("sparse_extent");
            if(std::isfinite(widthValue)&&std::isfinite(heightValue)&&widthValue>=1&&heightValue>=1
                &&widthValue<=INT_MAX&&heightValue<=INT_MAX) {
                width=size_t(widthValue);height=size_t(heightValue);
                // Reject huge/sparse extents before any allocation or integer multiplication.
                const long double cellCount=static_cast<long double>(width)*height;
                const long double vertexCount=(static_cast<long double>(width)+1)*(height+1);
                const long double bytes=cellCount+vertexCount*sizeof(quint32);
                if(cellCount<=static_cast<long double>(planeIndices.size())*4
                    &&bytes<=std::numeric_limits<size_t>::max()&&vertexCount<std::numeric_limits<quint32>::max()) {
                    if(options.processingBudget)denseLease=options.processingBudget->acquire(size_t(bytes));
                    if(!options.processingBudget||denseLease) {
                        try {
                            denseOccupied.resize(width*height,0);
                            denseVertexIds.resize((width+1)*(height+1),std::numeric_limits<quint32>::max());
                            denseFallback.clear();
                        } catch(const std::bad_alloc &) {
                            std::vector<quint8>().swap(denseOccupied);std::vector<quint32>().swap(denseVertexIds);
                            denseLease.reset();denseFallback=QStringLiteral("allocation");
                        }
                    } else denseFallback=QStringLiteral("budget");
                }
            }
        }
        const bool dense=!denseOccupied.empty();
        result.performance.insert("mask_grid_strategy",dense?"dense":"sparse");
        result.performance.insert("mask_dense_fallback",denseFallback);
        result.performance.insert("mask_dense_bytes",dense?double(denseOccupied.size()+denseVertexIds.size()*sizeof(quint32)):0.0);
        std::unordered_set<GridCell, GridCellHash> occupied;
        if(!dense)occupied.reserve(size_t(planeIndices.size()));
        std::vector<GridCell> occupiedCells;occupiedCells.reserve(size_t(planeIndices.size()));
        for (qsizetype i = 0; i < planeIndices.size(); ++i) {
            if ((i & 4095) == 0 && cancelled()) {
                result.cancelled = true; result.error = QStringLiteral("cancelled"); return result;
            }
            const double u = std::floor((projectedU.at(i) - minimumU) / spacingU);
            const double v = std::floor((projectedV.at(i) - minimumV) / spacingV);
            if (!std::isfinite(u) || !std::isfinite(v) || u > INT_MAX || v > INT_MAX)
                return fail(QStringLiteral("平面掩码网格坐标超出范围"));
            const GridCell cell{qint64(u), qint64(v)};
            if(dense) {
                if(size_t(cell.x)>=width||size_t(cell.y)>=height)return fail(QStringLiteral("平面掩码网格坐标超出范围"));
                auto &slot=denseOccupied.at(size_t(cell.y)*width+size_t(cell.x));
                if(!slot){slot=1;occupiedCells.push_back(cell);}
            } else occupied.insert(cell);
        }
        if(!dense)for(const auto &cell:occupied)occupiedCells.push_back(cell);
        const size_t occupiedCount=occupiedCells.size();
        result.performance.insert("mask_occupancy_ms",stage.nsecsElapsed()/1e6);
        result.performance.insert("mask_occupied_cells",double(occupiedCount));stage.restart();
        if (occupiedCells.empty() || occupiedCount > size_t(std::numeric_limits<quint32>::max() / 4))
            return fail(QStringLiteral("平面掩码网格为空或超过索引范围"));

        result.mesh.vertices.reserve(qsizetype(occupiedCount) * 4);
        result.mesh.triangleIndices.reserve(qsizetype(occupiedCount) * 6);
        std::unordered_map<GridVertex, quint32, GridVertexHash> vertexIds;
        if(!dense)vertexIds.reserve(occupiedCount * 4);
        const auto vertex = [&](qint64 x, qint64 y) -> quint32 {
            const GridVertex key{x, y};
            if(dense) {
                const auto id=denseVertexIds.at(size_t(y)*(width+1)+size_t(x));
                if(id!=std::numeric_limits<quint32>::max())return id;
            } else {
                const auto found = vertexIds.find(key);
                if (found != vertexIds.end()) return found->second;
            }
            const quint32 id = quint32(result.mesh.vertices.size());
            const double u = minimumU + double(x) * spacingU;
            const double v = minimumV + double(y) * spacingV;
            result.mesh.vertices.append(planeOrigin + axisU * float(u) + axisV * float(v));
            if(dense)denseVertexIds.at(size_t(y)*(width+1)+size_t(x))=id;else vertexIds.emplace(key, id);
            return id;
        };
        std::vector<BoundaryEdge> boundaryEdges;
        boundaryEdges.reserve(occupiedCount * 2);
        const auto hasCell = [&](qint64 x, qint64 y) {
            if(dense)return x>=0&&y>=0&&size_t(x)<width&&size_t(y)<height&&denseOccupied.at(size_t(y)*width+size_t(x))!=0;
            return occupied.find({x,y})!=occupied.end();
        };
        const auto addBoundary = [&boundaryEdges](const GridVertex &a, const GridVertex &b) {
            boundaryEdges.push_back({a, b, false});
        };
        for (const GridCell &cell : occupiedCells) {
            if (cancelled()) { result.mesh = {}; result.cancelled = true; result.error = QStringLiteral("cancelled"); return result; }
            const quint32 a = vertex(cell.x, cell.y);
            const quint32 b = vertex(cell.x + 1, cell.y);
            const quint32 c = vertex(cell.x + 1, cell.y + 1);
            const quint32 d = vertex(cell.x, cell.y + 1);
            result.mesh.triangleIndices << a << b << c << a << c << d;
            if (!hasCell(cell.x, cell.y - 1)) addBoundary({cell.x, cell.y}, {cell.x + 1, cell.y});
            if (!hasCell(cell.x + 1, cell.y)) addBoundary({cell.x + 1, cell.y}, {cell.x + 1, cell.y + 1});
            if (!hasCell(cell.x, cell.y + 1)) addBoundary({cell.x + 1, cell.y + 1}, {cell.x, cell.y + 1});
            if (!hasCell(cell.x - 1, cell.y)) addBoundary({cell.x, cell.y + 1}, {cell.x, cell.y});
        }
        result.performance.insert("mask_mesh_ms",stage.nsecsElapsed()/1e6);stage.restart();
        std::unordered_map<GridVertex, std::vector<size_t>, GridVertexHash> outgoing;
        outgoing.reserve(boundaryEdges.size());
        for (size_t i = 0; i < boundaryEdges.size(); ++i) {
            if ((i & 4095) == 0 && cancelled()) {
                result.mesh = {}; result.cancelled = true; result.error = QStringLiteral("cancelled"); return result;
            }
            outgoing[boundaryEdges.at(i).from].push_back(i);
        }
        const auto worldPoint = [&](const GridVertex &p) {
            const double u = minimumU + double(p.x) * spacingU;
            const double v = minimumV + double(p.y) * spacingV;
            return planeOrigin + axisU * float(u) + axisV * float(v);
        };
        for (size_t firstEdge = 0; firstEdge < boundaryEdges.size(); ++firstEdge) {
            if (boundaryEdges.at(firstEdge).used) continue;
            const int rangeStart = result.mesh.boundaryVertices.size();
            const GridVertex loopStart = boundaryEdges.at(firstEdge).from;
            size_t edgeIndex = firstEdge;
            result.mesh.boundaryVertices.append(worldPoint(loopStart));
            for (size_t steps = 0; steps <= boundaryEdges.size(); ++steps) {
                if ((steps & 4095) == 0 && cancelled()) {
                    result.mesh = {}; result.cancelled = true; result.error = QStringLiteral("cancelled"); return result;
                }
                BoundaryEdge &edge = boundaryEdges.at(edgeIndex);
                if (edge.used) break;
                edge.used = true;
                result.mesh.boundaryVertices.append(worldPoint(edge.to));
                if (edge.to == loopStart) break;
                const auto candidates = outgoing.find(edge.to);
                if (candidates == outgoing.end()) break;
                const qint64 dx = edge.to.x - edge.from.x;
                const qint64 dy = edge.to.y - edge.from.y;
                int bestTurn = -1;
                size_t nextEdge = boundaryEdges.size();
                for (size_t candidateIndex : candidates->second) {
                    const BoundaryEdge &candidate = boundaryEdges.at(candidateIndex);
                    if (candidate.used) continue;
                    const qint64 nx = candidate.to.x - candidate.from.x;
                    const qint64 ny = candidate.to.y - candidate.from.y;
                    const qint64 cross = dx * ny - dy * nx;
                    const qint64 dot = dx * nx + dy * ny;
                    const int turn = cross > 0 ? 3 : dot > 0 ? 2 : cross < 0 ? 1 : 0;
                    if (turn > bestTurn) { bestTurn = turn; nextEdge = candidateIndex; }
                }
                if (nextEdge == boundaryEdges.size()) break;
                edgeIndex = nextEdge;
            }
            const int count = result.mesh.boundaryVertices.size() - rangeStart;
            if (count >= 4 && result.mesh.boundaryVertices.at(rangeStart)
                    == result.mesh.boundaryVertices.at(rangeStart + count - 1))
                result.mesh.boundaryRanges.append({rangeStart, count});
            else
                return fail(QStringLiteral("无法提取闭合平面边界"));
        }
        if (cancelled()) { result.mesh = {}; result.cancelled = true; result.error = QStringLiteral("cancelled"); return result; }
        result.performance.insert("mask_boundary_ms",stage.nsecsElapsed()/1e6);
        result.performance.insert("mask_boundary_edges",double(boundaryEdges.size()));
        result.performance.insert("mask_vertices",double(result.mesh.vertices.size()));
        result.ok = true;
        return result;
    } catch (const std::bad_alloc &) {
        result.mesh = {}; result.outOfMemory = true;
        result.error = QStringLiteral("生成平面掩码时内存不足");
        return result;
    }
}
} // namespace pcv::planefitting
