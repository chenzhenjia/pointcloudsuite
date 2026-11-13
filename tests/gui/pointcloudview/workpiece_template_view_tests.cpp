#include "mainwindow.h"
#include "workbench_shell_v1.h"
#include "features/workpiece_template_dialog_v1.h"
#include <stitchingwindow.h>
#include <pcv/workpiece/temp_workpiece_interface.h>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <pcv/render/pointcloud_canvas.h>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { const QString failure = QString("CHECK failed at %1: %2").arg(__LINE__).arg(#condition); \
    std::cerr << failure.toStdString() << '\n'; \
    QFile report(qEnvironmentVariable("PCV_WORKBENCH_REPORT")); if (report.open(QIODevice::WriteOnly | QIODevice::Append)) report.write(failure.toUtf8() + '\n'); \
    return false; } } while (false)

struct PointCloudCanvasTestAccess {
    static bool hasTexture(const PointCloudCanvas &canvas) {
        for (const auto value : canvas.m_textureValid) if (value) return true;
        return false;
    }
};
struct WorkpieceTemplateViewTestAccess {
    static bool waitIdle(MainWindow &view)
    {
        QElapsedTimer timer;
        timer.start();
        while (view.m_templateTaskActive || (view.m_noiseWatcher && view.m_noiseWatcher->isRunning())) {
            if (timer.elapsed() > 15000) return false;
            QApplication::processEvents();
            QThread::msleep(1);
        }
        QApplication::processEvents();
        return true;
    }

