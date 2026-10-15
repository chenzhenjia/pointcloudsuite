#include "stitchingwindow.h"
#include <pcv/interface/stitching_interface.h>
#include <pcv/registration/handeye_transform.h>
#include "ui_stitchingwindow.h"
#include <pcv/io/ply_writer.h>

#include <QDateTime>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {


QString registrationPairStatus(const pointcloud::IcpDiagnostics &diagnostic)
{
    if (diagnostic.accepted)
        return diagnostic.steepPlaneDetected
            ? QObject::tr("陡峭平面恢复通过") : QObject::tr("严格通过");
    if (diagnostic.reason == QStringLiteral("plane_tilt_exceeds_limit"))
        return QObject::tr("平面拟合不足：倾角超过 75 deg");
    if (diagnostic.reason.contains(QStringLiteral("steep_plane_feature_insufficient")))
        return QObject::tr("结构特征不足");
    if (diagnostic.overlapCoordinateSystem == QStringLiteral("plane_uvn")
        && (diagnostic.localOverlapSourcePoints < 100
            || diagnostic.localOverlapTargetPoints < 100))
        return QObject::tr("局部重叠不足");
    if (diagnostic.planePrealignmentAttempted && !diagnostic.planePrealignmentAccepted)
        return QObject::tr("坐标链风险或平面预对齐超限");
    return QObject::tr("配准未通过");
}


StitchTaskResult runStitching(const pcv::interface::StitchingRequest &request)
{
    StitchTaskResult result;
    const auto &icp = request.registration.options;
    result.outputDirectory = request.artifacts.outputDirectory;
    result.transformOnly = !icp.enabled;
    const pcv::interface::StitchingResult shared = pcv::interface::stitch(request);
    result.merge = shared.merge;
    result.merge.points = shared.points;
    result.merge.icpDiagnostics = shared.icpDiagnostics;
    result.merge.diagnostics = shared.diagnostics;
    result.seamFusion = shared.seamFusion;
    if (!shared.success) {
        if (shared.cancelled) {
            result.cancelled = true;
            return result;
        }
        result.error = shared.message.isEmpty()
            ? (result.transformOnly ? QObject::tr("点云手眼坐标转换失败")
                                    : QObject::tr("点云世界坐标转换或配准失败"))
            : shared.message;
        return result;
    }
    result.registrationAccepted = true;
    result.multiFrameConsistent = true;
    if (icp.enabled) {
        for (int index = 1; index < result.merge.icpDiagnostics.size(); ++index) {
            const auto&diagnostic=result.merge.icpDiagnostics[index];
            if (!diagnostic.accepted) {
                result.registrationAccepted = false;
            }
        }
    }
    result.multiFrameConsistent = result.registrationAccepted;
    if(icp.enabled){
        for(int index=1;index<result.merge.icpDiagnostics.size();++index)
            result.registrationApplied=result.registrationApplied||result.merge.icpDiagnostics[index].accepted;
    }
    result.mergedFile = shared.manifest.formalPly.isEmpty() ? shared.outputPly : shared.manifest.formalPly;
    result.previewFile = shared.manifest.previewPly;
    result.mappingFile = shared.manifest.mappingFile;
    result.reportFile = shared.manifest.reportJson;
    result.formalOutput = true;
    result.ok = true;
    result.seamFusionApplied = !shared.seamDiagnostics.isEmpty();
    result.merge.diagnostics += QStringLiteral("production_pipeline=pcv::interface::stitch\n");
    // v0.4 production boundary: module 30 has already committed the formal
    // PLY, preview, mapping and manifest through module 10's transaction.
    return result;
}

QString numericText(double value)
{
    return QString::number(value, 'g', 12);
}

QString poseText(const QVector<double> &pose)
{
    QStringList values;
    values.reserve(pose.size());
    for (double value : pose) values.push_back(numericText(value));
    return values.join(QLatin1Char(' '));
}

bool parsePoseText(const QString &text, QVector<double> *pose)
{
    if (!pose) return false;
    pose->clear();
    const QStringList parts = text.trimmed().split(
        QRegularExpression(QStringLiteral("[\\s,，]+")), Qt::SkipEmptyParts);
    if (parts.size() != 6) return false;
    pose->reserve(6);
    for (const QString &part : parts) {
        bool ok = false;
        const double value = part.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            pose->clear();
            return false;
        }
        pose->push_back(value);
    }
    return true;
}

} // namespace

