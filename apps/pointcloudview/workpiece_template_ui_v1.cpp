#include "mainwindow.h"
#include "features/workpiece_template_dialog_v1.h"
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
#include "cameradialog.h"
#endif
#include <pcv/render/pointcloud_canvas.h>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>

struct WorkpieceTemplateTaskProgressV1 {
    std::atomic<pcv::workpiece::WorkpieceTemplateStage> stage{
        pcv::workpiece::WorkpieceTemplateStage::ValidatingPoints};
    std::atomic<qsizetype> completed{0};
    std::atomic<qsizetype> total{0};
};

namespace {
QString stageText(pcv::workpiece::WorkpieceTemplateStage stage)
{
    using Stage = pcv::workpiece::WorkpieceTemplateStage;
    switch (stage) {
    case Stage::ValidatingPoints: return QObject::tr("校验模板点云…");
    case Stage::WritingCloud: return QObject::tr("保存模板点云…");
    case Stage::WritingMetadata: return QObject::tr("保存模板信息…");
    case Stage::Committing: return QObject::tr("提交模板版本…");
    case Stage::ReadingMetadata: return QObject::tr("读取模板信息…");
    case Stage::ReadingCloud: return QObject::tr("读取模板点云…");
    }
    return {};
}

pcv::workpiece::WorkpieceTemplateProgress progressCallback(
    const std::shared_ptr<WorkpieceTemplateTaskProgressV1> &progress)
{
    return [progress](pcv::workpiece::WorkpieceTemplateStage stage, qsizetype completed, qsizetype total) {
        progress->completed.store(completed);
        progress->total.store(total);
        progress->stage.store(stage);
    };
}
} // namespace

bool MainWindow::workpieceTemplateReady() const
{
    return !m_closing && !pointTaskRunning() && m_livePreviewPointCount == 0
        && !m_points.isEmpty() && m_currentCloudSource.robotBaseConfirmed
        && m_cloudSourceRevision == m_canvasRevision;
}

void MainWindow::createWorkpieceTemplate()
{
    if (!workpieceTemplateReady()) {
        statusBar()->showMessage(tr("创建模板需要空闲状态下已确认的 robot_base 点云；请先完成手眼转换或拼接"));
        return;
    }
    pcv::workpiece::WorkpieceTemplateInfo summary;
    summary.name = QFileInfo(m_currentCloudSource.files.value(0)).completeBaseName();
    summary.pointCount = m_points.size();
    summary.source = m_currentCloudSource.templateSource();
    bool hasBounds = false;
    const QVector<pointcloud::Point3D> &points = m_points;
    for (const auto &p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        if (!hasBounds) { summary.minimum = summary.maximum = QVector3D(p.x, p.y, p.z); hasBounds = true; }
        else {
            summary.minimum.setX(std::min(summary.minimum.x(), p.x));
            summary.minimum.setY(std::min(summary.minimum.y(), p.y));
            summary.minimum.setZ(std::min(summary.minimum.z(), p.z));
            summary.maximum.setX(std::max(summary.maximum.x(), p.x));
            summary.maximum.setY(std::max(summary.maximum.y(), p.y));
            summary.maximum.setZ(std::max(summary.maximum.z(), p.z));
        }
    }
    const quint64 revision = m_canvasRevision;
    WorkpieceTemplateDialogV1 dialog(summary, pcv::workpiece::defaultWorkpieceTemplateRoot(), this);
    m_templateFormActive = true;
    const int accepted = dialog.exec();
    m_templateFormActive = false;
    if (accepted != QDialog::Accepted || !workpieceTemplateReady() || revision != m_canvasRevision) return;
    pcv::workpiece::SaveWorkpieceTemplateRequest request;
    request.name = dialog.templateName();
    request.outputRoot = dialog.outputRoot();
    request.coordinateFrame = QStringLiteral("robot_base");
    request.points = m_points;
    request.source = m_currentCloudSource.templateSource();
    startWorkpieceTemplateSave(std::move(request));
}

void MainWindow::openWorkpieceTemplate()
{
    if (m_closing || workflowBlocksNewSource()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("打开工件模板"),
        pcv::workpiece::defaultWorkpieceTemplateRoot(), tr("工件模板 JSON (*.json)"));
    if (path.isEmpty() || m_closing || workflowBlocksNewSource()) return;
    if (!confirmReplaceTempWorkpieceSession()) return;
    startWorkpieceTemplateLoad(path);
}