    static bool run()
    {
        using namespace pcv::workpiece;
        // The production window and this target must compile the same QRC.
        // Merely listing a .qrc source without AUTORCC leaves these unavailable.
        for (const char *icon : {"device", "layout", "transform", "file", "save", "stream", "reset"}) {
            const QString path = QStringLiteral(":/pointcloudview/icons/%1.svg").arg(QString::fromLatin1(icon));
            CHECK(QFile::exists(path));
            CHECK(!QPixmap(path).isNull());
        }
        QTemporaryDir temporary;
        CHECK(temporary.isValid());
        SaveWorkpieceTemplateRequest request;
        request.name = QString::fromUtf8("测试工件");
        request.outputRoot = temporary.path();
        request.coordinateFrame = "robot_base";
        request.points = {{1, 0, 2, 0, 0, 1}, {2, 10, 3, 0, 0, 0}, {-1, 5, 4, 0, 1, 0}};
        const auto snapshot = request.points;
        const auto saved = saveWorkpieceTemplate(request);
        CHECK(saved.success);
        WorkpieceTemplateDialogV1 form(saved.info, temporary.path());
        const QString screenshot = qEnvironmentVariable("PCV_TEMPLATE_FORM_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            form.show();
            QApplication::processEvents();
            CHECK(form.grab().save(screenshot));
            form.hide();
        }
        auto *buttons = form.findChild<QDialogButtonBox *>("template_buttons");
        auto *confirm = form.findChild<QCheckBox *>("template_baseline_confirmed");
        auto *name = form.findChild<QLineEdit *>("template_name");
        CHECK(buttons && confirm && name && !buttons->button(QDialogButtonBox::Save)->isEnabled());
        confirm->setChecked(true);
        CHECK(buttons->button(QDialogButtonBox::Save)->isEnabled());
        name->setText(" ");
        CHECK(!buttons->button(QDialogButtonBox::Save)->isEnabled());
        name->setText("valid");
        confirm->setChecked(false);
        CHECK(!buttons->button(QDialogButtonBox::Save)->isEnabled());

        MainWindow view;
        view.m_rawPoints = snapshot;
        view.publishCanvasCache(snapshot);
        CHECK(!view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.coordinateFrame != CloudCoordinateFrameV1::RobotBase);
        const auto initialRevision = view.m_canvasRevision;
        const auto initialPoints = view.m_points;
        view.startWorkpieceTemplateLoad(saved.infoFile);
        CHECK(view.pointTaskRunning());
        view.m_templateCancelToken->store(true);
        CHECK(waitIdle(view));
        CHECK(view.m_canvasRevision == initialRevision && view.m_points.constData() == initialPoints.constData());
        CHECK(!view.workpieceTemplateReady());

        // Failed input must leave the source and current point buffer intact.
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
        });
        dismiss.start(5);
        view.startWorkpieceTemplateLoad(temporary.path() + "/missing.json");
        CHECK(waitIdle(view));
        CHECK(view.m_canvasRevision == initialRevision && !view.workpieceTemplateReady());

        view.m_workpieceCoordinate.valid = true;
        view.m_scanTrajectoryResult.ok = true;
        view.m_textureExperimentActive = true;
        view.startWorkpieceTemplateLoad(saved.infoFile);
        CHECK(waitIdle(view));
        CHECK(view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.entry == "workpiece_template");
        CHECK(!view.m_workpieceCoordinate.valid && !view.m_scanTrajectoryResult.ok);
        CHECK(!view.m_textureExperimentActive && view.m_threePlaneResult.planeIndices.isEmpty());
        CHECK(view.m_points.size() == snapshot.size() && view.m_rawPoints.size() == snapshot.size());
        CHECK(view.m_currentCloudSource.manifest == saved.infoFile);

        // Preserve coordinates through processing, then restore the raw provenance.
        view.m_voxelNoise->setChecked(false);
        view.m_statisticalNoise->setChecked(false);
        view.applyNoiseRemoval();
        CHECK(waitIdle(view));
        CHECK(view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.processingSteps.size() == 1);
        view.publishCanvasCache(view.m_rawPoints, false, view.m_rawCloudSource);
        CHECK(view.workpieceTemplateReady() && view.m_currentCloudSource.processingSteps.isEmpty());

        // Saving cannot replace the current cloud or mutate the shared snapshot.
        const auto beforeSave = view.m_canvasRevision;
        const auto raw = view.m_rawPoints;
        request.source = view.m_currentCloudSource.templateSource();
        view.startWorkpieceTemplateSave(request);
        CHECK(view.pointTaskRunning() && !view.workpieceTemplateReady());
        CHECK(waitIdle(view));
        CHECK(view.m_canvasRevision == beforeSave && view.m_rawPoints.constData() == raw.constData());
        CHECK(view.workpieceTemplateReady());

        view.m_livePreviewPointCount = 3;
        CHECK(!view.workpieceTemplateReady());
        view.m_livePreviewPointCount = 0;
        ++view.m_cloudSourceRevision;
        CHECK(!view.workpieceTemplateReady());
        view.m_cloudSourceRevision = view.m_canvasRevision;
        CHECK(view.workpieceTemplateReady());

        // A stale/cancelled load is rejected even when the module result is valid.
        const auto loaded = loadWorkpieceTemplate({saved.infoFile});
        CHECK(loaded.success);
        view.m_templateInputRevision = view.m_canvasRevision - 1;
        CHECK(!view.publishWorkpieceTemplateResult(loaded));
        view.m_templateInputRevision = view.m_canvasRevision;
        view.m_templateCancelToken->store(true);
        CHECK(!view.publishWorkpieceTemplateResult(loaded));
        view.m_templateCancelToken->store(false);

        // The coordinate state does not depend on a temporary-workpiece session.
        StitchingPublication publication;
        publication.points = snapshot;
        publication.formalPly = saved.pointCloudFile;
        publication.reportJson = "stitching_manifest.json";
        publication.outputDirectory = temporary.path();
        publication.intensitySourceDirectory = temporary.filePath("capture-images");
        publication.textureGrayscale.fill(128, snapshot.size());
        publication.textureValid.fill(1, snapshot.size());
        publication.textureMappedPointCount = snapshot.size();
        const auto beforePublication = view.m_canvasRevision;
        view.stitchingSucceeded(publication);
        CHECK(view.m_canvasRevision > beforePublication && view.m_colorMode->currentIndex() == 0);
        CHECK(PointCloudCanvasTestAccess::hasTexture(*view.m_canvas));
        view.m_colorMode->setCurrentIndex(3);
        CHECK(PointCloudCanvasTestAccess::hasTexture(*view.m_canvas));
        publication.textureGrayscale.clear(); publication.textureValid.clear();
        publication.textureMappedPointCount = 0;
        view.stitchingSucceeded(publication);
        CHECK(!PointCloudCanvasTestAccess::hasTexture(*view.m_canvas) && view.m_colorMode->currentIndex() != 3);
        CHECK(view.statusBar()->currentMessage().contains(publication.intensitySourceDirectory));
        CHECK(view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.entry == "stitching_robot_base");
        view.invalidateTempWorkpieceSession();
        CHECK(view.workpieceTemplateReady());

        pcv::interface::TempWorkpiecePreparation preparation;
        preparation.success = true;
        preparation.coordinateFrame = "robot_base";
        preparation.robotBasePoints = snapshot;
        preparation.resolvedPointCloudFile = "scan.ply";
        preparation.resolvedCalibrationFile = "handeye.xml";
        preparation.outputDirectory = temporary.path();
        preparation.scanId = "scan-test";
        preparation.declaredPointCount = snapshot.size();
        preparation.sourceIndices = {0, 1, 2};
        view.m_pendingTempWorkpieceSource = MainWindow::TempWorkpieceSource::Ply;
        view.m_tempWorkpieceWatcher = new QFutureWatcher<pcv::interface::TempWorkpiecePreparation>(&view);
        view.m_tempWorkpieceWatcher->setFuture(QtConcurrent::run([preparation] { return preparation; }));
        view.m_tempWorkpieceWatcher->waitForFinished();
        view.tempWorkpieceFinished();
        CHECK(view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.entry == "handeye_conversion");
        view.invalidateTempWorkpieceSession();
        CHECK(view.workpieceTemplateReady());

        // An unknown replacement clears eligibility, regardless of its file name or WObj1.
        view.m_pendingPath = "already_robot_base.ply";
        view.publishCanvasCache(snapshot);
        view.m_workpieceCoordinate.valid = true;
        CHECK(!view.workpieceTemplateReady());

        // Closing joins a load; queued completions cannot publish after shutdown.
        const auto closeRevision = view.m_canvasRevision;
        view.startWorkpieceTemplateLoad(saved.infoFile);
        CHECK(view.m_templateTaskActive);
        view.close();
        CHECK(view.m_closing && !view.m_templateWatcher->isRunning());
        QApplication::processEvents();
        CHECK(view.m_canvasRevision == closeRevision);
        auto *destroyed = new MainWindow;
        destroyed->startWorkpieceTemplateLoad(saved.infoFile);
        QPointer<QFutureWatcher<WorkpieceTemplateResult>> watcher = destroyed->m_templateWatcher;
        delete destroyed;
        CHECK(watcher.isNull());
        return true;
    }
};