StitchingWindow::StitchingWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::StitchingWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(tr("多帧点云拼接"));
    auto *advanced = new QGroupBox(tr("高级设置"), this);
    advanced->setObjectName(QStringLiteral("gb_advanced"));
    advanced->setCheckable(true);
    advanced->setChecked(false);
    auto *advancedLayout = new QVBoxLayout(advanced);
    auto *advancedContent = new QWidget(advanced);
    advancedContent->setObjectName(QStringLiteral("wgt_advanced_content"));
    auto *advancedGrid = new QGridLayout(advancedContent);
    // Keep stride, preview and seam enable/width in the ordinary controls.
    for (int row = 0; row <= 5; ++row) {
        for (int column = 0; column < 6; ++column) {
            if ((row == 0 && column < 2) || (row == 1 && column >= 4)
                || (row == 3 && column < 2)) continue;
            auto *item = ui->gbl_options->itemAtPosition(row, column);
            if (!item) continue;
            int itemRow, itemColumn, rowSpan, columnSpan;
            ui->gbl_options->getItemPosition(ui->gbl_options->indexOf(item->widget()),
                &itemRow, &itemColumn, &rowSpan, &columnSpan);
            ui->gbl_options->removeItem(item);
            advancedGrid->addWidget(item->widget(), itemRow, itemColumn, rowSpan, columnSpan);
            delete item;
        }
    }
    advancedLayout->addWidget(advancedContent);
    advancedContent->hide();
    connect(advanced, &QGroupBox::toggled, advancedContent, &QWidget::setVisible);
    ui->vbl_main->insertWidget(ui->vbl_main->indexOf(ui->gb_options) + 1, advanced);
    auto *up = new QPushButton(tr("上移"), this);
    up->setObjectName(QStringLiteral("btn_move_up"));
    auto *down = new QPushButton(tr("下移"), this);
    down->setObjectName(QStringLiteral("btn_move_down"));
    ui->hbl_file_actions->insertWidget(2, up);
    ui->hbl_file_actions->insertWidget(3, down);
    connect(up, &QPushButton::clicked, this, [this] { moveSelectedRow(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelectedRow(1); });
    // Single cancellation token for the whole window lifetime: never replaced
    // between runs so no queued worker copy can outlive the token object.
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    ui->tbl_scans->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tbl_scans->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tbl_scans->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->tbl_scans->setEditTriggers(QAbstractItemView::DoubleClicked
        | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);

    connect(ui->btn_browse_calibration, &QPushButton::clicked, this, &StitchingWindow::browseCalibration);
    connect(ui->btn_browse_output, &QPushButton::clicked, this, &StitchingWindow::browseOutputDirectory);
    connect(ui->btn_add_files, &QPushButton::clicked, this, &StitchingWindow::addPlyFiles);
    connect(ui->btn_add_folder, &QPushButton::clicked, this, &StitchingWindow::addPlyFolder);
    connect(ui->btn_remove_rows, &QPushButton::clicked, this, &StitchingWindow::removeSelectedRows);
    connect(ui->btn_clear_files, &QPushButton::clicked, this, &StitchingWindow::clearFiles);
    connect(ui->btn_start, &QPushButton::clicked, this, &StitchingWindow::startStitching);
    connect(ui->btn_cancel, &QPushButton::clicked, this, &StitchingWindow::cancelStitching);
    connect(ui->cb_processing_mode, &QComboBox::currentIndexChanged,
            this, [this](int) { updateProcessingModeUi(); });
    updateProcessingModeUi();
        statusBar()->showMessage(tr("添加至少两个 PLY，并填写对应扫描 Start/End 位姿"));
}

StitchingWindow::~StitchingWindow()
{
    cancelAndWait();
    delete ui;
}

bool StitchingWindow::isBusy() const
{
    return m_watcher != nullptr;
}

