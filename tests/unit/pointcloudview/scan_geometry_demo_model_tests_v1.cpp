#include "scan_geometry_model_v1.h"
#include <QtTest>

using namespace pcv::demo;
using Model = ScanGeometryModelV1;
using Plane = DemoPlaneV1;
namespace {
void scanned(Model &m) { m.beginScan(); for (int i = 0; i < 5; ++i) m.advanceScan(); }
void ready(Model &m) {
    scanned(m); m.selectPlane(Plane::A, false); m.selectPlane(Plane::B, true); m.startProcessing();
    for (int i = 0; i < 30 && m.processState != Model::ProcessState::Ready; ++i) {
        if (m.candidate()) m.resolveCandidate(true); else m.advanceProcessing();
    }
}
}
class ScanGeometryModelTestsV1 : public QObject {
    Q_OBJECT
private slots:
    void deterministicFixture() {
        const auto a = makeDemoSegmentsV1(), b = makeDemoSegmentsV1();
        QSet<int> ids;
        int cCount = 0;
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(a.at(i).size(), b.at(i).size());
            for (int j = 0; j < a.at(i).size(); ++j) {
                const auto &p = a.at(i).at(j);
                QVERIFY(!ids.contains(p.id)); ids.insert(p.id);
                QCOMPARE(p.position.x, b.at(i).at(j).position.x);
                if (p.plane == Plane::C) { QCOMPARE(i, 3); ++cCount; }
            }
        }
        QCOMPARE(cCount, 25); QCOMPARE(ids.size(), 1465);
    }
    void scanLifecycle() {
        Model m; QVERIFY(!m.hasCloud()); QVERIFY(m.beginScan());
        for (int i = 0; i < 4; ++i) { m.advanceScan(); QVERIFY(!m.hasCloud()); }
        m.advanceScan(); QVERIFY(m.hasCloud()); QCOMPARE(m.scanProgress, 100);
        QVERIFY(m.setActiveSegment(5)); QVERIFY(!m.setActiveSegment(6)); QVERIFY(!m.setActiveSegment(-1));
    }
    void pauseStopEmergency() {
        Model m; m.beginScan(); m.advanceScan(); m.togglePause();
        for (int i = 0; i < 10; ++i) m.advanceScan();
        QCOMPARE(m.scanProgress, 20); QVERIFY(!m.beginScan());
        m.togglePause(); m.advanceScan(); QCOMPARE(m.scanProgress, 40);
        m.stopScan(); for (int i = 0; i < 10; ++i) m.advanceScan();
        QVERIFY(!m.hasCloud()); QCOMPARE(m.scanState, Model::ScanState::Stopped);
        QVERIFY(m.beginScan()); m.emergencyStop(); QVERIFY(!m.beginScan());
        m.reset(); QVERIFY(m.beginScan());
    }
    void planeConflict() {
        Model m; scanned(m);
        QVERIFY(!m.selectPlane(Plane::Noise, false)); QVERIFY(!m.selectPlane(Plane::None, false));
        QVERIFY(m.selectPlane(Plane::A, false)); QVERIFY(!m.selectPlane(Plane::A, true));
        QCOMPARE(m.interference, Plane::None); QVERIFY(!m.error.isEmpty());
        QVERIFY(m.selectPlane(Plane::B, true));
        QVERIFY(m.startProcessing()); QVERIFY(!m.selectPlane(Plane::C, false));
    }
    void candidatesIncludeAndIgnore() {
        Model m; scanned(m); m.selectPlane(Plane::A, false); QVERIFY(m.startProcessing());
        for (int i = 0; i < 12; ++i) m.advanceProcessing();
        QCOMPARE(m.processState, Model::ProcessState::Review);
        QVERIFY(m.candidate()); QCOMPARE(m.candidate()->plane, Plane::B);
        m.advanceProcessing(); QCOMPARE(m.workUnit, 12);
        QVERIFY(m.resolveCandidate(true)); QCOMPARE(m.candidate()->plane, Plane::C); QCOMPARE(m.activeSegment, 3);
        QVERIFY(m.resolveCandidate(false)); QCOMPARE(m.processState, Model::ProcessState::Running);
        while (m.workUnit < 25) m.advanceProcessing();
        QVERIFY(m.geometryValid()); QVERIFY(m.includedInterferences.contains(Plane::B));
        QVERIFY(!m.includedInterferences.contains(Plane::C));
        for (const auto &p : m.visiblePoints()) QVERIFY(p.plane == Plane::A || p.plane == Plane::B);
    }
    void missingWorkpieceFailsAtSource() {
        Model m; scanned(m); m.setActiveSegment(3); m.selectPlane(Plane::C, false); m.startProcessing();
        for (int i = 0; i < 20; ++i) m.advanceProcessing();
        QCOMPARE(m.processState, Model::ProcessState::Failed); QCOMPARE(m.activeSegment, 0);
        QVERIFY(m.error.contains(QStringLiteral("点云 1"))); QVERIFY(!m.geometryValid()); QVERIFY(m.hasCloud());
    }
    void cancellationAndRetry() {
        Model m; scanned(m); m.selectPlane(Plane::A, false); m.startProcessing();
        for (int i = 0; i < 12; ++i) m.advanceProcessing();
        m.cancelProcessing(); QVERIFY(!m.candidate()); QVERIFY(m.hasCloud());
        for (int i = 0; i < 20; ++i) m.advanceProcessing();
        QCOMPARE(m.processState, Model::ProcessState::Cancelled); QVERIFY(!m.setActiveSegment(-1));
        QVERIFY(m.startProcessing());
    }
    void stableIdsAndDeduplication() {
        Model m; ready(m); QCOMPARE(m.processState, Model::ProcessState::Ready);
        QSet<int> original, merged;
        for (const auto &segment : m.segments()) for (const auto &p : segment) original.insert(p.id);
        for (const auto &p : m.visiblePoints()) { QVERIFY(original.contains(p.id)); QVERIFY(!merged.contains(p.id)); merged.insert(p.id); }
        QCOMPARE(merged.size(), 1203); // 62 columns * (15 A rows + 4 B rows), plus 25 C points.
        m.setActiveSegment(0); QVERIFY(!m.erase({0}));
        m.setActiveSegment(-1); QCOMPARE(m.visiblePoints().size(), merged.size());
    }
    void eraseUndoRestoreAndConfirmation() {
        Model m; ready(m); const auto original = m.visiblePoints();
        QVERIFY(m.confirmGeometry()); QCOMPARE(m.confirmation.points.size(), original.size());
        QVERIFY(m.erase({original.at(0).id, original.at(1).id})); QVERIFY(!m.confirmed);
        QVERIFY(m.confirmation.points.isEmpty()); QCOMPARE(m.removedCount(), 2);
        QVERIFY(m.erase({original.at(2).id})); QCOMPARE(m.removedCount(), 3);
        QVERIFY(m.restoreAll()); QCOMPARE(m.removedCount(), 0);
        QVERIFY(m.undo()); QCOMPARE(m.removedCount(), 3);
        QVERIFY(m.undo()); QCOMPARE(m.removedCount(), 2);
        QVERIFY(m.undo()); QCOMPARE(m.visiblePoints().size(), original.size()); QVERIFY(!m.undo());
    }
    void degenerateAndEmptyResult() {
        Model m; ready(m); QVector<int> erase;
        for (const auto &p : m.visiblePoints()) if (p.plane == Plane::A && p.position.y != 0) erase.append(p.id);
        QVERIFY(m.erase(erase)); QVERIFY(!m.geometryValid()); QVERIFY(!m.confirmGeometry());
        QVERIFY(m.undo()); QVERIFY(m.geometryValid()); erase.clear();
        for (const auto &p : m.visiblePoints()) erase.append(p.id);
        QVERIFY(m.erase(erase)); QVERIFY(m.visiblePoints().isEmpty()); QVERIFY(!m.geometryValid());
        QVERIFY(m.restoreAll()); QVERIFY(m.geometryValid());
    }
    void invalidIdsAndReselection() {
        Model m; ready(m); QVERIFY(!m.erase({-1, 999999})); QVERIFY(!m.canUndo());
        m.confirmGeometry(); m.setActiveSegment(0); QVERIFY(m.selectPlane(Plane::C, false));
        QVERIFY(!m.confirmed); QVERIFY(!m.geometryValid()); QVERIFY(!m.setActiveSegment(-1));
        m.resetPlanes(); QVERIFY(m.hasCloud()); QCOMPARE(m.workpiece, Plane::None);
    }
};
QTEST_GUILESS_MAIN(ScanGeometryModelTestsV1)
#include "scan_geometry_demo_model_tests_v1.moc"
