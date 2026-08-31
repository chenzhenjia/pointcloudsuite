#include "registrationdialog.h"

#include <pcv/interface/stitching_interface.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

bool parsePose(const QString &text, pointcloud::RobotPose *pose, QString *error)
{
    QString normalized = text;
    normalized.replace(QLatin1Char(','), QLatin1Char(' '));
    const QStringList values = normalized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (values.size() != 6) {
        if (error) *error = QStringLiteral("位姿必须为 6 个数：[X Y Z A B C]");
        return false;
    }
    double parsed[6] = {};
    for (int index = 0; index < values.size(); ++index) {
        bool ok = false;
        parsed[index] = values[index].toDouble(&ok);
        if (!ok || !std::isfinite(parsed[index])) {
            if (error) *error = QStringLiteral("位姿包含无效数值：[X Y Z A B C]");
            return false;
        }
    }
    pose->x = parsed[0]; pose->y = parsed[1]; pose->z = parsed[2];
    pose->rx = parsed[3]; pose->ry = parsed[4]; pose->rz = parsed[5];
    return true;
}

QString posePlaceholder()
{
    return QStringLiteral("X Y Z A B C");
}

} // namespace

RegistrationDialog::RegistrationDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("多帧点云配准"));
    setModal(true);
    resize(1080, 700);

    auto *root = new QVBoxLayout(this);
    auto *paths = new QFormLayout;
    m_calibrationPath = new QLineEdit(this);
    auto *browseCalibrationButton = new QPushButton(tr("浏览..."), this);
    auto *calibrationRow = new QHBoxLayout;
    calibrationRow->addWidget(m_calibrationPath);
    calibrationRow->addWidget(browseCalibrationButton);
    paths->addRow(tr("Eye-in-Hand XML"), calibrationRow);
    m_outputDirectory = new QLineEdit(this);
    auto *browseOutputButton = new QPushButton(tr("浏览..."), this);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputDirectory);
    outputRow->addWidget(browseOutputButton);
    paths->addRow(tr("输出目录"), outputRow);
    root->addLayout(paths);

    auto *fileActions = new QHBoxLayout;
    auto *addFilesButton = new QPushButton(tr("添加 PLY"), this);
    auto *addFolderButton = new QPushButton(tr("扫描文件夹"), this);
    auto *upButton = new QPushButton(tr("上移"), this);
    auto *downButton = new QPushButton(tr("下移"), this);
    auto *removeButton = new QPushButton(tr("移除"), this);
    auto *clearButton = new QPushButton(tr("清空"), this);
    for (QPushButton *button : {addFilesButton, addFolderButton, upButton, downButton, removeButton, clearButton})
        fileActions->addWidget(button);
    fileActions->addStretch();
    root->addLayout(fileActions);

    m_frames = new QTableWidget(this);
    m_frames->setColumnCount(3);
    m_frames->setHorizontalHeaderLabels({tr("原始 PLY（LineProfileXz）"),
                                         tr("Start（X Y Z A B C）"),
                                         tr("End（X Y Z A B C）")});
    m_frames->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frames->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frames->setAlternatingRowColors(true);
    m_frames->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_frames->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frames->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    root->addWidget(m_frames, 1);

    auto *options = new QFormLayout;
    m_sampleStride = new QSpinBox(this);
    m_sampleStride->setRange(1, 100);
    m_sampleStride->setValue(8);
    options->addRow(tr("配准抽样步长"), m_sampleStride);
    m_seamEnabled = new QCheckBox(tr("启用渐变接缝融合"), this);
    m_seamEnabled->setChecked(false);
    m_seamEnabled->setEnabled(false);
    options->addRow(QString(), m_seamEnabled);
    m_seamHalfWidth = new QDoubleSpinBox(this);
    m_seamHalfWidth->setRange(0.1, 100.0);
    m_seamHalfWidth->setDecimals(2);
    m_seamHalfWidth->setSuffix(tr(" mm"));
    m_seamHalfWidth->setValue(8.0);
    m_seamHalfWidth->setEnabled(false);
    options->addRow(tr("接缝半宽"), m_seamHalfWidth);
    root->addLayout(options);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("读取、手眼转换、ICP、接缝和输出状态将显示在这里。"));
    root->addWidget(m_log, 1);

    auto *runRow = new QHBoxLayout;
    m_start = new QPushButton(tr("开始配准"), this);
    m_cancel = new QPushButton(tr("取消"), this);
    m_cancel->setEnabled(false);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    runRow->addWidget(m_start);
    runRow->addWidget(m_cancel);
    runRow->addWidget(m_progress, 1);
    root->addLayout(runRow);

    connect(browseCalibrationButton, &QPushButton::clicked, this, [this] { browseCalibration(); });
    connect(browseOutputButton, &QPushButton::clicked, this, [this] { browseOutputDirectory(); });
    connect(addFilesButton, &QPushButton::clicked, this, [this] { addFiles(); });
    connect(addFolderButton, &QPushButton::clicked, this, [this] { addFolder(); });
    connect(upButton, &QPushButton::clicked, this, [this] { moveSelectedRow(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveSelectedRow(1); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelectedRows(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { m_frames->setRowCount(0); });
    connect(m_start, &QPushButton::clicked, this, [this] { startRegistration(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelRegistration(); });
}

RegistrationDialog::~RegistrationDialog()
{
    if (m_watcher) {
        disconnect(m_watcher, nullptr, this, nullptr);
        if (m_watcher->isRunning()) {
            if (m_cancelRequested) m_cancelRequested->store(true);
            m_watcher->waitForFinished();
        }
    }
}

bool RegistrationDialog::hasSuccessfulResult() const { return m_success; }
QString RegistrationDialog::outputPly() const { return m_outputPly; }
QVector<pointcloud::Point3D> RegistrationDialog::takePoints() { return std::move(m_points); }

void RegistrationDialog::addPlyPaths(const QStringList &paths)
{
    QStringList sorted = paths;
    std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });
    for (const QString &path : sorted) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        bool exists = false;
        for (int row = 0; row < m_frames->rowCount(); ++row) {
            if (m_frames->item(row, 0) && QDir::cleanPath(m_frames->item(row, 0)->text()) == QDir::cleanPath(absolute)) {
                exists = true;
                break;
            }
        }
        if (exists) continue;
        const int row = m_frames->rowCount();
        m_frames->insertRow(row);
        auto *fileItem = new QTableWidgetItem(absolute);
        fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
        m_frames->setItem(row, 0, fileItem);
        m_frames->setItem(row, 1, new QTableWidgetItem(posePlaceholder()));
        m_frames->setItem(row, 2, new QTableWidgetItem(posePlaceholder()));
    }
}