void StitchingWindow::cancelAndWait()
{
    m_stopping = true;
    ++m_taskRevision;
    if (m_cancelRequested) m_cancelRequested->store(true);
    if (m_watcher) {
        disconnect(m_watcher, nullptr, this, nullptr);
        m_watcher->waitForFinished();
        delete m_watcher;
        m_watcher = nullptr;
    }
}

void StitchingWindow::closeEvent(QCloseEvent *event)
{
    if (parentWidget()) {
        hide();
        event->ignore();
        return;
    }
    cancelAndWait();
    event->accept();
}

void StitchingWindow::moveSelectedRow(int offset)
{
    if (isBusy()) return;
    const auto rows = ui->tbl_scans->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    const int source = rows.first().row();
    const int target = source + offset;
    if (target < 0 || target >= ui->tbl_scans->rowCount()) return;
    for (int column = 0; column < 3; ++column) {
        auto *first = ui->tbl_scans->takeItem(source, column);
        auto *second = ui->tbl_scans->takeItem(target, column);
        ui->tbl_scans->setItem(source, column, second);
        ui->tbl_scans->setItem(target, column, first);
    }
    ui->tbl_scans->selectRow(target);
}

void StitchingWindow::browseCalibration()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择 Eye-in-Hand 标定 XML"),
        ui->le_calibration_path->text(), tr("XML 文件 (*.xml)"));
    if (!path.isEmpty()) ui->le_calibration_path->setText(QDir::toNativeSeparators(path));
}

void StitchingWindow::browseOutputDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("选择输出目录"),
        ui->le_output_directory->text());
    if (!path.isEmpty()) ui->le_output_directory->setText(QDir::toNativeSeparators(path));
}

void StitchingWindow::addPlyFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("添加 PLY 点云"), {},
                                                            tr("PLY 点云 (*.ply)"));
    QStringList sorted = paths;
    std::sort(sorted.begin(), sorted.end());
    for (const QString &path : sorted) addPlyPath(path);
}

void StitchingWindow::addPlyFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("扫描 PLY 文件夹"));
    if (directory.isEmpty()) return;
    QStringList paths;
    QDirIterator iterator(directory, {QStringLiteral("*.ply"), QStringLiteral("*.PLY")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) paths.push_back(iterator.next());
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    for (const QString &path : paths) addPlyPath(path);
    statusBar()->showMessage(tr("已按文件名顺序加入 %1 个 PLY")
        .arg(ui->tbl_scans->rowCount()));
}

void StitchingWindow::addPlyPath(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int row = 0; row < ui->tbl_scans->rowCount(); ++row)
        if (QFileInfo(ui->tbl_scans->item(row, 0)->text()).absoluteFilePath()
            .compare(absolute, Qt::CaseInsensitive) == 0) return;

    double startX = 0.0, startY = 0.0, startZ = 0.0;
    const QRegularExpression expression(
        QStringLiteral("X(-?[0-9]+(?:\\.[0-9]+)?)Y(-?[0-9]+(?:\\.[0-9]+)?)Z(-?[0-9]+(?:\\.[0-9]+)?)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(QFileInfo(path).completeBaseName());
    if (match.hasMatch()) {
        startX = match.captured(1).toDouble();
        startY = match.captured(2).toDouble();
        startZ = match.captured(3).toDouble();
    }
    const QVector<double> start = {startX, startY, startZ, 0.0, 0.0, 0.0};
    const QVector<double> end = {startX, 0.0, startZ, 0.0, 0.0, 0.0};
    const int row = ui->tbl_scans->rowCount();
    ui->tbl_scans->insertRow(row);
    auto *fileItem = new QTableWidgetItem(QDir::toNativeSeparators(absolute));
    fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
    ui->tbl_scans->setItem(row, 0, fileItem);
    const QVector<QVector<double>> poses = {start, end};
    for (int column = 1; column <= 2; ++column) {
        auto *item = new QTableWidgetItem(poseText(poses[column - 1]));
        item->setToolTip(tr("按 X Y Z A B C 顺序输入 6 个数（A=Rx，B=Ry，C=Rz）；支持空格、Tab 或逗号分隔，可整串复制粘贴"));
        ui->tbl_scans->setItem(row, column, item);
    }
    if (ui->le_output_directory->text().trimmed().isEmpty())
        ui->le_output_directory->setText(QDir(QFileInfo(path).absolutePath())
                                             .filePath(QStringLiteral("stitch_output")));
}

void StitchingWindow::removeSelectedRows()
{
    QSet<int> rowSet;
    for (const QModelIndex &index : ui->tbl_scans->selectionModel()->selectedRows()) rowSet.insert(index.row());
    QList<int> rows = rowSet.values();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) ui->tbl_scans->removeRow(row);
}

