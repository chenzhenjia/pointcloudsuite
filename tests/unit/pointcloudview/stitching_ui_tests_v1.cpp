#include <stitchingwindow.h>
#ifdef PCV_TEST_STITCHING_VIEW
#include "mainwindow.h"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QStatusBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSemaphore>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <iostream>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "FAILED line " << __LINE__ << ": " << #expression << '\n'; return false; \
} } while (false)

namespace {
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool waitForTask(StitchingWindow &window)
{
    QElapsedTimer timer;
    timer.start();
    while (window.isBusy() && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return !window.isBusy();
}

template<class T> T *control(StitchingWindow &window, const char *name)
{
    return window.findChild<T *>(QString::fromLatin1(name));
}
}

struct StitchingUiTestAccess {
    static void configure(StitchingWindow &window, const QString &root, const QString &output)
    {
        window.addPlyPath(root + "/first.ply");
        control<QLineEdit>(window, "le_calibration_path")->setText(root + "/handeye.xml");
        control<QLineEdit>(window, "le_output_directory")->setText(output);
        auto *table = control<QTableWidget>(window, "tbl_scans");
        table->item(0, 1)->setText("0 0 0 0 0 0");
        table->item(0, 2)->setText("0 10 0 0 0 0");
        control<QComboBox>(window, "cb_processing_mode")->setCurrentIndex(1);
    }

