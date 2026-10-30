#include "scan_geometry_window_v1.h"
#include "demo_canvas_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QtTest>
#include <QCheckBox>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>

struct PointCloudCanvasTestAccess {
    static QString error(const PointCloudCanvas &c) { return c.m_initializationError; }
    static QColor background(const PointCloudCanvas &c) { return c.m_backgroundColor; }
    static QColor selection(const PointCloudCanvas &c) { return c.m_selectionColor; }
    static float yaw(const PointCloudCanvas &c) { return c.m_yaw; }
    static QPoint project(const PointCloudCanvas &c, const QVector3D &p) {
        QVector4D clip = c.viewTransform() * QVector4D((p - c.m_center) * (2.0f / c.m_span), 1);
        clip.setX(clip.x() + float(c.m_pan.x()) * clip.w());
        clip.setY(clip.y() + float(c.m_pan.y()) * clip.w());
        return QPoint(qRound((clip.x() / clip.w() + 1) * c.width() / 2), qRound((1 - clip.y() / clip.w()) * c.height() / 2));
    }
    static int pick(PointCloudCanvas &c, QPoint point) { return c.pickPoint(point); }
    static QVector<QVector4D> colors(const PointCloudCanvas &c) { return c.m_textureColors; }
};

namespace pcv::demo {
struct ScanGeometryViewTestAccessV1 {
    static ScanGeometryModelV1 &model(ScanGeometryWindowV1 &w) { return w.m_model; }
    static DemoCanvasV1 *view(ScanGeometryWindowV1 &w) { return w.m_view; }
    static void refresh(ScanGeometryWindowV1 &w, bool fit = true) { w.refresh(true, fit); }
    static void stop(ScanGeometryWindowV1 &w) { w.stopTimer(); }
    static void scanned(ScanGeometryWindowV1 &w) {
        w.stopTimer(); w.m_model.reset(); w.m_model.beginScan();
        for (int i = 0; i < 5; ++i) w.m_model.advanceScan();
        w.refresh(true, true);
    }
    static void ready(ScanGeometryWindowV1 &w) {
        scanned(w); w.m_model.selectPlane(DemoPlaneV1::A, false); w.m_model.selectPlane(DemoPlaneV1::B, true); w.m_model.startProcessing();
        for (int i = 0; i < 30 && w.m_model.processState != ScanGeometryModelV1::ProcessState::Ready; ++i) {
            if (w.m_model.candidate()) w.m_model.resolveCandidate(false); else w.m_model.advanceProcessing();
        }
        w.refresh(true, true);
    }
};
}
using namespace pcv::demo;
using Access = ScanGeometryViewTestAccessV1;
using Model = ScanGeometryModelV1;
using Plane = DemoPlaneV1;