void StitchingWindow::clearFiles()
{
    ui->tbl_scans->setRowCount(0);
}

bool StitchingWindow::readCalibration(QMatrix4x4 *transform, QString *error) const
{
    const QString path = ui->le_calibration_path->text().trimmed();
    if (path.isEmpty()) {
        if (error) *error = tr("请选择 Eye-in-Hand XML");
        return false;
    }
    pointcloud::HandEyeCalibration calibration;
    if (!pointcloud::loadHandEyeCalibration(path, &calibration, error)) return false;
    *transform = calibration.flangeFromDepth;
    return true;
}

QVector<pcv::stitching::WorldCloudInput> StitchingWindow::collectInputs(QString *error) const
{
    QVector<pcv::stitching::WorldCloudInput> inputs;
    const bool transformOnly = ui->cb_processing_mode->currentIndex() == 1;
    if ((!transformOnly && ui->tbl_scans->rowCount() < 2)
        || (transformOnly && ui->tbl_scans->rowCount() < 1)) {
        if (error) *error = transformOnly
            ? tr("仅手眼坐标转换模式至少需要一个 PLY 文件")
            : tr("配准与融合模式至少需要两个 PLY 文件");
        return inputs;
    }
    QMatrix4x4 flangeFromDepth;
    if (!readCalibration(&flangeFromDepth, error)) return inputs;

    for (int row = 0; row < ui->tbl_scans->rowCount(); ++row) {
        QVector<double> start, end;
        if (!parsePoseText(ui->tbl_scans->item(row, 1)->text(), &start)) {
            if (error) *error = tr("第 %1 行 Start 必须包含 6 个有效数字：X Y Z A B C").arg(row + 1);
            return {};
        }
        if (!parsePoseText(ui->tbl_scans->item(row, 2)->text(), &end)) {
            if (error) *error = tr("第 %1 行 End 必须包含 6 个有效数字：X Y Z A B C").arg(row + 1);
            return {};
        }
        pcv::stitching::WorldCloudInput input;
        input.filePath = ui->tbl_scans->item(row, 0)->text();
        // Input arrays are controller order [X,Y,Z,A,B,C], A=Rx, B=Ry, C=Rz.
        const pointcloud::RobotPose startPose{start[0], start[1], start[2],
                                               start[3], start[4], start[5]};
        const pointcloud::RobotPose endPose{end[0], end[1], end[2],
                                             end[3], end[4], end[5]};
        input.startBaseFromFlange = pointcloud::robotPoseToMatrix(startPose);
        input.endBaseFromFlange = pointcloud::robotPoseToMatrix(endPose);
        input.flangeFromDepth = flangeFromDepth;
        inputs.push_back(input);
    }
    return inputs;
}

