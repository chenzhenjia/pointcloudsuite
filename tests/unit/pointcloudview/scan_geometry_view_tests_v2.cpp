#include "scan_geometry_window_v1.h"
#include "scan_geometry_canvas_v2.h"
#include "input_config_dialog_v2.h"
#include "scan_geometry_fixture_v2.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QtTest>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QSurfaceFormat>
#include <QApplication>
#include <QTemporaryDir>
struct PointCloudCanvasTestAccess {
    static QColor background(const PointCloudCanvas &c){return c.m_backgroundColor;}
    static QString error(const PointCloudCanvas &c){return c.m_initializationError;}
    static QVector<QVector4D> markers(const PointCloudCanvas &c){return c.m_markerColors;}
    static int colorMode(const PointCloudCanvas &c){return c.m_colorMode;}
    static QString emptyMessage(const PointCloudCanvas &c){return c.m_emptyMessage;}
    static QVector<int> mapping(const PointCloudCanvas &c){return c.m_displayToFullIndices;}
    static QVector<QPair<int,int>> maskRanges(const PointCloudCanvas &c){return c.m_planarMaskRanges;}
    static QVector<QPair<int,int>> maskBoundaryRanges(const PointCloudCanvas &c){return c.m_planarMaskBoundaryRanges;}
    static QVector<QVector4D> maskColors(const PointCloudCanvas &c){return c.m_planarMaskColors;}
    static QVector<int> rectangle(PointCloudCanvas &c){return c.pickRectangleForSelection(c.rect().adjusted(1,1,-1,-1));}
};
namespace pcv::demo {
struct ScanGeometryViewTestAccessV2 {
    static ScanGeometryCanvasV2 *view(ScanGeometryWindowV1 &w){return w.m_view;}
    static ScanGeometrySessionV2 &session(ScanGeometryWindowV1 &w){return w.m_session;}
    static void refresh(ScanGeometryWindowV1 &w,bool publish){w.refresh(publish);}
    static int maximumWorkflowStage(const ScanGeometryWindowV1 &w){return w.maximumWorkflowStage();}
    static int workflowStage(const ScanGeometryWindowV1 &w){return w.m_workflowStage;}
    static void setWorkflowStage(ScanGeometryWindowV1 &w,int stage){w.setWorkflowStage(stage);}
};
}
using namespace pcv::demo;
class ScanGeometryViewTestsV2 : public QObject {
    Q_OBJECT
private slots:
    void initialDesignerControlsAndDefaults() {
        ScanGeometryWindowV1 w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        for(const char *name:{"configureInputs","loadInputs","retryLoad","cancelTask","resetInputs","previousRound","nextRound", "confirmCandidate","confirmInterference","stitchFrames","colorLegend","highSideCandidates","markHighSideCandidate","ignoreHighSideCandidate"})
            QVERIFY2(w.findChild<QPushButton *>(name),name);
        QVERIFY(w.findChild<QToolButton *>("moreActions"));
        for(int i=0;i<5;++i) {
            auto *step=w.findChild<QToolButton *>(QStringLiteral("modelStep%1").arg(i));QVERIFY(step);
            QCOMPARE(step->isEnabled(),i==2);
            QCOMPARE(step->isChecked(),i==2);
        }
        QVERIFY(w.findChild<QPushButton *>("previousStage"));
        QVERIFY(w.findChild<QPushButton *>("nextStage"));
        QCOMPARE(ScanGeometryViewTestAccessV2::maximumWorkflowStage(w),0);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),0);
        QVERIFY(!w.findChild<QPushButton *>("nextStage")->isEnabled());
        QVERIFY(w.findChild<QPushButton *>("configureInputs")->isEnabled());
        QVERIFY(!w.findChild<QPushButton *>("stitchFrames")->isVisible());
        QVERIFY(!w.findChild<QPushButton *>("confirmGeometry")->isEnabled());
        QCOMPARE(w.findChild<QPushButton *>("configureInputs")->text(),QStringLiteral("导入数据…"));
        QVERIFY(w.findChild<QLabel *>("demoBadge")->text().contains(QStringLiteral("离线")));
        QVERIFY(!w.findChild<QPushButton *>("emergencyStop"));
        QCOMPARE(w.minimumSize(),QSize(1024,640));w.resize(w.minimumSize());QTest::qWait(30);
        ScanGeometryViewTestAccessV2::setWorkflowStage(w,3);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),0);
        QVERIFY(w.findChild<QScrollArea *>("sideScroll")->widgetResizable());
        auto *canvas=ScanGeometryViewTestAccessV2::view(w)->canvas();QTRY_VERIFY(canvas->isValid());
        QVERIFY2(PointCloudCanvasTestAccess::error(*canvas).isEmpty(),qPrintable(PointCloudCanvasTestAccess::error(*canvas)));
        QCOMPARE(PointCloudCanvasTestAccess::background(*canvas),QColor("#eef1f4"));
        QCOMPARE(PointCloudCanvasTestAccess::colorMode(*canvas),0);
        QVERIFY(PointCloudCanvasTestAccess::emptyMessage(*canvas).isEmpty());
        PointCloudCanvas old;QCOMPARE(PointCloudCanvasTestAccess::background(old),QColor(Qt::black));
        QCOMPARE(PointCloudCanvasTestAccess::emptyMessage(old),QStringLiteral("打开 PLY 文件开始预览"));
    }
    void designerInputTableAndEmptyPoses() {
        InputConfigDialogV2 dialog({});auto *table=dialog.findChild<QTableWidget *>("frames");QVERIFY(table);
        QCOMPARE(table->columnCount(),13);QCOMPARE(table->rowCount(),0);
        QVERIFY(dialog.findChild<QPushButton *>("browseXml"));QVERIFY(dialog.findChild<QPushButton *>("browseOutput"));
    }
    void workflowAdvancesFromRealSessionState() {
        QTemporaryDir root;QVERIFY(root.isValid());InputConfigV2 config;
        QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));config.poseOnly=true;
        ScanGeometryWindowV1 w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto &session=ScanGeometryViewTestAccessV2::session(w);
        QString error;QVERIFY2(session.configure(config,&error),qPrintable(error));session.loadRound(0);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);ScanGeometryViewTestAccessV2::refresh(w,true);
        QCOMPARE(ScanGeometryViewTestAccessV2::maximumWorkflowStage(w),1);
        auto *next=w.findChild<QPushButton *>("nextStage");QVERIFY(next->isEnabled());QTest::mouseClick(next,Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),1);
        session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.confirmCandidate());session.matchRemaining();
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy()&&!session.matching(),60000);
        session.activate(1);QVERIFY(session.confirmCandidate());ScanGeometryViewTestAccessV2::refresh(w,false);
        QVERIFY(session.canStitch());QVERIFY(next->isEnabled());QTest::mouseClick(next,Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),2);
        session.stitch();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        ScanGeometryViewTestAccessV2::refresh(w,true);QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),3);
        QVERIFY(w.findChild<QLabel *>("workflowStageTitle")->text().contains(QStringLiteral("确认并保存")));
        session.confirmGeometry();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.geometryConfirmed());
        session.erase({0},session.resultRevision());QVERIFY(!session.geometryConfirmed());
        QVERIFY(w.findChild<QPushButton *>("previousStage")->isEnabled());
        QTest::mouseClick(w.findChild<QPushButton *>("previousStage"),Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),2);
    }
    void nativeVisiblePickingAndStableResultMapping() {
        ScanGeometryCanvasV2 view;view.resize(700,500);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *canvas=view.canvas();QTRY_VERIFY(canvas->isValid());
        // Three coincident screen locations, two depths. Full IDs deliberately
        // contain holes, as they do after trimming an immutable result.
        QVector<pointcloud::Point3D> points{{-2,-2,0},{2,-2,0},{0,2,0},{-2,-2,5},{2,-2,5},{0,2,5}};
        view.publish(points,{1,3,5,7,9,11},12,{1,3,5},{7,9,11},{},false,true);
        canvas->setStandardView(2);QTRY_VERIFY(canvas->hasRenderedCurrentCloud());
        const auto ids=PointCloudCanvasTestAccess::rectangle(*canvas);QVERIFY(!ids.isEmpty());
        const QSet<int> picked(ids.cbegin(),ids.cend());
        for(const auto &pair:{qMakePair(1,7),qMakePair(3,9),qMakePair(5,11)})QVERIFY(!(picked.contains(pair.first)&&picked.contains(pair.second)));
        for(int id:ids)QVERIFY(QVector<int>({1,3,5,7,9,11}).contains(id));
        const auto markers=PointCloudCanvasTestAccess::markers(*canvas);QCOMPARE(markers.size(),6);
        QCOMPARE(markers.at(0).w(),0.0f);QCOMPARE(markers.at(1).w(),1.0f);
        QVERIFY(markers.at(1).z()>markers.at(1).x());QVERIFY(markers.at(4).x()>markers.at(4).z());
        view.publish({points.at(1),points.at(4)},{3,9},12,{3},{9},{},false,false);
        QCOMPARE(PointCloudCanvasTestAccess::mapping(*canvas),QVector<int>({3,9}));
        const auto remappedMarkers=PointCloudCanvasTestAccess::markers(*canvas);QCOMPARE(remappedMarkers.size(),2);
        QCOMPARE(remappedMarkers.at(0).w(),1.0f);QCOMPARE(remappedMarkers.at(1).w(),1.0f);
        view.publish({points.at(0),points.at(1),points.at(2)},{0,1,2},12,{1},{},{},false,false);
        const auto sparseMarkers=PointCloudCanvasTestAccess::markers(*canvas);
        QCOMPARE(sparseMarkers.size(),3);QCOMPARE(sparseMarkers.at(0).w(),0.0f);
        QCOMPARE(sparseMarkers.at(1).w(),1.0f);QCOMPARE(sparseMarkers.at(2).w(),0.0f);
    }
    void fittedMaskOverlayUsesSeparateFullResolutionGeometry() {
        QVector<pointcloud::Point3D> full{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
        QVector<int> indices{0,1,2,3};
        pcv::planefitting::PlaneModel model;model.c=1;
        pcv::planefitting::PlaneMaskOptionsV1 options;options.spacingUMm=1;options.spacingVMm=1;
        const auto mask=pcv::planefitting::buildPlaneMaskV1(full,indices,model,options);
        QVERIFY(mask.ok);
        ScanGeometryCanvasV2 view;view.resize(500,400);
        view.publish({full.at(0),full.at(2)},{0,2},full.size(),{0,1,2,3},{},{},true,false,1,full,&mask.mesh);
        auto *canvas=view.canvas();
        QCOMPARE(PointCloudCanvasTestAccess::maskRanges(*canvas).size(),1);
        QCOMPARE(PointCloudCanvasTestAccess::maskBoundaryRanges(*canvas).size(),1);
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).first().x(),float(QColor("#1675e8").redF()));
        QCOMPARE(PointCloudCanvasTestAccess::mapping(*canvas),QVector<int>({0,2}));
        view.publish({full.at(0),full.at(2)},{0,2},full.size(),{},{0,1,2,3},{},true,false,2,full,
                     nullptr,&mask.mesh);
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).first().z(),float(QColor("#f04452").blueF()));
        canvas->setCloud({full.first()});
        QVERIFY(PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
    }
};
int main(int argc,char **argv) {
    qputenv("QT_OPENGL","desktop");QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);ScanGeometryViewTestsV2 tests;return QTest::qExec(&tests,argc,argv);
}
#include "scan_geometry_view_tests_v2.moc"