class ScanGeometryViewTestsV1 : public QObject {
    Q_OBJECT
    template<typename T> T *control(ScanGeometryWindowV1 &w, const char *name) { return w.findChild<T *>(QString::fromLatin1(name)); }
    void evidence(ScanGeometryWindowV1 &w, const QString &name) {
        const QString dir = qEnvironmentVariable("PCV_SCAN_GEOMETRY_EVIDENCE");
        if (!dir.isEmpty()) { QDir().mkpath(dir); QVERIFY(w.grab().save(QDir(dir).filePath(name + ".png"))); }
    }
private slots:
    void initialStateAndLayout() {
        ScanGeometryWindowV1 w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *c = Access::view(w)->canvas(); QTRY_VERIFY(c->isValid());
        qInfo() << "GUI evidence:" << w.size() << "devicePixelRatio" << w.devicePixelRatioF();
        if (qEnvironmentVariableIsSet("PCV_SCAN_GEOMETRY_EXPECTED_DPR"))
            QVERIFY(qAbs(w.devicePixelRatioF() - qEnvironmentVariable("PCV_SCAN_GEOMETRY_EXPECTED_DPR").toDouble()) < 0.05);
        QVERIFY2(PointCloudCanvasTestAccess::error(*c).isEmpty(), qPrintable(PointCloudCanvasTestAccess::error(*c)));
        QVERIFY(!control<QPushButton>(w, "startScan")->isEnabled());
        QVERIFY(!control<QPushButton>(w, "startProcessing")->isEnabled());
        QVERIFY(c->height() > w.height() * 0.60);
        QCOMPARE(control<QFrame>(w, "toolStrip")->width(), 42);
        QVERIFY(control<QLabel>(w, "scanStatus")->height() < 40);
        QVERIFY(control<QLabel>(w, "footerNotice")->isVisible());
        evidence(w, "01-empty");
    }
    void realTimerPauseStopAndEmergency() {
        ScanGeometryWindowV1 w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        control<QCheckBox>(w, "preflight")->click(); control<QPushButton>(w, "startScan")->click();
        QTRY_VERIFY_WITH_TIMEOUT(Access::model(w).scanProgress > 0, 1800);
        control<QPushButton>(w, "pauseScan")->click(); const int paused = Access::model(w).scanProgress;
        QTest::qWait(600); QCOMPARE(Access::model(w).scanProgress, paused);
        control<QPushButton>(w, "stopScan")->click(); QTest::qWait(650); QVERIFY(!Access::model(w).hasCloud());
        QCOMPARE(Access::model(w).scanState, Model::ScanState::Stopped);
        control<QPushButton>(w, "emergencyStop")->click(); QVERIFY(!control<QPushButton>(w, "startScan")->isEnabled());
        control<QPushButton>(w, "resetScan")->click(); QCOMPARE(Access::model(w).scanState, Model::ScanState::Waiting);
        QVERIFY(!control<QCheckBox>(w, "preflight")->isChecked());
    }
    void pointPickingAndColorMapping() {
        ScanGeometryWindowV1 w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w)); Access::scanned(w);
        auto *c = Access::view(w)->canvas(); QTRY_VERIFY(c->hasRenderedCurrentCloud());
        QCOMPARE(PointCloudCanvasTestAccess::background(*c), QColor("#f5f9fc"));
        QCOMPARE(PointCloudCanvasTestAccess::selection(*c), QColor("#b33436"));
        auto choose = [&](Plane plane, const char *tool) {
            control<QToolButton>(w, tool)->click();
            const auto points = Access::model(w).visiblePoints();
            const auto it = std::find_if(points.cbegin(), points.cend(), [plane](const auto &p) { return p.plane == plane && p.position.x == 35 && (plane == Plane::A ? p.position.y == 96 : p.position.y == -34); });
            QVERIFY(it != points.cend());
            const auto &p = it->position;
            const QPoint pixel = PointCloudCanvasTestAccess::project(*c, {p.x, p.y, p.z});
            QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, pixel);
            QCoreApplication::processEvents();
        };
        choose(Plane::A, "selectWorkpiece"); QCOMPARE(Access::model(w).workpiece, Plane::A);
        QTRY_VERIFY(c->hasRenderedCurrentCloud());
        choose(Plane::B, "selectInterference"); QCOMPARE(Access::model(w).interference, Plane::B);
        const auto colors = PointCloudCanvasTestAccess::colors(*c);
        QCOMPARE(colors.size(), Access::model(w).visiblePoints().size());
        QVERIFY(colors.first().z() > colors.first().x());
        evidence(w, "02-selected-planes");
        const QPoint start(c->width() / 2, c->height() / 2);
        const float oldYaw = PointCloudCanvasTestAccess::yaw(*c);
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(c, start + QPoint(50, 25), 30);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, start + QPoint(50, 25));
        QVERIFY(PointCloudCanvasTestAccess::yaw(*c) != oldYaw);
        choose(Plane::A, "selectWorkpiece"); QCOMPARE(Access::model(w).workpiece, Plane::A);
    }
    void candidateReviewAndCompletion() {
        ScanGeometryWindowV1 w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w)); Access::scanned(w);
        auto &m = Access::model(w); m.selectPlane(Plane::A, false); m.startProcessing();
        for (int i = 0; i < 12; ++i) m.advanceProcessing(); Access::refresh(w);
        QVERIFY(control<QFrame>(w, "candidateCard")->isVisible());
        evidence(w, "03-review");
        control<QPushButton>(w, "includeCandidate")->click(); QVERIFY(m.candidate()); QCOMPARE(m.candidate()->plane, Plane::C);
        control<QPushButton>(w, "ignoreCandidate")->click();
        QTRY_COMPARE_WITH_TIMEOUT(m.processState, Model::ProcessState::Ready, 6000);
        QVERIFY(control<QPushButton>(w, "confirmGeometry")->isEnabled());
        control<QPushButton>(w, "confirmGeometry")->click(); QVERIFY(m.confirmed);
        QTest::qWait(100); evidence(w, "04-ready-confirmed");
    }
    void rectangleEraseUndoRestoreAndMapping() {
        ScanGeometryWindowV1 w; w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w)); Access::ready(w);
        auto *view = Access::view(w); auto *c = view->canvas(); c->setStandardView(2);
        QTRY_VERIFY(c->hasRenderedCurrentCloud());
        const int original = int(Access::model(w).visiblePoints().size());
        control<QPushButton>(w, "confirmGeometry")->click(); QVERIFY(Access::model(w).confirmed);
        control<QToolButton>(w, "rectangleTool")->click();
        const QPoint a = PointCloudCanvasTestAccess::project(*c, {105, 64, 0});
        const QPoint b = PointCloudCanvasTestAccess::project(*c, {175, 144, 0});
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(c, b); QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(!view->selectedIds().isEmpty()); const int selected = int(view->selectedIds().size());
        QCOMPARE(Access::model(w).visiblePoints().size(), original); // Selection is only a preview.
        evidence(w, "05-trim-preview");
        control<QPushButton>(w, "eraseSelection")->click();
        QCOMPARE(Access::model(w).visiblePoints().size(), original - selected);
        QVERIFY(!Access::model(w).confirmed);
        QTest::keyClick(c, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(Access::model(w).visiblePoints().size(), original);
        control<QPushButton>(w, "segment4")->click(); QCOMPARE(Access::model(w).activeSegment, 4);
        control<QPushButton>(w, "mergedSegment")->click(); QCOMPARE(Access::model(w).visiblePoints().size(), original);
        control<QToolButton>(w, "rectangleTool")->click();
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2)); QVERIFY(view->selectedIds().isEmpty());
        QTest::keyClick(c, Qt::Key_Escape); QCOMPARE(view->tool(), DemoCanvasV1::Tool::Browse);
        control<QToolButton>(w, "rectangleTool")->click();
        control<QPushButton>(w, "fitView")->setFocus();
        QTest::keyClick(control<QPushButton>(w, "fitView"), Qt::Key_Escape);
        QCOMPARE(view->tool(), DemoCanvasV1::Tool::Browse);
    }
    void visibleOnlyPickingAndDefaultCompatibility() {
        PointCloudCanvas c; c.setMinimumSize(0, 0); c.resize(600, 500);
        QCOMPARE(PointCloudCanvasTestAccess::background(c), QColor(Qt::black));
        QCOMPARE(PointCloudCanvasTestAccess::selection(c), QColor(Qt::white));
        c.setAppearance(QColor("#f5f9fc"), QColor("#b33436"));
        // Identical projected position, front point at positive Z in top view.
        c.setCloud({{0, 0, 10, 0, 0, 1}, {0, 0, -10, 0, 0, 1}, {80, 60, 0, 0, 0, 1}});
        c.setStandardView(2); c.show(); QVERIFY(QTest::qWaitForWindowExposed(&c));
        QTRY_VERIFY(c.hasRenderedCurrentCloud());
        const QPoint pixel = PointCloudCanvasTestAccess::project(c, {0, 0, 10});
        const auto selected = c.pickRectangleForSelection(QRect(pixel - QPoint(5, 5), QSize(10, 10)));
        QCOMPARE(selected.size(), 1); QCOMPARE(selected.first(), 0);
        QCOMPARE(PointCloudCanvasTestAccess::pick(c, pixel), 0);
        const auto background = c.grabFramebuffer().pixelColor(10, c.height() - 10);
        QVERIFY(qAbs(background.red() - 245) < 4 && qAbs(background.green() - 249) < 4);
        c.setDisplayCloud({{0, 0, 0, 0, 0, 1}}, {2}, 3);
        pcv::render::PointColorOverlay colors;
        colors.colors = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}; colors.valid = {1, 1, 1};
        QVERIFY(c.setPointColorOverlay(colors)); QCOMPARE(PointCloudCanvasTestAccess::colors(c).first(), QVector4D(0, 0, 1, 1));
        colors.colors[2].setX(std::numeric_limits<float>::quiet_NaN()); QVERIFY(!c.setPointColorOverlay(colors));
        QCOMPARE(PointCloudCanvasTestAccess::colors(c).first(), QVector4D(0, 0, 1, 1));
    }
    void minimumWindowAndScreenshot() {
        ScanGeometryWindowV1 w; w.resize(1024, 640); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w)); Access::ready(w);
        QTest::qWait(100);
        QVERIFY(control<QLabel>(w, "footerNotice")->geometry().right() <= w.width());
        QVERIFY(control<QScrollArea>(w, "sideScroll")->viewport()->width() >= 280);
        QVERIFY(Access::view(w)->canvas()->width() >= 500);
        evidence(w, "06-minimum-window");
    }
};
int main(int argc, char **argv) {
    qputenv("QT_OPENGL", "desktop"); QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format; format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24); QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv); ScanGeometryViewTestsV1 tests; return QTest::qExec(&tests, argc, argv);
}
#include "scan_geometry_demo_view_tests_v1.moc"