pointcloud::IcpOptions StitchingWindow::collectIcpOptions(QString *error) const
{
    pointcloud::IcpOptions options;
    options.enabled = ui->cb_processing_mode->currentIndex() == 0;
    options.collectDiagnosticFrames = ui->chk_diagnostic_output->isChecked();
    // Keep the high-precision runtime profile explicit here. This also makes
    // Qt Creator builds independent of compiler header-dependency locale.
    options.sampleStride = ui->sb_sample_stride->value();
    options.overlapMargin = float(ui->dsb_overlap_margin->value());
    options.parallelEnabled = ui->chk_parallel_processing->isChecked();
    options.maxParallelTasks = ui->sb_parallel_tasks->value();
    const auto parseLevels = [error](const QString &text, const QString &name) {
        QVector<float> values;
        const QStringList parts = text.split(
            QRegularExpression(QStringLiteral("[\\s,，]+")), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            bool ok = false;
            const float value = part.toFloat(&ok);
            if (!ok || !std::isfinite(value) || value <= 0.0f) {
                if (error) *error = QObject::tr("%1 必须是空格或逗号分隔的正数").arg(name);
                return QVector<float>();
            }
            values.push_back(value);
        }
        return values;
    };
    if (!options.enabled) return options;
    options.voxelLevels = parseLevels(ui->le_voxel_levels->text(), tr("体素层级"));
    if (error && !error->isEmpty()) return options;
    options.correspondenceDistances = parseLevels(
        ui->le_correspondence_distances->text(), tr("对应距离层级"));
    if (error && !error->isEmpty()) return options;
    if (options.voxelLevels.size() != options.correspondenceDistances.size()
        || options.voxelLevels.size() != 3) {
        if (error) *error = tr("参考流程要求三个体素层级和三个对应距离，数量必须一致");
        return options;
    }
    options.maximumCorrectionTranslation = float(ui->dsb_max_translation->value());
    options.maximumCorrectionAngleDegrees = float(ui->dsb_max_rotation->value());
    options.parallelEnabled = ui->chk_parallel_processing->isChecked();
    options.maxParallelTasks = ui->sb_parallel_tasks->value();
    if (ui->chk_diagnostic_output->isChecked()) {
        // Diagnostic mode re-enables the complete coarse search (in-plane
        // tangent coarse alignment and the wide structure diagnostic).
        options.tangentCoarseAlignmentEnabled = true;
        options.tangentCoarseAlignmentAuto = false;
        options.wideStructureDiagnosticEnabled = true;
    }
    return options;
}

pcv::interface::StitchingRequest StitchingWindow::collectRequest(QString *error) const
{
    pcv::interface::StitchingRequest request;
    QString detail;
    const auto inputs = collectInputs(&detail);
    const auto icp = detail.isEmpty() ? collectIcpOptions(&detail) : pointcloud::IcpOptions{};
    if (!detail.isEmpty()) {
        if (error) *error = detail;
        return request;
    }
    request.input.calibrationPath = ui->le_calibration_path->text().trimmed();
    for (const auto &input : inputs) {
        request.input.frames.push_back({input.filePath,
            pointcloud::matrixToRobotPose(input.startBaseFromFlange),
            pointcloud::matrixToRobotPose(input.endBaseFromFlange)});
    }
    request.registration.options = icp;
    request.registration.options.cancellation = m_cancelRequested;
    request.registration.sampleStride = icp.sampleStride;
    request.registration.parallelEnabled = icp.parallelEnabled;
    request.registration.maxParallelTasks = icp.maxParallelTasks;
    request.registration.collectDiagnosticFrames = icp.collectDiagnosticFrames;
    request.seam.enabled = icp.enabled && ui->chk_seam_fusion->isChecked();
    request.seam.halfWidthMm = float(ui->dsb_seam_half_width->value());
    request.seam.mutualDistanceMm = float(ui->dsb_mutual_distance->value());
    request.seam.decisionCellSizeMm = float(ui->dsb_decision_voxel->value());
    request.artifacts.outputDirectory = QDir::fromNativeSeparators(
        ui->le_output_directory->text().trimmed());
    request.artifacts.previewRatio = ui->dsb_preview_ratio->value();
    request.artifacts.writeSourceClouds = ui->chk_diagnostic_output->isChecked();
    request.artifacts.writeDiagnosticFrames = ui->chk_diagnostic_output->isChecked();
    const auto cancel = m_cancelRequested;
    request.isCancelled = [cancel] { return cancel->load(); };
    return request;
}