struct CadTemplateViewTestAccess {
    static bool run() {
        using namespace pcv::workpiece;
        MainWindow view;
        CadCloudResult result; result.success=true;
        result.points={{1,2,3,0,0,1},{2,3,4,0,1,0}};
        const QJsonObject cad{{"original_step","gone.step"},{"sha256",QString(64,'a')},
            {"original_units","millimetre"},{"unit_scale_to_mm",1.0},{"occt_version","7.9.3"},
            {"algorithm_version","cad_surface_area_v1"},{"selected_faces",QJsonArray{QJsonObject{{"id",1},{"surface_type","plane"},{"area_mm2",.5}}}},
            {"spacing_mm",.5},{"geometry_tolerance_mm",.01},{"generated_point_count",2},{"edge_sampling",false}};
        result.source={{"files",QJsonArray{"gone.step"}},{"cad",cad},{"processing_steps",QJsonArray{}}};
        const auto revision=view.m_canvasRevision;
        CHECK(!view.publishCadCloud(result,revision+1));
        CHECK(view.publishCadCloud(result,revision));
        CHECK(view.cadSourceActive() && view.workpieceTemplateReady());
        CHECK(!view.m_pickPointsButton->isEnabled());
        CHECK(view.m_rawCloudSource.coordinateFrame==CloudCoordinateFrameV1::CadModel);
        view.startPlanePointSelection(); CHECK(!view.m_threePointSelectionActive);
        view.planScanTrajectory(); CHECK(!view.m_scanPlanningWatcher);
        view.openProcessJob(); CHECK(!view.m_processJob);
        view.m_voxelNoise->setChecked(true);view.m_voxelSize->setValue(100);view.m_statisticalNoise->setChecked(false);view.applyNoiseRemoval();
        CHECK(WorkpieceTemplateViewTestAccess::waitIdle(view));
        CHECK(view.cadSourceActive() && view.m_currentCloudSource.cad==cad && view.m_points.size()==1);
        CHECK(view.m_currentCloudSource.processingSteps.size()==1);
        view.publishCanvasCache(view.m_rawPoints,false,view.m_rawCloudSource);
        CHECK(view.cadSourceActive() && view.m_currentCloudSource.processingSteps.isEmpty());
        QTemporaryDir root;CHECK(root.isValid());
        SaveWorkpieceTemplateRequest save;save.name="CAD";save.outputRoot=root.path();save.coordinateFrame="cad_model";
        save.points=view.m_points;save.source=view.m_currentCloudSource.templateSource();
        const auto saved=saveCadTemplate(save);CHECK(saved.success);
        view.startWorkpieceTemplateLoad(saved.infoFile);CHECK(WorkpieceTemplateViewTestAccess::waitIdle(view));
        CHECK(view.cadSourceActive() && view.workpieceTemplateReady());
        CHECK(view.m_currentCloudSource.cad==cad);
        CHECK(!view.m_workpieceCoordinate.valid && !view.m_scanTrajectoryResult.ok);
        view.publishCanvasCache(result.points);CHECK(!view.cadSourceActive() && view.m_pickPointsButton->isEnabled());
        return true;
    }
};

