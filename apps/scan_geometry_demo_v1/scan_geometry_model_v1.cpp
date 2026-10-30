#include "scan_geometry_model_v1.h"
#include <QVector3D>

namespace pcv::demo {
bool ScanGeometryModelV1::busy() const {
    return scanState == ScanState::Running || scanState == ScanState::Paused
        || processState == ProcessState::Running || processState == ProcessState::Review;
}
void ScanGeometryModelV1::invalidateConfirmation() {
    confirmed = false;
    confirmation = {};
    ++revision;
}
void ScanGeometryModelV1::clearProcessing() {
    processState = ProcessState::Idle;
    workUnit = 0;
    error.clear();
    includedInterferences.clear();
    m_pending.clear(); m_result.clear(); m_removed.clear(); m_undo.clear();
    invalidateConfirmation();
}
void ScanGeometryModelV1::reset() {
    clearProcessing();
    scanState = ScanState::Waiting; scanProgress = 0; activeSegment = 0;
    workpiece = interference = DemoPlaneV1::None;
    m_segments = {};
}
bool ScanGeometryModelV1::beginScan() {
    if (busy() || scanState == ScanState::Emergency) return false;
    reset(); scanState = ScanState::Running; return true;
}
void ScanGeometryModelV1::advanceScan() {
    if (scanState != ScanState::Running) return;
    scanProgress = qMin(100, scanProgress + 20);
    if (scanProgress == 100) {
        m_segments = makeDemoSegmentsV1();
        scanState = ScanState::Complete;
        ++revision;
    }
}
void ScanGeometryModelV1::togglePause() {
    if (scanState == ScanState::Running) scanState = ScanState::Paused;
    else if (scanState == ScanState::Paused) scanState = ScanState::Running;
}
void ScanGeometryModelV1::stopScan() {
    if (scanState != ScanState::Running && scanState != ScanState::Paused) return;
    reset(); scanState = ScanState::Stopped;
}
void ScanGeometryModelV1::emergencyStop() { reset(); scanState = ScanState::Emergency; }
bool ScanGeometryModelV1::selectPlane(DemoPlaneV1 plane, bool isInterference) {
    if (!hasCloud() || busy() || plane == DemoPlaneV1::None || plane == DemoPlaneV1::Noise) return false;
    if (plane == (isInterference ? workpiece : interference)) {
        error = QStringLiteral("工件平面与干涉平面不能相同，请重新选择。");
        return false;
    }
    auto &target = isInterference ? interference : workpiece;
    if (target != plane) { clearProcessing(); target = plane; }
    return true;
}
void ScanGeometryModelV1::resetPlanes() {
    if (!hasCloud() || scanState == ScanState::Running || processState == ProcessState::Running) return;
    clearProcessing(); workpiece = interference = DemoPlaneV1::None;
    if (activeSegment == -1) activeSegment = 0;
}
bool ScanGeometryModelV1::startProcessing() {
    if (!hasCloud() || busy() || workpiece == DemoPlaneV1::None || processState == ProcessState::Ready) return false;
    clearProcessing();
    if (interference != DemoPlaneV1::None) includedInterferences.insert(interference);
    processState = ProcessState::Running; return true;
}
const ScanGeometryModelV1::Candidate *ScanGeometryModelV1::candidate() const {
    return m_pending.isEmpty() ? nullptr : &m_pending.first();
}
void ScanGeometryModelV1::advanceProcessing() {
    if (processState != ProcessState::Running) return;
    ++workUnit;
    if (workUnit == 12) {
        for (int i = 0; i < 6; ++i) {
            bool found = false;
            for (const auto &p : m_segments.at(i)) if (p.plane == workpiece) { found = true; break; }
            if (!found) {
                error = QStringLiteral("点云 %1 未匹配到所选工件平面，请重新选择后处理。").arg(i + 1);
                activeSegment = i; processState = ProcessState::Failed; return;
            }
        }
        QSet<DemoPlaneV1> seen;
        for (int i = 0; i < 6; ++i) for (const auto &p : m_segments.at(i)) {
            if (p.plane == DemoPlaneV1::Noise || p.plane == workpiece || includedInterferences.contains(p.plane) || seen.contains(p.plane)) continue;
            seen.insert(p.plane); m_pending.append({p.plane, i});
        }
        if (!m_pending.isEmpty()) { processState = ProcessState::Review; activeSegment = m_pending.first().segment; }
    }
    if (workUnit == 25) {
        QSet<QString> unique;
        for (const auto &segment : m_segments) for (const auto &p : segment) {
            if (p.plane != workpiece && !includedInterferences.contains(p.plane)) continue;
            const auto &v = p.position;
            const QString key = QStringLiteral("%1/%2/%3/%4").arg(int(p.plane)).arg(v.x).arg(v.y).arg(v.z);
            if (!unique.contains(key)) { unique.insert(key); m_result.append(p); }
        }
        processState = ProcessState::Ready; activeSegment = -1;
    }
}
bool ScanGeometryModelV1::resolveCandidate(bool include) {
    if (processState != ProcessState::Review || m_pending.isEmpty()) return false;
    const auto item = m_pending.takeFirst();
    if (include) includedInterferences.insert(item.plane);
    if (m_pending.isEmpty()) processState = ProcessState::Running;
    else activeSegment = m_pending.first().segment;
    return true;
}
void ScanGeometryModelV1::cancelProcessing() {
    if (processState != ProcessState::Running && processState != ProcessState::Review) return;
    clearProcessing(); processState = ProcessState::Cancelled;
    if (activeSegment == -1) activeSegment = 0;
}
bool ScanGeometryModelV1::setActiveSegment(int segment) {
    if (!hasCloud() || processState == ProcessState::Running || segment < -1 || segment > 5
        || (segment == -1 && processState != ProcessState::Ready)) return false;
    activeSegment = segment; return true;
}
QVector<DemoPointV1> ScanGeometryModelV1::visiblePoints() const {
    if (!hasCloud()) return {};
    if (activeSegment >= 0) return m_segments.at(activeSegment);
    QVector<DemoPointV1> points;
    for (const auto &p : m_result) if (!m_removed.contains(p.id)) points.append(p);
    return points;
}
bool ScanGeometryModelV1::erase(const QVector<int> &ids) {
    if (!editable() || ids.isEmpty()) return false;
    QSet<int> validIds;
    for (const auto &p : m_result) validIds.insert(p.id);
    auto next = m_removed;
    for (int id : ids) if (validIds.contains(id)) next.insert(id);
    if (next == m_removed) return false;
    m_undo.append(m_removed); m_removed = std::move(next); invalidateConfirmation(); return true;
}
bool ScanGeometryModelV1::undo() {
    if (!editable() || m_undo.isEmpty()) return false;
    m_removed = m_undo.takeLast(); invalidateConfirmation(); return true;
}
bool ScanGeometryModelV1::restoreAll() {
    if (!editable() || m_removed.isEmpty()) return false;
    m_undo.append(m_removed); m_removed.clear(); invalidateConfirmation(); return true;
}
bool ScanGeometryModelV1::geometryValid() const {
    if (processState != ProcessState::Ready) return false;
    QVector3D first, direction;
    int count = 0;
    for (const auto &p : m_result) {
        if (p.plane != workpiece || m_removed.contains(p.id)) continue;
        const QVector3D v(p.position.x, p.position.y, p.position.z);
        if (count == 0) { first = v; count = 1; }
        else if (count == 1 && (v - first).lengthSquared() > 1e-8f) { direction = v - first; count = 2; }
        else if (count == 2 && QVector3D::crossProduct(direction, v - first).lengthSquared() > 1e-8f) return true;
    }
    return false;
}
bool ScanGeometryModelV1::confirmGeometry() {
    if (!geometryValid() || confirmed) return false;
    confirmation.revision = revision; confirmation.workpiece = workpiece;
    confirmation.interferences = includedInterferences;
    for (const auto &p : m_result) if (!m_removed.contains(p.id)) confirmation.points.append(p);
    confirmed = true; return true;
}
QString ScanGeometryModelV1::processDescription() const {
    switch (processState) {
    case ProcessState::Idle: return QStringLiteral("等待选择工件平面");
    case ProcessState::Review: return QStringLiteral("发现疑似漏选干涉面，等待确认");
    case ProcessState::Ready: return geometryValid() ? QStringLiteral("处理完成 · 可修剪并确认几何结果") : QStringLiteral("工件面有效点不足，请撤销或恢复后确认");
    case ProcessState::Failed: return error;
    case ProcessState::Cancelled: return QStringLiteral("处理已取消，原始点云已保留");
    default: {
        const QStringList phases{QStringLiteral("坐标统一"), QStringLiteral("平面匹配与拟合"), QStringLiteral("提取目标点云"), QStringLiteral("配准拼接"), QStringLiteral("生成结果")};
        return phases.at(qMin(4, workUnit / 6)) + QStringLiteral(" · 演示处理 %1 / 25").arg(workUnit);
    }
    }
}
}
