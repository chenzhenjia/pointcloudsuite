#include "mainwindow.h"
#include "features/workpiece_template_dialog_v1.h"
#include <stitchingwindow.h>
#include <pcv/interface/temp_workpiece_interface.h>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
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
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed at " << __LINE__ \
    << ": " << #condition << '\n'; return false; } } while (false)

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
        CHECK(!view.m_currentCloudSource.robotBaseConfirmed);
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
        view.stitchingSucceeded(publication);
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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (!WorkpieceTemplateViewTestAccess::run()) return 1;
    std::cout << "Workpiece template GUI lifecycle regression passed\n";
    return 0;
}