void MainWindow::prepareWorkpieceTemplateTask(bool loading)
{
    m_templateTaskActive = true;
    m_templateLoading = loading;
    m_templateInputRevision = m_canvasRevision;
    m_templateCancelToken = std::make_shared<std::atomic_bool>(false);
    m_templateProgress = std::make_shared<WorkpieceTemplateTaskProgressV1>();
    if (!m_templateWatcher) {
        m_templateWatcher = new QFutureWatcher<pcv::workpiece::WorkpieceTemplateResult>(this);
        connect(m_templateWatcher, &QFutureWatcher<pcv::workpiece::WorkpieceTemplateResult>::finished,
                this, &MainWindow::workpieceTemplateFinished);
    }
    m_templateProgressDialog = new QProgressDialog(loading ? tr("读取工件模板…") : tr("保存工件模板…"),
                                                 tr("取消"), 0, 0, this);
    m_templateProgressDialog->setObjectName(QStringLiteral("workpiece_template_progress_v1"));
    m_templateProgressDialog->setWindowTitle(loading ? tr("打开工件模板") : tr("创建工件模板"));
    m_templateProgressDialog->setWindowModality(Qt::WindowModal);
    m_templateProgressDialog->setMinimumDuration(0);
    m_templateProgressDialog->setAutoClose(false);
    m_templateProgressDialog->setAutoReset(false);
    const auto cancel = m_templateCancelToken;
    connect(m_templateProgressDialog, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    if (!m_templateProgressTimer) {
        m_templateProgressTimer = new QTimer(this);
        m_templateProgressTimer->setInterval(100);
        connect(m_templateProgressTimer, &QTimer::timeout, this, [this] {
            if (m_closing || !m_templateTaskActive || !m_templateProgressDialog) return;
            const auto stage = m_templateProgress->stage.load();
            const qsizetype completed = m_templateProgress->completed.load();
            const qsizetype total = m_templateProgress->total.load();
            m_templateProgressDialog->setLabelText(stageText(stage));
            if (total > 0) {
                m_templateProgressDialog->setRange(0, 100);
                m_templateProgressDialog->setValue(int(std::min(100.0, 100.0 * double(completed) / double(total))));
            } else m_templateProgressDialog->setRange(0, 0);
        });
    }
    m_templateProgressTimer->start();
    m_templateProgressDialog->show();
}

void MainWindow::startWorkpieceTemplateSave(pcv::workpiece::SaveWorkpieceTemplateRequest request)
{
    if (!workpieceTemplateReady()) return;
    prepareWorkpieceTemplateTask(false);
    const auto cancel = m_templateCancelToken;
    request.isCancelled = [cancel] { return cancel->load(); };
    request.progress = progressCallback(m_templateProgress);
    // Retain another shared buffer: no worker may detach or mutate Qt containers.
    const auto sharedSnapshot = request.points;
    m_templateWatcher->setFuture(QtConcurrent::run([request = std::move(request), sharedSnapshot] {
        Q_UNUSED(sharedSnapshot);
        return pcv::workpiece::saveWorkpieceTemplate(request);
    }));
}

void MainWindow::startWorkpieceTemplateLoad(const QString &infoFile)
{
    if (m_closing || pointTaskRunning()) return;
    prepareWorkpieceTemplateTask(true);
    const auto cancel = m_templateCancelToken;
    pcv::workpiece::LoadWorkpieceTemplateRequest request;
    request.infoFile = infoFile;
    request.isCancelled = [cancel] { return cancel->load(); };
    request.progress = progressCallback(m_templateProgress);
    m_templateWatcher->setFuture(QtConcurrent::run([request] {
        return pcv::workpiece::loadWorkpieceTemplate(request);
    }));
}

bool MainWindow::publishWorkpieceTemplateResult(const pcv::workpiece::WorkpieceTemplateResult &result)
{
    if (m_closing || !result.success || result.cancelled || result.points.isEmpty()
        || m_templateInputRevision != m_canvasRevision
        || (m_templateCancelToken && m_templateCancelToken->load())) return false;
    CloudSourceContextV1 source;
    source.robotBaseConfirmed = true;
    source.entry = QStringLiteral("workpiece_template");
    source.files = {result.pointCloudFile};
    source.manifest = result.infoFile;
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    if (m_cameraDialog) m_cameraDialog->close();
#endif
    m_rawPoints = result.points;
    m_rawCloudSource = source;
    m_pendingPath = result.pointCloudFile;
    m_sourceFiles = source.files;
    m_sourceDirectory = result.outputDirectory;
    m_folderScanOnly = false;
    m_pendingTextureExperiment = false;
    publishCanvasCache(result.points, false, source);
    invalidateScanTrajectory(tr("已打开工件模板，旧扫描轨迹失效"));
    setWorkflowState(WorkflowState::Idle);
    {
        const QSignalBlocker blocker(m_fileList);
        m_fileList->clear();
        m_fileList->addItem(QFileInfo(result.pointCloudFile).fileName());
        m_fileList->setCurrentRow(0);
    }
    applyDepthRenderRange(m_points);
    m_fileInfo->setText(tr("工件模板：%1\n版本：%2\nrobot_base / mm\n点数：%3")
        .arg(result.info.name, result.info.versionId).arg(QLocale().toString(result.info.pointCount)));
    return true;
}

void MainWindow::workpieceTemplateFinished()
{
    if (m_closing) return;
    const auto result = m_templateWatcher->result();
    m_templateWatcher->deleteLater();
    m_templateWatcher = nullptr;
    m_templateProgressTimer->stop();
    m_templateProgressDialog->hide();
    m_templateProgressDialog->deleteLater();
    m_templateProgressDialog = nullptr;
    m_templateTaskActive = false;
    if (result.cancelled) { statusBar()->showMessage(tr("工件模板任务已取消，当前点云保留")); return; }
    if (!result.success) {
        QMessageBox::warning(this, tr("工件模板任务失败"), result.errorCode + QStringLiteral(": ") + result.message);
        statusBar()->showMessage(tr("工件模板任务失败，当前点云保留"));
        return;
    }
    if (!m_templateLoading) {
        statusBar()->showMessage(tr("工件模板已保存：%1").arg(result.outputDirectory));
        QMessageBox::information(this, tr("工件模板已保存"), result.outputDirectory);
    } else {
        statusBar()->showMessage(publishWorkpieceTemplateResult(result)
            ? tr("工件模板已打开：%1").arg(result.info.name)
            : tr("模板读取完成，但请求已取消或画布已变化，当前点云保留"));
    }
}

void MainWindow::cancelAndWaitWorkpieceTemplate()
{
    if (m_templateProgressTimer) m_templateProgressTimer->stop();
    if (m_templateCancelToken) m_templateCancelToken->store(true);
    if (m_templateWatcher) {
        disconnect(m_templateWatcher, nullptr, this, nullptr);
        if (m_templateWatcher->isRunning()) m_templateWatcher->waitForFinished();
    }
    m_templateTaskActive = false;
}