struct WorkbenchViewTestAccess {
    static bool run(bool native) {
        std::cerr << "workbench-test-start native=" << native << '\n';
        MainWindow view;
        view.resize(1280, 720);
        view.show(); QApplication::processEvents();
        CHECK(view.m_workbench && view.m_pointSize->value() == 2);
        const int cameraPage=view.m_workbench->pagePicker->findData("camera");
        view.m_workbench->setPageEnabled("camera",false);
        CHECK(!(view.m_workbench->pagePicker->model()->flags(view.m_workbench->pagePicker->model()->index(cameraPage,0)) & Qt::ItemIsEnabled));
        view.m_workbench->setPageEnabled("camera",true);
        CHECK(view.m_workbench->pagePicker->model()->flags(view.m_workbench->pagePicker->model()->index(cameraPage,0)) & Qt::ItemIsEnabled);
        for(auto *action:view.m_canvas->actions())
            CHECK(action->shortcutContext()==Qt::WidgetWithChildrenShortcut);
        for(auto *action:view.actions())
            CHECK(action->shortcut()!=QKeySequence(Qt::Key_Backspace));
        CHECK(!view.findChild<QWidget *>("wgt_source_panel")->isVisible());
        const QSize canvasSize = view.m_canvas->size();
        const QSize clientSize = view.centralWidget()->size();
        const double ratio = double(canvasSize.width()) * canvasSize.height()
            / qMax(1, clientSize.width() * clientSize.height());
        std::cerr << "workbench-test-ratio=" << ratio << " canvas=" << canvasSize.width() << "x" << canvasSize.height() << " client=" << clientSize.width() << "x" << clientSize.height() << '\n';
        std::cout << "preview_ratio=" << ratio << " canvas=" << canvasSize.width() << "x" << canvasSize.height() << '\n';
        CHECK(ratio >= .85);
        view.showCleanDialog(); QApplication::processEvents();
        CHECK(view.m_workbench->drawerVisible());
        CHECK(view.m_canvas->size() == canvasSize);
        const double voxel = view.m_voxelSize->value();
        view.m_voxelSize->setValue(voxel + .1);
        view.m_workbench->hideDrawer(); view.showCleanDialog();
        CHECK(view.m_voxelSize->value() == voxel + .1);
        const auto revision = view.m_canvasRevision;
        view.activateWorkbenchPage("selection");
        auto *selection = view.m_selectedPlanePage; CHECK(selection && !selection->isWindow());
        view.activateWorkbenchPage("main"); view.activateWorkbenchPage("selection");
        CHECK(view.m_selectedPlanePage == selection);
        view.openMultiFrameRegistration();
        CHECK(view.m_stitchingWindow && !view.m_stitchingWindow->isWindow());
        view.activateWorkbenchPage("main");
        CHECK(view.m_canvasRevision == revision);
        QVector<pointcloud::Point3D> cloud;
        for (int y = -80; y <= 80; ++y) for (int x = -110; x <= 110; ++x)
            cloud.append({float(x), float(y), float(x + 110) / 8.0f, 0, 0, 1});
        view.m_rawPoints = cloud;
        view.publishCanvasCache(cloud);
        const auto options = view.m_canvas->overlayOptions();
        CHECK(options.showColorBar && options.showOrientation && options.showCoordinateAxes);
        CHECK(!options.showFloorGrid && !options.showBoundingBox);
        CHECK(view.m_mapMax->value() > view.m_mapMin->value());
        const QString output = qEnvironmentVariable("PCV_WORKBENCH_SCREENSHOT_DIR");
        if (native) {
            if (!output.isEmpty()) { QDir().mkpath(output); view.grab().save(QDir(output).filePath("before_v3.png")); }
            std::cerr << "canvas-visible=" << view.m_canvas->isVisible() << " window=" << view.m_canvas->window()->objectName().toStdString() << '\n';
            QElapsedTimer timer; timer.start();
            while (!view.m_canvas->hasRenderedCurrentCloud() && timer.elapsed() < 5000) {
                QApplication::processEvents(); QThread::msleep(5);
            }
            CHECK(view.m_canvas->isValid() && view.m_canvas->hasRenderedCurrentCloud());
            if (!output.isEmpty()) {
                QDir().mkpath(output);
                CHECK(view.grab().save(QDir(output).filePath("main_v3.png")));
                view.showCleanDialog(); QApplication::processEvents();
                CHECK(view.grab().save(QDir(output).filePath("drawer_v3.png")));
                view.activateWorkbenchPage("selection"); QApplication::processEvents();
                CHECK(view.grab().save(QDir(output).filePath("selection_v3.png")));
                view.openMultiFrameRegistration(); QApplication::processEvents();
                CHECK(view.grab().save(QDir(output).filePath("stitch_v3.png")));
            }
        }
        view.activateWorkbenchPage("main"); view.showPlaneDialog();
        view.startPlanePointSelection(); QApplication::processEvents();
        CHECK(!view.m_workbench->drawerVisible());
        CHECK(view.m_operationBar->isVisible());
        view.cancelPickingOperation(); QApplication::processEvents();
        CHECK(!view.m_operationBar->isVisible());
        CHECK(view.m_workbench->drawerVisible());
        view.close(); QApplication::processEvents();
        return true;
    }
};

int main(int argc, char **argv)
{
    const bool native = argc > 1 && QByteArray(argv[1]) == "--native-ui";
    if (native) {
        qputenv("QT_OPENGL", "desktop"); QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
        QSurfaceFormat format; format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile);
        format.setDepthBufferSize(24); QSurfaceFormat::setDefaultFormat(format);
    }
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (!WorkbenchViewTestAccess::run(native)) return 3;
    if (native) return 0;
    if (!WorkpieceTemplateViewTestAccess::run()) return 1;
    if (!CadTemplateViewTestAccess::run()) return 2;
    std::cout << "Workpiece template GUI lifecycle regression passed\n";
    return 0;
}