    static bool run()
    {
        QTemporaryDir temporary;
        CHECK(temporary.isValid());
        const QString root = temporary.path();
        CHECK(writeFile(root + "/handeye.xml", R"(<ArithConfig><RTmatDepth2robot>
            <RotMat r00="1" r01="0" r02="0" r10="0" r11="1" r12="0" r20="0" r21="0" r22="1"/>
            <TVec t0="0" t1="0" t2="0"/>
            </RTmatDepth2robot></ArithConfig>)"));
        CHECK(writeFile(root + "/first.ply", "ply\nformat ascii 1.0\nelement vertex 3\n"
            "property float x\nproperty float y\nproperty float z\nend_header\n"
            "1 0 2\n1 5 3\n2 10 4\n"));

        StitchingWindow window;
        CHECK(!window.isBusy());
        CHECK(control<QSpinBox>(window, "sb_sample_stride")->value() == 8);
        CHECK(control<QDoubleSpinBox>(window, "dsb_preview_ratio")->value() == 0.6);
        CHECK(control<QCheckBox>(window, "chk_seam_fusion")->isChecked());
        CHECK(!control<QGroupBox>(window, "gb_advanced")->isChecked());
        CHECK(control<QWidget>(window, "wgt_advanced_content")->isHidden());
        control<QGroupBox>(window, "gb_advanced")->setChecked(true);
        CHECK(!control<QWidget>(window, "wgt_advanced_content")->isHidden());

        configure(window, root, root + "/success");
        window.addPlyPath(root + "/first.ply");
        window.addPlyPath(root + "/second.ply");
        auto *table = control<QTableWidget>(window, "tbl_scans");
        CHECK(table->rowCount() == 2);
        table->selectRow(1);
        const QString secondPose = table->item(1, 1)->text();
        window.moveSelectedRow(-1);
        CHECK(table->item(0, 0)->text().endsWith("second.ply"));
        CHECK(table->item(0, 1)->text() == secondPose);
        window.moveSelectedRow(1);
        table->selectRow(1);
        window.removeSelectedRows();
        CHECK(table->rowCount() == 1);
        QString error;
        CHECK(window.collectInputs(&error).size() == 1 && error.isEmpty());
        table->item(0, 1)->setText("0 0 nan 0 0 0");
        CHECK(window.collectInputs(&error).isEmpty() && !error.isEmpty());
        table->item(0, 1)->setText("0,0,0,0,0,0");
        error.clear();
        CHECK(window.collectInputs(&error).size() == 1 && error.isEmpty());
        control<QComboBox>(window, "cb_processing_mode")->setCurrentIndex(0);
        control<QSpinBox>(window, "sb_sample_stride")->setValue(3);
        control<QDoubleSpinBox>(window, "dsb_max_translation")->setValue(9);
        control<QCheckBox>(window, "chk_diagnostic_output")->setChecked(true);
        auto options = window.collectIcpOptions(&error);
        CHECK(options.enabled && options.sampleStride == 3);
        CHECK(options.maximumCorrectionTranslation == 9);
        CHECK(options.collectDiagnosticFrames && options.tangentCoarseAlignmentEnabled
              && options.wideStructureDiagnosticEnabled);
        window.addPlyPath(root + "/second.ply");
        control<QCheckBox>(window, "chk_parallel_processing")->setChecked(false);
        control<QSpinBox>(window, "sb_parallel_tasks")->setValue(2);
        control<QDoubleSpinBox>(window, "dsb_mutual_distance")->setValue(0.4);
        control<QDoubleSpinBox>(window, "dsb_decision_voxel")->setValue(0.3);
        const auto request = window.collectRequest(&error);
        CHECK(error.isEmpty() && request.input.frames.size() == 2);
        CHECK(request.registration.options.enabled && request.registration.sampleStride == 3);
        CHECK(!request.registration.parallelEnabled && request.registration.maxParallelTasks == 2);
        CHECK(request.registration.collectDiagnosticFrames && request.artifacts.writeSourceClouds
              && request.artifacts.writeDiagnosticFrames);
        CHECK(request.seam.enabled && request.seam.halfWidthMm == 8.0f);
        CHECK(request.seam.mutualDistanceMm == 0.4f && request.seam.decisionCellSizeMm == 0.3f);
        CHECK(request.artifacts.previewRatio == 0.6 && !request.artifacts.allowOverwrite);
        CHECK(request.input.calibrationPath == root + "/handeye.xml");
        CHECK(request.input.frames.first().endPose.y == 10);
        table->selectRow(1);
        window.removeSelectedRows();
        control<QLineEdit>(window, "le_voxel_levels")->setText("1,nan,0.2");
        window.collectIcpOptions(&error);
        CHECK(!error.isEmpty());
        control<QLineEdit>(window, "le_voxel_levels")->setText("1,0.5,0.2");
        control<QComboBox>(window, "cb_processing_mode")->setCurrentIndex(1);
        error.clear();
        CHECK(!window.collectIcpOptions(&error).enabled && error.isEmpty());
        control<QCheckBox>(window, "chk_diagnostic_output")->setChecked(false);
        const auto transformRequest = window.collectRequest(&error);
        CHECK(error.isEmpty() && !transformRequest.registration.options.enabled);
        CHECK(!transformRequest.seam.enabled && !transformRequest.artifacts.writeSourceClouds);

        int publications = 0;
        StitchingPublication publication;
        QObject::connect(&window, &StitchingWindow::stitchingSucceeded,
            &window, [&](const StitchingPublication &result) { ++publications; publication = result; });
        window.startStitching();
        CHECK(window.isBusy());
        auto *watcher = window.m_watcher;
        window.startStitching();
        CHECK(window.m_watcher == watcher);
        CHECK(waitForTask(window));
        CHECK(publications == 1 && publication.points.size() == 3);
        CHECK(publication.points.at(1).x == 1 && publication.points.at(1).y == 5);
        CHECK(QFileInfo::exists(publication.formalPly));
        CHECK(QFileInfo::exists(publication.previewPly));
        CHECK(QFileInfo::exists(publication.mappingFile));
        CHECK(QJsonDocument::fromJson(readFile(publication.reportJson)).object()["success"].toBool());

        int prompts = 0;
        int response = QMessageBox::No;
        QTimer dialogs;
        QObject::connect(&dialogs, &QTimer::timeout, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(widget);
                if (box && box->isVisible()) { ++prompts; box->done(response); }
            }
        });
        dialogs.start(1);
        const QByteArray formal = readFile(publication.formalPly);
        window.startStitching(); // Refuse replacement of an existing result.
        CHECK(!window.isBusy() && prompts == 1 && publications == 1);
        CHECK(readFile(publication.formalPly) == formal);