void StitchingWindow::startStitching()
{
    if (m_watcher || m_stopping) return;
    QString error;
    auto request = collectRequest(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("输入无效"), error);
        return;
    }
    const auto &inputs = request.input.frames;
    const auto &icp = request.registration.options;
    const QString output = QDir::fromNativeSeparators(ui->le_output_directory->text().trimmed());
    if (output.isEmpty()) {
        QMessageBox::warning(this, tr("输出无效"), tr("请选择输出目录"));
        return;
    }
    QStringList outputNames = {QStringLiteral("stitched_robot_base.ply"),
        QStringLiteral("stitched_robot_base.pcvmap"), QStringLiteral("stitching_manifest.json"),
        QStringLiteral("stitched_robot_base_preview.ply")};
    if (ui->chk_diagnostic_output->isChecked()) {
        for (int index = 0; index < inputs.size(); ++index) {
            outputNames << QStringLiteral("%1_%2_robot_base.ply").arg(index + 1)
                .arg(QFileInfo(inputs.at(index).plyPath).completeBaseName());
            outputNames << QStringLiteral("diagnostic_pre_icp_frame_%1.ply")
                .arg(index + 1, 2, 10, QChar('0'));
            outputNames << QStringLiteral("diagnostic_post_icp_frame_%1.ply")
                .arg(index + 1, 2, 10, QChar('0'));
        }
    }
    QStringList existing;
    for (const QString &name : outputNames) {
        const QFileInfo artifact(QDir(output).filePath(name));
        for (const auto &input : inputs) {
            const QFileInfo source(input.plyPath);
            if (source.absoluteFilePath().compare(artifact.absoluteFilePath(), Qt::CaseInsensitive) == 0
                || (artifact.exists() && source.canonicalFilePath().compare(
                        artifact.canonicalFilePath(), Qt::CaseInsensitive) == 0)) {
                QMessageBox::warning(this, tr("输出无效"), tr("输出不能覆盖输入 PLY"));
                return;
            }
        }
        if (artifact.exists()) existing.push_back(name);
    }
    if (!existing.isEmpty() && QMessageBox::question(this, tr("确认覆盖输出"),
        tr("输出目录已有结果：\n%1\n是否覆盖本次待写入文件？未写入的旧文件将保留。")
            .arg(existing.join(QLatin1Char('\n'))), QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No) != QMessageBox::Yes) return;

    if (canStartTask && !canStartTask()) return;
    request.artifacts.allowOverwrite = !existing.isEmpty();
    m_cancelRequested->store(false);
    const quint64 taskRevision = ++m_taskRevision;
    m_watcher = new QFutureWatcher<StitchTaskResult>(this);
    connect(m_watcher, &QFutureWatcher<StitchTaskResult>::finished,
            this, &StitchingWindow::stitchingFinished);
    setBusy(true);
    appendLog(tr("开始：%1 个点云；模式=%2；机器人坐标转换=开启；ICP=%3")
                  .arg(inputs.size())
                  .arg(icp.enabled ? tr("配准与融合") : tr("仅手眼坐标转换"))
                  .arg(icp.enabled ? tr("开启") : tr("关闭")));
    appendLog(tr("并行处理=%1，最大任务数=%2")
                  .arg(icp.parallelEnabled ? tr("开启") : tr("关闭"))
                  .arg(icp.maxParallelTasks == 0 ? tr("自动") : QString::number(icp.maxParallelTasks)));
    request.progress = [this, taskRevision](float fraction, const QString &message) {
        const int value = qBound(0, qRound(fraction * 100.0f), 100);
        QMetaObject::invokeMethod(this, [this, taskRevision, value, message]() {
            if (m_stopping || taskRevision != m_taskRevision || !m_watcher) return;
            ui->pbar_progress->setValue(value);
            ui->pbar_progress->setFormat(QStringLiteral("%1%  %2").arg(value).arg(message));
            statusBar()->showMessage(message);
            appendLog(message);
        }, Qt::QueuedConnection);
    };
    m_watcher->setFuture(QtConcurrent::run([request]() {
        return runStitching(request);
    }));
}

void StitchingWindow::cancelStitching()
{
    if (m_cancelRequested) m_cancelRequested->store(true);
    ui->btn_cancel->setEnabled(false);
    statusBar()->showMessage(tr("已请求取消；将在当前计算/输出阶段结束后停止"));
}

