#pragma once

#include "demo_cloud_v1.h"
#include <QSet>

namespace pcv::demo {
// Deterministic interaction model only. No acquisition, fitting or registration.
class ScanGeometryModelV1 {
public:
    enum class ScanState { Waiting, Running, Paused, Stopped, Complete, Emergency };
    enum class ProcessState { Idle, Running, Review, Ready, Failed, Cancelled };
    struct Candidate { DemoPlaneV1 plane; int segment; };
    struct Confirmation {
        quint64 revision = 0;
        DemoPlaneV1 workpiece = DemoPlaneV1::None;
        QSet<DemoPlaneV1> interferences;
        QVector<DemoPointV1> points;
    };

    bool beginScan();
    void advanceScan();
    void togglePause();
    void stopScan();
    void emergencyStop();
    void reset();
    bool selectPlane(DemoPlaneV1 plane, bool interference);
    void resetPlanes();
    bool startProcessing();
    void advanceProcessing();
    bool resolveCandidate(bool include);
    void cancelProcessing();
    bool setActiveSegment(int segment);
    QVector<DemoPointV1> visiblePoints() const;
    bool erase(const QVector<int> &ids);
    bool undo();
    bool restoreAll();
    bool geometryValid() const;
    bool confirmGeometry();
    bool busy() const;
    bool hasCloud() const { return scanState == ScanState::Complete; }
    bool editable() const { return processState == ProcessState::Ready && activeSegment == -1; }
    const DemoSegmentsV1 &segments() const { return m_segments; }
    const Candidate *candidate() const;
    int removedCount() const { return int(m_removed.size()); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    QString processDescription() const;

    ScanState scanState = ScanState::Waiting;
    ProcessState processState = ProcessState::Idle;
    int scanProgress = 0;
    int workUnit = 0;
    int activeSegment = 0;
    quint64 revision = 0;
    DemoPlaneV1 workpiece = DemoPlaneV1::None;
    DemoPlaneV1 interference = DemoPlaneV1::None;
    QSet<DemoPlaneV1> includedInterferences;
    QString error;
    bool confirmed = false;
    Confirmation confirmation;
private:
    void invalidateConfirmation();
    void clearProcessing();
    DemoSegmentsV1 m_segments;
    QVector<Candidate> m_pending;
    QVector<DemoPointV1> m_result;
    QSet<int> m_removed;
    QVector<QSet<int>> m_undo;
};
}