        response = QMessageBox::Yes;
        auto *pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int oldThreadCount = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore started, release;
        auto blocker = QtConcurrent::run([&] { started.release(); release.acquire(); });
        started.acquire();
        window.startStitching();
        window.cancelStitching(); // Cancellation is set before queued completion can publish.
        release.release();
        blocker.waitForFinished();
        CHECK(waitForTask(window));
        pool->setMaxThreadCount(oldThreadCount);
        CHECK(publications == 1 && readFile(publication.formalPly) == formal);

        CHECK(writeFile(root + "/blocked", "not a directory"));
        control<QLineEdit>(window, "le_output_directory")->setText(root + "/blocked");
        window.startStitching();
        CHECK(waitForTask(window));
        CHECK(publications == 1 && readFile(publication.formalPly) == formal);

        CHECK(writeFile(root + "/broken.ply", "invalid ply"));
        window.clearFiles();
        window.addPlyPath(root + "/broken.ply");
        table->item(0, 1)->setText("0 0 0 0 0 0");
        table->item(0, 2)->setText("0 10 0 0 0 0");
        control<QLineEdit>(window, "le_output_directory")->setText(root + "/failure");
        window.startStitching();
        CHECK(waitForTask(window));
        CHECK(publications == 1 && !QFileInfo::exists(root + "/failure/stitched_robot_base.ply"));

        QWidget parent;
        StitchingWindow child(&parent);
        configure(child, root, root + "/closing");
        child.show();
        child.startStitching();
        child.close();
        CHECK(child.isHidden() && child.isBusy());
        child.cancelAndWait();
        CHECK(!child.isBusy());
        QApplication::processEvents();
        return true;
    }
};

