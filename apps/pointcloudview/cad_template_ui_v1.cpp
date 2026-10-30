#include "mainwindow.h"
#include "workbench_shell_v1.h"
#include "features/cad_import_dialog_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>
#include <QSignalBlocker>
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
#include "cameradialog.h"
#endif
bool MainWindow::cadSourceActive() const
{
    return m_currentCloudSource.coordinateFrame == CloudCoordinateFrameV1::CadModel;
}
void MainWindow::openStepTemplate()
{
#ifdef PCV_BUILD_STEP_IMPORT
    if(m_closing || workflowBlocksNewSource() || m_processJob) return;
    const QString path=QFileDialog::getOpenFileName(this,tr("打开 STEP 工件"),{},tr("STEP (*.step *.stp *.STEP *.STP)"));
    if(path.isEmpty() || m_closing || workflowBlocksNewSource() || !confirmReplaceTempWorkpieceSession()) return;
    const quint64 revision=m_canvasRevision;
    if (m_cadImportDialog) { m_cadImportDialog->cancelAndWait(); m_cadImportDialog->deleteLater(); }
    auto *dialog = new CadImportDialogV1(path, this);
    m_cadImportDialog = dialog; m_cadDialogActive = true;
    connect(dialog, &QDialog::accepted, this, [this, dialog, revision] {
        m_cadDialogActive = false;
        if (publishCadCloud(dialog->result(), revision) && m_workbench) m_workbench->showPage("main");
    });
    connect(dialog, &QDialog::rejected, this, [this] {
        m_cadDialogActive = false;
        if (m_workbench && !m_closing) m_workbench->showPage("main");
    });
    m_workbench->embedPage("cad", dialog); m_workbench->showPage("cad");
#endif
}
bool MainWindow::publishCadCloud(const pcv::workpiece::CadCloudResult &result,quint64 inputRevision)
{
    if(m_closing || !result.success || result.cancelled || result.points.isEmpty()
        || inputRevision!=m_canvasRevision || m_processJob) return false;
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    if(m_cameraDialog) m_cameraDialog->close();
#endif
    CloudSourceContextV1 source;
    source.coordinateFrame=CloudCoordinateFrameV1::CadModel; source.entry=QStringLiteral("step_cad");
    for(const auto &file:result.source.value("files").toArray()) source.files.append(file.toString());
    source.cad=result.source.value("cad").toObject();
    source.processingSteps=result.source.value("processing_steps").toArray();
    m_rawPoints=result.points; m_rawCloudSource=source;
    m_pendingPath.clear(); m_sourceFiles.clear(); m_sourceDirectory.clear();
    m_folderScanOnly=false; m_pendingTextureExperiment=false;
    publishCanvasCache(result.points,false,source);
    invalidateScanTrajectory(tr("CAD 点云已发布，旧扫描轨迹失效"));
    setWorkflowState(WorkflowState::Idle);
    { const QSignalBlocker blocker(m_fileList); m_fileList->clear(); }
    applyDepthRenderRange(m_points);
    m_fileInfo->setText(tr("STEP 生成点云\ncad_model / mm\n点数：%1\n来源：%2")
        .arg(m_points.size()).arg(source.files.value(0)));
    statusBar()->showMessage(tr("CAD 点云已发布，可通过“创建工件模板”保存；尚未写入文件"));
    return true;
}
void MainWindow::cancelAndWaitCadImport()
{
    if(m_cadImportDialog) m_cadImportDialog->reject();
}