void RegistrationDialog::browseCalibration()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择 Eye-in-Hand XML"),
        m_calibrationPath->text(), tr("XML 文件 (*.xml);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_calibrationPath->setText(QFileInfo(path).absoluteFilePath());
}

void RegistrationDialog::browseOutputDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("选择输出目录"), m_outputDirectory->text());
    if (!path.isEmpty()) m_outputDirectory->setText(QFileInfo(path).absoluteFilePath());
}

void RegistrationDialog::addFiles()
{
    addPlyPaths(QFileDialog::getOpenFileNames(this, tr("添加原始 PLY"), {}, tr("PLY 文件 (*.ply)")));
}

void RegistrationDialog::addFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("扫描 PLY 文件夹"));
    if (directory.isEmpty()) return;
    QStringList paths;
    QDirIterator it(directory, {QStringLiteral("*.ply"), QStringLiteral("*.PLY")}, QDir::Files);
    while (it.hasNext()) paths.push_back(it.next());
    addPlyPaths(paths);
}

void RegistrationDialog::moveSelectedRow(int offset)
{
    const auto rows = m_frames->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    const int source = rows.first().row();
    const int target = source + offset;
    if (target < 0 || target >= m_frames->rowCount()) return;
    for (int column = 0; column < m_frames->columnCount(); ++column) {
        QTableWidgetItem *sourceItem = m_frames->takeItem(source, column);
        QTableWidgetItem *targetItem = m_frames->takeItem(target, column);
        m_frames->setItem(source, column, targetItem);
        m_frames->setItem(target, column, sourceItem);
    }
    m_frames->selectRow(target);
}

void RegistrationDialog::removeSelectedRows()
{
    QList<int> rows;
    for (const QModelIndex &index : m_frames->selectionModel()->selectedRows()) rows.push_back(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) m_frames->removeRow(row);
}