#ifdef PCV_TEST_STITCHING_VIEW
struct StitchingViewTestAccess {
    static bool run()
    {
        MainWindow view;
        auto *memoryLabel = view.findChild<QLabel *>("lbl_memory_info");
        CHECK(memoryLabel && !memoryLabel->text().isEmpty());
        CHECK(memoryLabel->toolTip().contains(QStringLiteral("工作集")));
        CHECK(memoryLabel->toolTip().contains(QStringLiteral("pointcloudview.exe")));
        auto *memoryTimer = view.findChild<QTimer *>("memory_monitor_timer");
        CHECK(memoryTimer && memoryTimer->interval() == 1000);
        CHECK(memoryTimer->timerType() == Qt::CoarseTimer && memoryTimer->isActive());
        CHECK(memoryLabel->parentWidget() == view.statusBar());
        CHECK(memoryLabel->sizePolicy().horizontalPolicy() == QSizePolicy::Minimum);
#ifdef Q_OS_WIN
        CHECK(memoryLabel->text().contains(QStringLiteral("工作集")));
        CHECK(memoryLabel->text().contains(QStringLiteral("私有提交")));
#endif
        // A temporary operation message must not replace the permanent readout.
        view.statusBar()->showMessage(QStringLiteral("memory monitor test"));
        memoryLabel->setText(QStringLiteral("waiting for sample"));
        QElapsedTimer refreshWait;
        refreshWait.start();
        while (memoryLabel->text() == QStringLiteral("waiting for sample")
               && refreshWait.elapsed() < 5000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        CHECK(memoryLabel->text() != QStringLiteral("waiting for sample"));
        CHECK(view.statusBar()->currentMessage() == QStringLiteral("memory monitor test"));
        CHECK(!memoryLabel->isHidden());
        const QString canvasInfo = view.m_canvasInfo->text();
        view.m_canvasInfo->setText(QStringLiteral("当前显示 6,300,000 个点 · 原始 6,300,000 个点 · 全量直接标记"));
        auto *bar = view.statusBar();
        bar->ensurePolished();
        bar->resize(1040, bar->sizeHint().height());
        bar->layout()->activate();
        CHECK(memoryLabel->width() >= memoryLabel->sizeHint().width());
        CHECK(memoryLabel->geometry().right() < bar->width());
        CHECK(view.m_canvasInfo->width() > 1);
        CHECK(view.m_canvasInfo->geometry().right() < memoryLabel->geometry().left());
        view.m_canvasInfo->setText(canvasInfo);
        CHECK(view.m_stitchingWindow == nullptr);
        view.openMultiFrameRegistration();
        auto *window = view.m_stitchingWindow;
        CHECK(window && window->isWindow() && window->windowModality() == Qt::NonModal);
        window->close();
        CHECK(window->isHidden());
        view.openMultiFrameRegistration();
        CHECK(view.m_stitchingWindow == window);
        CHECK(view.findChild<QPushButton *>("btn_multiframe_registration")->isVisibleTo(&view));
        view.m_rawPoints = {{1, 2, 3}};
        view.publishCanvasCache(view.m_rawPoints);
        view.m_tempWorkpieceSource = MainWindow::TempWorkpieceSource::Ply;
        view.m_tempWorkpieceSessionRevision = 7;
        view.m_tempWorkpieceScanId = "old";
        view.m_tempWorkpieceOutputDirectory = "old-output";
        view.m_threePlaneResult.ok = true;
        view.m_threePlaneResult.planeIndices = {0};
        view.m_workpieceCoordinate.valid = true;
        view.m_planeEdgeResult.ok = true;
        view.m_planeImageResult.ok = true;
        view.m_textureImagePath = "old.png";
        const quint64 revision = view.m_canvasRevision;
        StitchingPublication result;
        view.stitchingSucceeded(result);
        CHECK(view.m_canvasRevision == revision && view.hasTempWorkpieceSession());
        result.points = {{10, 20, 30}, {40, 50, 60}};
        result.formalPly = "C:/output/stitched_robot_base.ply";
        result.previewPly = "preview.ply";
        result.mappingFile = "mapping.pcvmap";
        result.reportJson = "manifest.json";
        result.outputDirectory = "C:/output";
        emit window->stitchingSucceeded(result);
        CHECK(view.m_points.size() == 2 && view.m_rawPoints.size() == 2);
        CHECK(view.m_points.at(0).x == 10 && view.m_canvasRevision > revision);
        CHECK(!view.hasTempWorkpieceSession());
        CHECK(!view.m_threePlaneResult.ok && !view.m_workpieceCoordinate.valid);
        CHECK(!view.m_planeEdgeResult.ok && !view.m_planeImageResult.ok);
        CHECK(view.m_textureImagePath.isEmpty() && view.m_stitchingArtifactPaths.size() == 4);
        CHECK(view.m_pendingPath == result.formalPly);
        view.close();
        CHECK(!memoryTimer->isActive());
        memoryLabel->setText(QStringLiteral("closed"));
        CHECK(QMetaObject::invokeMethod(&view, "updateMemoryUsage", Qt::DirectConnection));
        CHECK(memoryLabel->text() == QStringLiteral("closed"));
        QApplication::processEvents();
        // Direct destruction (including application quit) also owns the timer.
        auto *destroyedView = new MainWindow;
        QPointer<QTimer> destroyedTimer = destroyedView->findChild<QTimer *>("memory_monitor_timer");
        CHECK(destroyedTimer && destroyedTimer->isActive());
        delete destroyedView;
        CHECK(destroyedTimer.isNull());
        return true;
    }
};
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (!StitchingUiTestAccess::run()) return 1;
#ifdef PCV_TEST_STITCHING_VIEW
    if (!StitchingViewTestAccess::run()) return 1;
#endif
    std::cout << "Stitching UI regression passed\n";
    return 0;
}