void StitchingWindow::stitchingFinished()
{
    QFutureWatcher<StitchTaskResult> *watcher = m_watcher;
    if (!watcher) return;
    disconnect(watcher, nullptr, this, nullptr);
    const StitchTaskResult result = watcher->result();
    watcher->deleteLater();
    m_watcher = nullptr;
    setBusy(false);
    appendLog(result.merge.diagnostics);
    if (result.cancelled) {
        appendLog(tr("任务已取消；正式输出和当前画布未更新。"));
        statusBar()->showMessage(tr("已取消"));
        return;
    }
    if (!result.ok) {
        appendLog(tr("失败：%1").arg(result.error));
        QMessageBox::critical(this, tr("配准失败"), result.error);
        return;
    }
    appendLog(tr("完成：合并 %1 点；跨点云重复 %2\n输出：%3\n报告：%4\n预览：%5\n来源映射：%6")
                  .arg(result.merge.points.size())
                  .arg(result.merge.crossCloudDuplicateCount)
                  .arg(result.mergedFile, result.reportFile, result.previewFile, result.mappingFile));
    for (int index = 1; index < result.merge.icpDiagnostics.size(); ++index) {
        const pointcloud::IcpDiagnostics &diagnostic = result.merge.icpDiagnostics[index];
        appendLog(tr("scan %1：%2").arg(index + 1).arg(registrationPairStatus(diagnostic)));
    }
    statusBar()->showMessage(result.transformOnly
        ? tr("手眼坐标转换和输出完成") : tr("点云配准和输出完成"));
    StitchingPublication publication;
    publication.points = result.merge.points;
    publication.formalPly = result.mergedFile;
    publication.previewPly = result.previewFile;
    publication.mappingFile = result.mappingFile;
    publication.reportJson = result.reportFile;
    publication.outputDirectory = result.outputDirectory;
    emit stitchingSucceeded(publication);
}

void StitchingWindow::updateProcessingModeUi()
{
    const bool registrationMode = ui->cb_processing_mode->currentIndex() == 0;
    ui->chk_icp->setChecked(registrationMode);
    ui->le_voxel_levels->setEnabled(registrationMode);
    ui->le_correspondence_distances->setEnabled(registrationMode);
    ui->le_iterations->setEnabled(registrationMode);
    ui->dsb_overlap_margin->setEnabled(registrationMode);
    ui->dsb_max_translation->setEnabled(registrationMode);
    ui->dsb_max_rotation->setEnabled(registrationMode);
    ui->chk_seam_fusion->setEnabled(registrationMode);
    ui->dsb_seam_half_width->setEnabled(registrationMode);
    ui->dsb_mutual_distance->setEnabled(registrationMode);
    ui->dsb_decision_voxel->setEnabled(registrationMode);
    ui->chk_diagnostic_output->setEnabled(registrationMode);
    ui->btn_start->setText(registrationMode
        ? tr("开始世界坐标配准") : tr("开始手眼坐标转换"));
    ui->pte_log->setPlaceholderText(registrationMode
        ? tr("转换、重叠检查、ICP、接缝和输出报告将显示在这里。")
        : tr("读取、手眼坐标转换和输出进度将显示在这里。"));
    statusBar()->showMessage(registrationMode
        ? tr("添加至少两个 PLY，并填写对应扫描 Start/End 位姿")
        : tr("添加一个或多个 PLY，转换后不执行 ICP 或接缝融合"));
}

void StitchingWindow::setBusy(bool busy)
{
    ui->gb_paths->setEnabled(!busy);
    ui->tbl_scans->setEnabled(!busy);
    ui->gb_options->setEnabled(!busy);
    findChild<QGroupBox *>(QStringLiteral("gb_advanced"))->setEnabled(!busy);
    findChild<QPushButton *>(QStringLiteral("btn_move_up"))->setEnabled(!busy);
    findChild<QPushButton *>(QStringLiteral("btn_move_down"))->setEnabled(!busy);
    ui->btn_add_files->setEnabled(!busy);
    ui->btn_add_folder->setEnabled(!busy);
    ui->btn_remove_rows->setEnabled(!busy);
    ui->btn_clear_files->setEnabled(!busy);
    ui->btn_start->setEnabled(!busy);
    ui->btn_cancel->setEnabled(busy);
    ui->pbar_progress->setRange(0, 100);
    if (busy) {
        ui->pbar_progress->setValue(0);
        ui->pbar_progress->setFormat(tr("0%  准备开始"));
    }
}

void StitchingWindow::appendLog(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    ui->pte_log->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text.trimmed()));
}

