#include "scan_geometry_window_v1.h"
#include "scan_geometry_canvas_v2.h"
#include "input_config_dialog_v2.h"
#include "scan_geometry_fixture.h"
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
#include <QGroupBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <limits>
struct PointCloudCanvasTestAccess {
    static QColor background(const PointCloudCanvas &c){return c.m_backgroundColor;}
    static QColor uniformColor(const PointCloudCanvas &c){return c.m_uniformPointColor;}
    static bool sortedMapping(const PointCloudCanvas &c){return c.m_displayMappingSorted;}
    static qsizetype reverseSize(const PointCloudCanvas &c){return c.m_fullToDisplayIndices.size();}
    static int reverse(const PointCloudCanvas &c,int full){return c.displayIndexForFullIndex(full);}
    static QVector<int> controls(const PointCloudCanvas &c){return c.m_controlPointIndices;}
    static QVector<int> selected(const PointCloudCanvas &c){return c.m_selectedIndices;}
    static bool uploadPending(const PointCloudCanvas &c){return c.m_uploadPending;}
    static QVector<float> opacities(const PointCloudCanvas &c){return c.m_planarMaskOpacities;}
    static QString error(const PointCloudCanvas &c){return c.m_initializationError;}
    static QVector<QVector4D> markers(const PointCloudCanvas &c){return c.m_markerColors;}
    static int colorMode(const PointCloudCanvas &c){return c.m_colorMode;}
    static QPair<float,float> heightRange(const PointCloudCanvas &c){return {c.m_mapMin,c.m_mapMax};}
    static QString emptyMessage(const PointCloudCanvas &c){return c.m_emptyMessage;}
    static QVector<int> mapping(const PointCloudCanvas &c){return c.m_displayToFullIndices;}
    static QVector<QPair<int,int>> maskRanges(const PointCloudCanvas &c){return c.m_planarMaskRanges;}
    static QVector<QPair<int,int>> maskBoundaryRanges(const PointCloudCanvas &c){return c.m_planarMaskBoundaryRanges;}
    static QVector<QVector4D> maskColors(const PointCloudCanvas &c){return c.m_planarMaskColors;}
    static QVector<int> rectangle(PointCloudCanvas &c){return c.pickRectangleForSelection(c.rect().adjusted(1,1,-1,-1));}
};
namespace pcv::demo {
struct ScanGeometryCanvasTestAccessV2 {
    static quint64 displayRevision(const ScanGeometryCanvasV2 &v){return v.m_displayRevision;}
    static quint64 regionRevision(const ScanGeometryCanvasV2 &v){return v.m_regionRevision;}
    static quint64 heightRangeRevision(const ScanGeometryCanvasV2 &v){return v.m_heightRangeRevision;}
    static void rectangle(ScanGeometryCanvasV2 &v,const QPoint &start,const QPoint &end){v.m_start=start;v.finishRectangle(end);}
};
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
    void sortedAndUnorderedDisplayMappings() {
        PointCloudCanvas c;QVector<pointcloud::Point3D> points{{0,0,0},{1,0,0},{0,1,0}};
        c.setDisplayCloud(points,{1,4,8},10);QVERIFY(PointCloudCanvasTestAccess::sortedMapping(c));
        QCOMPARE(PointCloudCanvasTestAccess::reverseSize(c),qsizetype(0));
        const QVector<int> expected{1,4,8};
        for(int i=0;i<3;++i)QCOMPARE(PointCloudCanvasTestAccess::reverse(c,expected.at(i)),i);
        QCOMPARE(PointCloudCanvasTestAccess::reverse(c,2),-1);
        c.setDisplayCloud(points,{8,1,4},10);QVERIFY(!PointCloudCanvasTestAccess::sortedMapping(c));
        QCOMPARE(PointCloudCanvasTestAccess::reverseSize(c),qsizetype(3));QCOMPARE(PointCloudCanvasTestAccess::reverse(c,8),0);
        const auto revision=c.displayCloudRevision();
        c.setDisplayCloud(points,{1,1,8},10);QCOMPARE(c.displayCloudRevision(),revision);
        c.setDisplayCloud(points,{1,4,10},10);QCOMPARE(c.displayCloudRevision(),revision);
        c.setDisplayCloud({points.at(0),points.at(2)},{1,8},10);
        QCOMPARE(PointCloudCanvasTestAccess::reverse(c,4),-1);QCOMPARE(PointCloudCanvasTestAccess::reverse(c,8),1);
        c.setCloud(points);QVERIFY(!PointCloudCanvasTestAccess::sortedMapping(c));QCOMPARE(PointCloudCanvasTestAccess::reverse(c,1),1);
    }
    void viewTimingUsesEachActivation() {
        ScanGeometryCanvasV2 v;v.resize(800,600);v.show();QVERIFY(QTest::qWaitForWindowExposed(&v));
        QVector<QJsonObject> events;
        v.loadCanvasTiming=[&](quint64,quint64,const QJsonObject &event){if(event.value("event")=="view_frame_swapped")events.append(event);};
        const QVector<pointcloud::Point3D> points{{0,0,0},{1,0,0},{0,1,0}};const QVector<int> mapping{0,1,2};
        const auto publish=[&]{v.publish(points,mapping,3,{},{},{},false,false,1,points);};
        v.bindLoadObservation(11,1);publish();QTRY_COMPARE(events.size(),1);
        const auto sequence=events.first().value("view_sequence");
        const auto revision=v.canvas()->displayCloudRevision();
        v.beginViewObservation(11,1);publish();QTRY_COMPARE(events.size(),2);
        QVERIFY(sequence!=events.last().value("view_sequence"));QCOMPARE(v.canvas()->displayCloudRevision(),revision);
        QVERIFY(events.last().value("visible_wait_ms").isDouble());
        v.hide();v.beginViewObservation(12,1);publish();const auto count=events.size();
        QCoreApplication::processEvents();QCOMPARE(events.size(),count);
        v.show();QTRY_COMPARE(events.size(),count+1);
    }
    void canvasPublicationTimingAndPreparedHeight() {
        ScanGeometryCanvasV2 v;v.bindLoadObservation(12,3);
        QVector<QJsonObject> records;
        v.loadCanvasTiming=[&](quint64 frame,quint64 attempt,const QJsonObject &event){
            QCOMPARE(frame,quint64(12));QCOMPARE(attempt,quint64(3));records.append(event);
        };
        QVector<pointcloud::Point3D> points{{0,0,0},{1,0,0},{0,1,0}};
        v.publish(points,{0,1,2},3,{},{},{},false,false,1,points,nullptr,nullptr,nullptr,
            ScanGeometryCanvasV2::DisplayMode::Height,"prepared",QPair<float,float>{-10,80});
        QCOMPARE(records.size(),1);QVERIFY(records.first().value("mapping_ms").isDouble());
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*v.canvas()),qMakePair(-10.0f,80.0f));
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(v),quint64(0));
        const auto revision=v.canvas()->displayCloudRevision();
        v.publish(points,{0,1,2},3,{},{},{},true,false,1,points,nullptr,nullptr,nullptr,
            ScanGeometryCanvasV2::DisplayMode::UniformGray,"prepared",QPair<float,float>{-10,80});
        // Reuse the exact mapping storage for the no-republish contract below.
        QVector<int> mapping{0,1,2};
        v.publish(points,mapping,3,{},{},{},false,false,1,points);
        const auto stable=v.canvas()->displayCloudRevision();
        v.publish(points,mapping,3,{},{},{},true,false,1,points,nullptr,nullptr,nullptr,ScanGeometryCanvasV2::DisplayMode::UniformGray);
        QCOMPARE(v.canvas()->displayCloudRevision(),stable);QVERIFY(stable>=revision);
    }
    void contractInputConfiguration() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        InputConfigDialogV2 dialog(config);dialog.resize(1120,800);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *group=dialog.findChild<QGroupBox *>("contractOutputGroup");QVERIFY(group);QVERIFY(!group->isChecked());group->setChecked(true);
        const auto set=[&](const char *name,const QString &value){auto *field=dialog.findChild<QLineEdit *>(name);QVERIFY(field);field->setText(value);};
        set("contractRuntimeRoot",QDir(root.path()).filePath("runtime_v1"));set("contractJobId","JOB-VIEW");set("contractRunId","RUN-001");
        set("contractStepId","STEP-001");set("contractWorkpieceId","WP-001");set("contractControllerId","ROBOT-001");
        auto *pixel=dialog.findChild<QDoubleSpinBox *>("contractMaskPixel");QVERIFY(pixel);pixel->setValue(0.5);
        const auto evidence=qEnvironmentVariable("PCV_SCAN_CONTRACT_EVIDENCE");
        if(!evidence.isEmpty())QVERIFY(dialog.grab().save(QDir(evidence).filePath("contract-config-v1.png")));
        auto *buttons=dialog.findChild<QDialogButtonBox *>("buttons");QVERIFY(buttons);QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
        QCOMPARE(dialog.result(),int(QDialog::Accepted));const auto result=dialog.configuration();
        QVERIFY(result.contract.enabled());QCOMPARE(result.contract.jobId,QString("JOB-VIEW"));QCOMPARE(result.contract.maskPixelSizeMm,0.5);
        QCOMPARE(result.frames.size(),2);QVERIFY(result.contract.scanPathOutputRef.isEmpty());
    }
    void initialDesignerControlsAndDefaults() {
        ScanGeometryWindowV1 w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        for(const char *name:{"configureInputs","loadInputs","retryLoad","cancelTask","resetInputs","previousRound","nextRound", "confirmCandidate","confirmInterference","stitchFrames","markHighSideCandidate","ignoreHighSideCandidate"})
            QVERIFY2(w.findChild<QPushButton *>(name),name);
        QVERIFY(w.findChild<QLabel *>("colorLegend"));
        QVERIFY(w.findChild<QComboBox *>("highSideCandidates"));
        QVERIFY(w.findChild<QToolButton *>("moreActions"));
        auto *toggle=w.findChild<QToolButton *>("toggleLoadTiming");QVERIFY(toggle);QVERIFY(!toggle->isChecked());
        auto *panel=w.findChild<QWidget *>("loadTimingPanel");QVERIFY(panel);QVERIFY(panel->isHidden());
        toggle->setChecked(true);QVERIFY(!panel->isHidden());toggle->setChecked(false);
        QVERIFY(w.findChild<QPushButton *>("openLoadLogDirectory"));
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
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        QVERIFY(canvas->overlayOptions().showColorBar);
        QVERIFY(w.findChild<QLabel *>("colorLegend")->text().contains(QStringLiteral("高度色点云")));
        QVERIFY(PointCloudCanvasTestAccess::emptyMessage(*canvas).isEmpty());
        PointCloudCanvas old;QCOMPARE(PointCloudCanvasTestAccess::background(old),QColor(Qt::black));
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(old).isValid());
        old.setUniformPointColor(QColor("#59636f"));
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(old),QColor("#59636f"));
        old.clearUniformPointColor();QVERIFY(!PointCloudCanvasTestAccess::uniformColor(old).isValid());
        QCOMPARE(PointCloudCanvasTestAccess::emptyMessage(old),QStringLiteral("打开 PLY 文件开始预览"));
    }
    void designerInputTableAndEmptyPoses() {
        InputConfigDialogV2 dialog({});auto *table=dialog.findChild<QTableWidget *>("frames");QVERIFY(table);
        QCOMPARE(table->columnCount(),13);QCOMPARE(table->rowCount(),0);
        QVERIFY(dialog.findChild<QPushButton *>("browseXml"));QVERIFY(dialog.findChild<QPushButton *>("browseOutput"));
    }
    void frameColorFollowsCandidateAndSourceLifecycle() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        ScanGeometryWindowV1 w;auto &session=ScanGeometryViewTestAccessV2::session(w);
        auto *view=ScanGeometryViewTestAccessV2::view(w);auto *canvas=view->canvas();
        auto *legend=w.findChild<QLabel *>("colorLegend");QVERIFY(legend);
        QString error;QVERIFY(session.configure(config,&error));session.loadRound(0);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        session.pick(312,true);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(!session.activeFrame()->pendingInterference.isEmpty());QVERIFY(!session.activeFrame()->hasCandidate);
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());QVERIFY(canvas->overlayOptions().showColorBar);
        QVERIFY(!PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
        session.clearInterference();session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.activeFrame()->hasCandidate);
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QVERIFY(legend->text().contains(QStringLiteral("灰色点云")));
        session.activate(1);QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        const auto measured=ScanGeometryCanvasTestAccessV2::heightRangeRevision(*view);
        session.activate(0);QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(*view),measured);
        session.retry();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.activeFrame()->hasCandidate);
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(*view),measured);
        // A collinear three-point retry clears the valid fit, even though the
        // replacement fit fails. Color follows the current candidate, not history.
        session.setThreePoint(true);session.pick(0,false);session.pick(1,false);session.pick(2,false);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);QVERIFY(!session.activeFrame()->hasCandidate);
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());QVERIFY(canvas->overlayOptions().showColorBar);
        session.setThreePoint(false);session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.activeFrame()->hasCandidate);
        QFile source(config.frames.first().path);QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray ply=source.readAll();source.close();ply.replace(" 30\n"," 40\n");
        QVERIFY(source.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(source.write(ply),qint64(ply.size()));source.close();
        session.retry();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(!session.activeFrame()->hasCandidate);
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(40.0f,41.0f));
        QVERIFY(legend->text().contains(QStringLiteral("高度色点云")));
    }
    void completeHeightRangeAndColorSwitchAvoidPointUploads() {
        using Mode=ScanGeometryCanvasV2::DisplayMode;
        ScanGeometryCanvasV2 view;view.resize(500,400);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *canvas=view.canvas();
        const QVector<pointcloud::Point3D> full{{0,0,-10},{1,0,2},{0,1,5},{1,1,20},
            {0,0,std::numeric_limits<float>::quiet_NaN()},{0,0,std::numeric_limits<float>::infinity()}};
        const QVector<pointcloud::Point3D> display{full.at(1),full.at(2)};const QVector<int> mapping{1,2};
        const auto publish=[&](Mode mode) {view.publish(display,mapping,full.size(),{},{},{},false,false,1,full,
                                                     nullptr,nullptr,nullptr,mode,"source-a");};
        publish(Mode::Height);QTRY_VERIFY(canvas->hasRenderedCurrentCloud());
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(-10.0f,20.0f));
        const auto displayRevision=ScanGeometryCanvasTestAccessV2::displayRevision(view);
        const auto regionRevision=ScanGeometryCanvasTestAccessV2::regionRevision(view);
        const auto rangeRevision=ScanGeometryCanvasTestAccessV2::heightRangeRevision(view);
        publish(Mode::UniformGray);QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QVERIFY(!canvas->overlayOptions().showColorBar);QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        publish(Mode::Height);QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        QVERIFY(canvas->overlayOptions().showColorBar);QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        QCOMPARE(ScanGeometryCanvasTestAccessV2::displayRevision(view),displayRevision);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::regionRevision(view),regionRevision);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(view),rangeRevision);
        // Simulate result trimming, including an empty result, always supplying
        // the immutable complete result as the height source.
        view.publish({display.first()},{1},full.size(),{},{},{},false,false,1,full,nullptr,nullptr,nullptr,Mode::Height,"source-a");
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(-10.0f,20.0f));
        view.publish({},{},full.size(),{},{},{},false,false,1,full,nullptr,nullptr,nullptr,Mode::Height,"source-a");
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(-10.0f,20.0f));
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(view),rangeRevision);
        const QVector<pointcloud::Point3D> flat{{0,0,7},{1,1,7}};
        view.publish(flat,{0,1},2,{},{},{},false,false,2,flat,nullptr,nullptr,nullptr,Mode::Height,"source-b");
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(7.0f,8.0f));
        const QVector<pointcloud::Point3D> invalidHeight{full.at(4),full.at(5)};
        view.publish({},{},invalidHeight.size(),{},{},{},false,false,3,invalidHeight,
                     nullptr,nullptr,nullptr,Mode::Height,"source-no-finite-z");
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),qMakePair(0.0f,1.0f));
    }
    void workflowAdvancesFromRealSessionState() {
        QTemporaryDir root;QVERIFY(root.isValid());InputConfigV2 config;
        QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));config.poseOnly=true;
        ScanGeometryWindowV1 w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto &session=ScanGeometryViewTestAccessV2::session(w);
        QString error;QVERIFY2(session.configure(config,&error),qPrintable(error));session.loadRound(0);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);ScanGeometryViewTestAccessV2::refresh(w,true);
        auto *canvas=ScanGeometryViewTestAccessV2::view(w)->canvas();
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        const auto expectGray=[&] {
            return PointCloudCanvasTestAccess::uniformColor(*canvas)==QColor("#59636f")
                &&!canvas->overlayOptions().showColorBar;
        };
        QCOMPARE(ScanGeometryViewTestAccessV2::maximumWorkflowStage(w),1);
        auto *next=w.findChild<QPushButton *>("nextStage");QVERIFY(next->isEnabled());QTest::mouseClick(next,Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),1);
        session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.activeFrame()->hasCandidate);QVERIFY(expectGray());
        QVERIFY(session.confirmCandidate());QVERIFY(expectGray());session.matchRemaining();
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy()&&!session.matching(),60000);
        session.activate(1);QVERIFY(session.activeFrame()->automatic);QVERIFY(expectGray());
        QVERIFY(session.confirmCandidate());ScanGeometryViewTestAccessV2::refresh(w,false);QVERIFY(expectGray());
        QVERIFY(session.canStitch());QVERIFY(next->isEnabled());QTest::mouseClick(next,Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),2);
        session.stitch();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        ScanGeometryViewTestAccessV2::refresh(w,true);QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),3);
        QVERIFY(expectGray());QVERIFY(!PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
        const auto resultRange=PointCloudCanvasTestAccess::heightRange(*canvas);
        const auto rangeRevision=ScanGeometryCanvasTestAccessV2::heightRangeRevision(*ScanGeometryViewTestAccessV2::view(w));
        QVERIFY(w.findChild<QLabel *>("workflowStageTitle")->text().contains(QStringLiteral("确认并保存")));
        session.saveGeometry();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.geometryConfirmed());
        session.erase({0},session.resultRevision());QVERIFY(!session.geometryConfirmed());
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);QVERIFY(expectGray());
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),resultRange);
        session.undo();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);QVERIFY(expectGray());
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),resultRange);
        session.erase({0},session.resultRevision());QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        session.restore();QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QCOMPARE(PointCloudCanvasTestAccess::heightRange(*canvas),resultRange);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::heightRangeRevision(*ScanGeometryViewTestAccessV2::view(w)),rangeRevision);
        QVERIFY(w.findChild<QPushButton *>("previousStage")->isEnabled());
        QTest::mouseClick(w.findChild<QPushButton *>("previousStage"),Qt::LeftButton);
        QCOMPARE(ScanGeometryViewTestAccessV2::workflowStage(w),2);
        session.activate(0);QVERIFY(expectGray());session.clearPlane();
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());QVERIFY(canvas->overlayOptions().showColorBar);
        session.activate(1);QVERIFY(!session.activeFrame()->hasCandidate);
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
    }
    void resultRegionsUseBlueRedAndRebuildWithoutPointUpload() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));config.poseOnly=true;
        ScanGeometryWindowV1 w;auto &s=ScanGeometryViewTestAccessV2::session(w);QString error;
        QVERIFY(s.configure(config,&error));s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        s.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(s.confirmCandidate());
        s.matchRemaining();QTRY_VERIFY_WITH_TIMEOUT(!s.busy()&&!s.matching(),60000);
        s.activate(1);QVERIFY(s.confirmCandidate());
        auto &f=const_cast<SessionFrameV2 &>(s.frames().at(1));
        // Rendering fixture: a confirmed annotation distinct from workpiece indices.
        f.interferenceIndices={625};
        f.interferenceMask.vertices={{0,0,34},{1,0,34},{1,1,34},{0,1,34}};
        f.interferenceMask.triangleIndices={0,1,2,0,2,3};
        f.interferenceMask.boundaryVertices={{0,0,34},{1,0,34},{1,1,34},{0,1,34},{0,0,34}};
        f.interferenceMask.boundaryRanges={{0,5}};
        s.stitch();QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(s.hasResult());
        QCOMPARE(s.resultRegions().state,ResultRegionStateV1::Ready);
        auto *canvas=ScanGeometryViewTestAccessV2::view(w)->canvas();
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));QVERIFY(!canvas->overlayOptions().showColorBar);
        const auto colors=PointCloudCanvasTestAccess::maskColors(*canvas);
        const auto rgba=[](const QColor &c){return QVector4D(c.redF(),c.greenF(),c.blueF(),1);};
        QVERIFY(colors.contains(rgba(QColor("#1675e8"))));QVERIFY(colors.contains(rgba(QColor("#f04452"))));
        for(float opacity:PointCloudCanvasTestAccess::opacities(*canvas))QCOMPARE(opacity,0.25f);
        for(const auto &marker:PointCloudCanvasTestAccess::markers(*canvas))QCOMPARE(marker.w(),0.0f);
        const auto revision=canvas->displayCloudRevision();
        s.rebuildResultRegions();QVERIFY(!w.findChild<QToolButton *>("rectangleTool")->isEnabled());
        QVERIFY(!w.findChild<QPushButton *>("confirmGeometry")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QCOMPARE(canvas->displayCloudRevision(),revision);
        QVERIFY(w.findChild<QLabel *>("resultRegionStatus")->text().contains(QStringLiteral("蓝色工件模板")));
    }
    void unverifiedReloadHidesOldRegionsWithoutPointUpload() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        ScanGeometryWindowV1 w;auto &session=ScanGeometryViewTestAccessV2::session(w);QString error;
        QVERIFY(session.configure(config,&error));session.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);QVERIFY(session.activeFrame()->hasCandidate);
        auto *canvas=ScanGeometryViewTestAccessV2::view(w)->canvas();
        const auto revision=canvas->displayCloudRevision();
        auto &frame=const_cast<SessionFrameV2 &>(session.frames().at(0));
        frame.sourceVerification=SourceVerificationV3::Checking;
        ScanGeometryViewTestAccessV2::refresh(w,true);
        QVERIFY(PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());QVERIFY(canvas->overlayOptions().showColorBar);
        QVERIFY(!w.findChild<QPushButton *>("confirmCandidate")->isEnabled());QVERIFY(!session.confirmCandidate());
        QCOMPARE(canvas->displayCloudRevision(),revision);
        frame.sourceVerification=SourceVerificationV3::Verified;
        ScanGeometryViewTestAccessV2::refresh(w,true);
        QVERIFY(!PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QCOMPARE(canvas->displayCloudRevision(),revision);
    }
    void maskFailureIsVisibleWithoutPointFallback() {
        QTemporaryDir root;QVERIFY(root.isValid());InputConfigV2 config;
        QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        ScanGeometryWindowV1 w;
        auto &session=ScanGeometryViewTestAccessV2::session(w);
        QString error;QVERIFY(session.configure(config,&error));session.loadRound(0);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        session.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),60000);
        QVERIFY(session.activeFrame()->hasCandidate);
        auto &frame=const_cast<SessionFrameV2 &>(session.frames().at(0));
        frame.workpieceMask={};frame.workpieceMaskError=QStringLiteral("synthetic mask failure");
        ScanGeometryViewTestAccessV2::refresh(w,true);
        QVERIFY(w.findChild<QLabel *>("interactionHint")->text().contains(QStringLiteral("平面拟合成功，区域显示失败")));
        QVERIFY(w.findChild<QPushButton *>("confirmCandidate")->isEnabled());
        const auto *canvas=ScanGeometryViewTestAccessV2::view(w)->canvas();
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QVERIFY(!canvas->overlayOptions().showColorBar);
        for(const auto &marker:PointCloudCanvasTestAccess::markers(*canvas))QCOMPARE(marker.w(),0.0f);
    }
    void nativeVisiblePickingAndStableResultMapping() {
        ScanGeometryCanvasV2 view;view.resize(700,500);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *canvas=view.canvas();QTRY_VERIFY(canvas->isValid());
        // Three coincident screen locations, two depths. Full IDs deliberately
        // contain holes, as they do after trimming an immutable result.
        QVector<pointcloud::Point3D> points{{-2,-2,0},{2,-2,0},{0,2,0},{-2,-2,5},{2,-2,5},{0,2,5}};
        view.publish(points,{1,3,5,7,9,11},12,{1,3,5},{7,9,11},{},false,true,0,{},nullptr,nullptr,nullptr,
                     ScanGeometryCanvasV2::DisplayMode::UniformGray);
        canvas->setStandardView(2);QTRY_VERIFY(canvas->hasRenderedCurrentCloud());
        const QImage rendered=canvas->grabFramebuffer();QVERIFY(!rendered.isNull());
        int grayPixels=0;
        for(int y=0;y<rendered.height();++y)for(int x=0;x<rendered.width();++x) {
            const QColor pixel=rendered.pixelColor(x,y);
            if(qAbs(pixel.red()-89)<=1&&qAbs(pixel.green()-99)<=1&&qAbs(pixel.blue()-111)<=1)++grayPixels;
        }
        QVERIFY(grayPixels>0);
        const auto ids=PointCloudCanvasTestAccess::rectangle(*canvas);QVERIFY(!ids.isEmpty());
        const QSet<int> picked(ids.cbegin(),ids.cend());
        for(const auto &pair:{qMakePair(1,7),qMakePair(3,9),qMakePair(5,11)})QVERIFY(!(picked.contains(pair.first)&&picked.contains(pair.second)));
        for(int id:ids)QVERIFY(QVector<int>({1,3,5,7,9,11}).contains(id));
        const auto markers=PointCloudCanvasTestAccess::markers(*canvas);QCOMPARE(markers.size(),6);
        for(const auto &marker:markers)QCOMPARE(marker.w(),0.0f);
        canvas->setControlPointIndices({3,9,4,-1});
        QCOMPARE(PointCloudCanvasTestAccess::controls(*canvas),QVector<int>({1,4}));
        QVERIFY(PointCloudCanvasTestAccess::selected(*canvas).isEmpty());
        view.setTool(ScanGeometryCanvasV2::Tool::Rectangle);
        ScanGeometryCanvasTestAccessV2::rectangle(view,{1,1},{canvas->width()-2,canvas->height()-2});
        QVERIFY(!view.selectedIds().isEmpty());
        QVERIFY(PointCloudCanvasTestAccess::selected(*canvas).isEmpty());
        auto *erase=view.findChild<QPushButton *>("eraseSelection");QVERIFY(erase);
        QCOMPARE(erase->text(),QStringLiteral("擦除 %1 点").arg(view.selectedIds().size()));
        view.clearSelection();QVERIFY(view.selectedIds().isEmpty());QVERIFY(!erase->isVisible());
        ScanGeometryCanvasTestAccessV2::rectangle(view,{1,1},{3,3});
        QVERIFY(view.selectedIds().isEmpty());
        ScanGeometryCanvasTestAccessV2::rectangle(view,{1,1},{10,10});
        QVERIFY(view.selectedIds().isEmpty());QVERIFY(!erase->isEnabled());
        view.publish({points.at(1),points.at(4)},{3,9},12,{3},{9},{},false,false);
        QCOMPARE(PointCloudCanvasTestAccess::mapping(*canvas),QVector<int>({3,9}));
        const auto remappedMarkers=PointCloudCanvasTestAccess::markers(*canvas);QCOMPARE(remappedMarkers.size(),2);
        for(const auto &marker:remappedMarkers)QCOMPARE(marker.w(),0.0f);
        QVERIFY(PointCloudCanvasTestAccess::controls(*canvas).isEmpty());
        view.publish({points.at(0),points.at(1),points.at(2)},{0,1,2},12,{1},{},{},false,false);
        const auto sparseMarkers=PointCloudCanvasTestAccess::markers(*canvas);
        QCOMPARE(sparseMarkers.size(),3);QCOMPARE(sparseMarkers.at(0).w(),0.0f);
        QCOMPARE(sparseMarkers.at(1).w(),0.0f);QCOMPARE(sparseMarkers.at(2).w(),0.0f);
    }
    void fittedMaskOverlayUsesSeparateFullResolutionGeometry() {
        QVector<pointcloud::Point3D> full{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
        QVector<int> indices{0,1,2,3};
        pcv::planefitting::PlaneModel model;model.c=1;
        pcv::planefitting::PlaneMaskOptionsV1 options;options.spacingUMm=1;options.spacingVMm=1;
        const auto mask=pcv::planefitting::buildPlaneMaskV1(full,indices,model,options);
        QVERIFY(mask.ok);
        ScanGeometryCanvasV2 view;view.resize(500,400);
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QVector<pointcloud::Point3D> display{full.at(0),full.at(2)};
        const QVector<int> mapping{0,2};
        view.publish(display,mapping,full.size(),{0,1,2,3},{},{},true,false,1,full,&mask.mesh);
        auto *canvas=view.canvas();
        QTRY_VERIFY(canvas->hasRenderedCurrentCloud());
        QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        const auto displayRevision=ScanGeometryCanvasTestAccessV2::displayRevision(view);
        const auto regionRevision=ScanGeometryCanvasTestAccessV2::regionRevision(view);
        QCOMPARE(PointCloudCanvasTestAccess::maskRanges(*canvas).size(),1);
        QCOMPARE(PointCloudCanvasTestAccess::maskBoundaryRanges(*canvas).size(),1);
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).first().x(),float(QColor("#1675e8").redF()));
        QCOMPARE(PointCloudCanvasTestAccess::mapping(*canvas),QVector<int>({0,2}));
        QCOMPARE(PointCloudCanvasTestAccess::opacities(*canvas).first(),0.25f);
        view.publish(display,mapping,full.size(),{0,1,2,3},{},{},true,false,1,full,&mask.mesh,
                     nullptr,nullptr,ScanGeometryCanvasV2::DisplayMode::UniformGray);
        QCOMPARE(PointCloudCanvasTestAccess::uniformColor(*canvas),QColor("#59636f"));
        QCOMPARE(PointCloudCanvasTestAccess::maskRanges(*canvas).size(),1);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::regionRevision(view),regionRevision);
        QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        view.publish(display,mapping,full.size(),{},{0,1,2,3},{},true,false,1,full,
                     nullptr,&mask.mesh);
        QVERIFY(!PointCloudCanvasTestAccess::uniformColor(*canvas).isValid());
        QCOMPARE(ScanGeometryCanvasTestAccessV2::displayRevision(view),displayRevision);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::regionRevision(view),regionRevision+1);
        QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).first().z(),float(QColor("#f04452").blueF()));
        view.publish(display,mapping,full.size(),{},{0,1,2,3},{},false,false,1,full,nullptr,&mask.mesh);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::regionRevision(view),regionRevision+1);
        view.publish(display,mapping,full.size(),{0,1,2,3},{0,1,2,3},{},true,false,1,full,
                     &mask.mesh,&mask.mesh,nullptr,ScanGeometryCanvasV2::DisplayMode::UniformGray);
        QCOMPARE(PointCloudCanvasTestAccess::maskRanges(*canvas).size(),2);
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).at(0).x(),float(QColor("#1675e8").redF()));
        QCOMPARE(PointCloudCanvasTestAccess::maskColors(*canvas).at(1).x(),float(QColor("#f04452").redF()));
        for(float opacity:PointCloudCanvasTestAccess::opacities(*canvas))QCOMPARE(opacity,0.25f);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::displayRevision(view),displayRevision);
        QVERIFY(!PointCloudCanvasTestAccess::uploadPending(*canvas));
        view.publish(display,mapping,full.size(),{},{},{},false,false,1,full);
        QVERIFY(PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
        auto reloaded=display;reloaded.detach();
        view.publish(reloaded,mapping,full.size(),{},{},{},false,false,1,full);
        QCOMPARE(ScanGeometryCanvasTestAccessV2::displayRevision(view),displayRevision+1);
        canvas->setCloud({full.first()});
        QVERIFY(PointCloudCanvasTestAccess::maskRanges(*canvas).isEmpty());
    }
};
int main(int argc,char **argv) {
    qputenv("QT_OPENGL","desktop");QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);ScanGeometryViewTestsV2 tests;return QTest::qExec(&tests,argc,argv);
}
#include "scan_geometry_view_tests.moc"