void RegistrationDialog::startRegistration()
{
    if (m_watcher && m_watcher->isRunning()) return;
    pcv::interface::StitchingOptions options;
    options.calibrationPath = m_calibrationPath->text().trimmed();
    options.outputDirectory = m_outputDirectory->text().trimmed();
    options.sampleStride = m_sampleStride->value();
    options.seamEnabled = false;
    options.seamHalfWidthMm = float(m_seamHalfWidth->value());
    for (int row = 0; row < m_frames->rowCount(); ++row) {
        if (!m_frames->item(row, 0) || !m_frames->item(row, 1) || !m_frames->item(row, 2)) {
            QMessageBox::warning(this, tr("输入无效"), tr("第 %1 行不完整").arg(row + 1));
            return;
        }
        pcv::interface::StitchingFrameInput frame;
        frame.plyPath = m_frames->item(row, 0)->text();
        QString error;
        if (!parsePose(m_frames->item(row, 1)->text(), &frame.startPose, &error)
            || !parsePose(m_frames->item(row, 2)->text(), &frame.endPose, &error)) {
            QMessageBox::warning(this, tr("位姿无效"), tr("第 %1 行：%2").arg(row + 1).arg(error));
            return;
        }
        options.frames.push_back(frame);
    }
    if (options.frames.size() < 2) {
        QMessageBox::warning(this, tr("输入无效"), tr("配准至少需要两个 PLY 文件"));
        return;
    }

    m_success = false;
    m_points.clear();
    m_outputPly.clear();
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    const auto cancelRequested = m_cancelRequested;
    options.isCancelled = [cancelRequested] { return cancelRequested->load(); };
    const QPointer<RegistrationDialog> dialog(this);
    options.progress = [dialog](float value, const QString &message) {
        if (!dialog) return;
        QMetaObject::invokeMethod(dialog.data(), [dialog, value, message] {
            if (dialog && !dialog->m_closing && dialog->m_watcher
                && dialog->m_watcher->isRunning()) {
                dialog->m_progress->setValue(qRound(value * 100.0f));
                dialog->appendLog(message);
            }
        }, Qt::QueuedConnection);
    };
    if (!m_watcher) {
        m_watcher = new QFutureWatcher<pcv::interface::StitchingResult>(this);
        connect(m_watcher, &QFutureWatcher<pcv::interface::StitchingResult>::finished,
                this, [this] { registrationFinished(); });
    }
    appendLog(tr("开始多帧配准，严格按表格顺序执行。"));
    setBusy(true);
    m_watcher->setFuture(QtConcurrent::run([options] {
        return pcv::interface::stitchRawLineProfiles(options);
    }));
}

void RegistrationDialog::cancelRegistration()
{
    if (m_cancelRequested) m_cancelRequested->store(true);
    m_cancel->setEnabled(false);
    appendLog(tr("已请求取消，正在等待当前步骤结束。"));
}

void RegistrationDialog::registrationFinished()
{
    auto result = m_watcher->future().takeResult();
    setBusy(false);
    if (m_closing) return;
    if (!result.diagnostics.isEmpty()) appendLog(result.diagnostics.trimmed());
    for (const pointcloud::IcpDiagnostics &diagnostic : result.icpDiagnostics)
        appendLog(QStringLiteral("scan %1: %2").arg(diagnostic.sourceCloudIndex + 1).arg(diagnostic.reason));
    for (const pointcloud::SeamFusionDiagnostic &diagnostic : result.seamDiagnostics)
        appendLog(QStringLiteral("seam %1-%2: %3").arg(diagnostic.cloudA + 1).arg(diagnostic.cloudB + 1).arg(diagnostic.reason));
    if (result.cancelled) {
        appendLog(tr("已取消；正式输出未更新。"));
        return;
    }
    if (!result.success) {
        const QString detail = result.errorCode.isEmpty()
            ? result.message : QStringLiteral("%1: %2").arg(result.errorCode, result.message);
        appendLog(tr("失败：%1；正式输出未更新。").arg(detail));
        QMessageBox::warning(this, tr("多帧配准失败"), detail);
        return;
    }
    m_points = std::move(result.points);
    m_outputPly = result.outputPly;
    m_success = true;
    appendLog(tr("完成：%1").arg(m_outputPly));
    QMessageBox::information(this, tr("多帧配准完成"), tr("已写入正式 PLY：\n%1").arg(m_outputPly));
    accept();
}

void RegistrationDialog::setBusy(bool busy)
{
    m_start->setEnabled(!busy);
    m_cancel->setEnabled(busy);
    m_calibrationPath->setEnabled(!busy);
    m_outputDirectory->setEnabled(!busy);
    m_frames->setEnabled(!busy);
    m_sampleStride->setEnabled(!busy);
    m_seamEnabled->setEnabled(false);
    m_seamHalfWidth->setEnabled(false);
    if (busy) m_progress->setValue(0);
}

void RegistrationDialog::appendLog(const QString &message)
{
    if (!message.trimmed().isEmpty()) m_log->appendPlainText(message.trimmed());
}

void RegistrationDialog::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    if (m_watcher && m_watcher->isRunning()) {
        if (m_cancelRequested) m_cancelRequested->store(true);
        disconnect(m_watcher, nullptr, this, nullptr);
        m_watcher->waitForFinished();
    }
    event->accept();
}
