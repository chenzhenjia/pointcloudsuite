#include <task_runtime.h>
#include "camera_capture_wizard.h"
#include <pcv/registration/handeye_transform.h>
#include <pcv/texture/texture_mapping.h>
#include <pcv/io/artifact_transaction.h>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

namespace {
bool parsePose(const QString &text, pointcloud::RobotPose *pose) {
    return pcv::ui::parseRobotPose(text, pose, QRegularExpression(QStringLiteral("[\\s,，;]+")));
}

bool attachCaptureTexture(pcv::interface::StitchingResult *result,
                          const QVector<QString> &frameDirectories,
                          const std::shared_ptr<std::atomic_bool> &token,
                          QString *error) {
    if (!result || !result->success) return true;
    if (frameDirectories.isEmpty() || result->merge.points.isEmpty()) {
        if (error) *error = QStringLiteral("纹理映射缺少拼接点云或采集帧");
        return false;
    }
    pcv::texture::MultiFrameBrightnessInput input;
    input.points = result->merge.points;
    input.cloudIds = result->merge.cloudIds;
    input.sourceIndices = result->merge.sourceIndices;
    input.isCancelled = [token] { return token && token->load(); };
    for (const QString &directory : frameDirectories) {
        const auto frame = pcv::camera::readCapturedFrame(directory, {}, pcv::camera::CaptureFrameReadMode::PreviewOnly);
        if (!frame.ok || frame.intensityMono8.isEmpty()) {
            if (error) *error = frame.error.isEmpty()
                ? QStringLiteral("采集帧缺少有效亮度图：%1").arg(directory) : frame.error;
            return false;
        }
        input.frameMono8.push_back(frame.intensityMono8);
        input.frameWidths.push_back(int(frame.intensityWidth));
        input.frameHeights.push_back(int(frame.intensityHeight));
    }
    const auto mapped = pcv::texture::mapMultiFrameBrightness(input);
    if (!mapped.ok || mapped.grayscale.size() != result->points.size()
        || mapped.valid.size() != result->points.size()
        || mapped.mappedPointCount != result->points.size() || mapped.skippedPointCount != 0) {
        if (error) *error = mapped.error.isEmpty() ? QStringLiteral("亮度图映射失败") : mapped.error;
        return false;
    }
    const QString outputDirectory = QFileInfo(result->outputPly).absolutePath();
    pcv::io::ArtifactTransaction transaction(outputDirectory, true);
    QJsonObject manifest{
        {"schema", "pcv-capture-texture-v1"},
        {"mapping_mode", "capture_frame_source_index"},
        {"overlap_policy", "later_frame_wins"},
        {"point_count", qint64(result->points.size())},
        {"mapped_point_count", mapped.mappedPointCount},
        {"skipped_point_count", mapped.skippedPointCount},
        {"frame_count", frameDirectories.size()},
        {"intensity_file", "stitched_robot_base.intensity.bin"},
        {"valid_file", "stitched_robot_base.intensity_valid.bin"}
    };
    if (!transaction.stageBytes(QStringLiteral("stitched_robot_base.intensity.bin"),
                                QByteArray(reinterpret_cast<const char *>(mapped.grayscale.constData()),
                                           mapped.grayscale.size()))
        || !transaction.stageBytes(QStringLiteral("stitched_robot_base.intensity_valid.bin"),
                                   QByteArray(reinterpret_cast<const char *>(mapped.valid.constData()),
                                              mapped.valid.size()))
        || !transaction.stageJson(QStringLiteral("texture_manifest.json"),
                                  QJsonDocument(manifest).toJson(QJsonDocument::Indented))
        || !transaction.commit()) {
        if (error) *error = transaction.error();
        return false;
    }
    result->textureGrayscale = mapped.grayscale;
    result->textureValid = mapped.valid;
    result->textureMappedPointCount = mapped.mappedPointCount;
    result->textureSkippedPointCount = mapped.skippedPointCount;
    result->textureManifest = QDir(outputDirectory).filePath(QStringLiteral("texture_manifest.json"));
    result->textureSourceDirectory = QFileInfo(frameDirectories.first()).absolutePath();
    return true;
}
}
CameraCaptureWizard::CameraCaptureWizard(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *group = new QGroupBox(tr("连续采集向导（不执行 ICP）"), this);
    layout->addWidget(group);
    auto *body = new QVBoxLayout(group);
    m_stepTitle = new QLabel(tr("步骤 1/4：连接相机"));
    m_stepTitle->setStyleSheet(QStringLiteral("font-size:14pt;font-weight:600;color:#1769aa;"));
    body->addWidget(m_stepTitle);
    m_connectionStatus = new QLabel(tr("请先在上方设备区连接相机；单台设备会自动连接。"));
    m_connectionStatus->setWordWrap(true); body->addWidget(m_connectionStatus);
    m_steps = new QStackedWidget;
    body->addWidget(m_steps, 1);
    auto *connectionPage = new QWidget;
    auto *connectionLayout = new QVBoxLayout(connectionPage);
    auto *connectionHint = new QLabel(tr("在上方选择并连接相机，然后进入采集设置。\n已有会话无需连接相机，可直接打开继续填写位姿。"));
    connectionHint->setWordWrap(true);
    connectionLayout->addWidget(connectionHint);
    m_open = new QPushButton(tr("打开已有采集会话…"));
    connectionLayout->addWidget(m_open);
    connectionLayout->addStretch();
    m_steps->addWidget(connectionPage);

    m_directory = new QLineEdit;
    m_directory->setPlaceholderText(tr("选择源码树外的采集保存目录"));
    m_browseDirectory = new QPushButton(tr("选择保存目录…"));
    m_rows = new QSpinBox; m_rows->setRange(2, 1000000); m_rows->setValue(3000);
    m_boundaryMode = new QComboBox;
    m_boundaryMode->addItems({tr("SDK完整图像（自动行数，推荐）"), tr("固定行数（兼容）")});
    m_actualRows = new QLabel(tr("实际接收行数：—"));
    m_fixedRowsLabel = new QLabel(tr("固定扫描行数"));
    m_sdkVerified = new QCheckBox(tr("当前设备/触发配置的一次门控扫描对应一幅完整图像"));
    m_gateInactive = new QCheckBox(tr("当前门控无效，尚未开始下一次扫描"));
    m_preflight = new QCheckBox(tr("我已完成现场预检：门控已结束，且当前配置一次扫描对应一幅完整SDK图像"));
    m_intensityEnabled = new QCheckBox(tr("同步采集2D亮度图（Mono8，自动保存 PNG + 原始数据）"));
    m_intensityEnabled->setChecked(true);
    m_externalLight = new QCheckBox(tr("本次使用外置光源"));
    m_externalLightConfirmed = new QCheckBox(tr("外置光源已开启且稳定"));
    m_preflight->setStyleSheet(QStringLiteral("font-weight:600;color:#1769aa;"));

    auto *step1 = new QWidget;
    auto *step1Layout = new QVBoxLayout(step1);
    auto *settingsHint = new QLabel(tr("选择保存目录并完成现场预检。默认接收SDK完整图像，自动读取每次扫描的实际行数。"));
    settingsHint->setWordWrap(true); step1Layout->addWidget(settingsHint);
    auto *storage = new QHBoxLayout;
    storage->addWidget(new QLabel(tr("保存目录"))); storage->addWidget(m_directory, 1); storage->addWidget(m_browseDirectory);
    step1Layout->addLayout(storage);
    m_advanced = new QPushButton(tr("高级设置 ▸")); step1Layout->addWidget(m_advanced);
    m_captureAdvanced = new QWidget;
    auto *mode = new QHBoxLayout(m_captureAdvanced);
    mode->addWidget(new QLabel(tr("扫描边界"))); mode->addWidget(m_boundaryMode);
    mode->addWidget(m_fixedRowsLabel); mode->addWidget(m_rows); mode->addStretch();
    step1Layout->addWidget(m_captureAdvanced); m_captureAdvanced->hide();
    m_rows->hide(); m_fixedRowsLabel->hide();
    connect(m_advanced, &QPushButton::clicked, this, [this] {
        const bool show = m_captureAdvanced->isHidden();
        m_captureAdvanced->setVisible(show);
        m_advanced->setText(show ? tr("高级设置 ▾") : tr("高级设置 ▸"));
    });
    step1Layout->addWidget(m_preflight);
    step1Layout->addWidget(m_intensityEnabled);
    auto *intensityHint = new QLabel(tr("亮度图按帧独立保存和预览，不拼接2D图像；默认仅发布几何点云。"));
    intensityHint->setWordWrap(true); step1Layout->addWidget(intensityHint);
    step1Layout->addWidget(m_externalLight);
    step1Layout->addWidget(m_externalLightConfirmed);
    m_externalLightConfirmed->setVisible(false);
    connect(m_externalLight, &QCheckBox::toggled, this, [this](bool enabled) {
        m_externalLightConfirmed->setVisible(enabled);
        if (!enabled) m_externalLightConfirmed->setChecked(false);
        updateActions();
    });
    auto *preflightHint = new QLabel(tr("开始前请在3DMVS核对：一次门控扫描显示一幅完整图像；当前门控无效，下一次扫描尚未开始。"));
    preflightHint->setWordWrap(true); step1Layout->addWidget(preflightHint);
    // Keep the original controls as compatibility/test hooks while using the
    // single visible preflight check in the first-use flow.
    m_sdkVerified->setVisible(false); m_gateInactive->setVisible(false);
    step1Layout->addWidget(m_sdkVerified); step1Layout->addWidget(m_gateInactive);
    m_steps->addWidget(step1);

    auto *step2 = new QWidget;
    auto *step2Layout = new QVBoxLayout(step2);
    step2Layout->addWidget(new QLabel(tr("步骤 3 开始后，软件会持续接收完整扫描并后台保存。停止前请先结束门控。")));
    auto *capture = new QHBoxLayout;
    m_start = new QPushButton(tr("开始采集")); m_stop = new QPushButton(tr("停止并等待保存"));
    capture->addWidget(m_start); capture->addWidget(m_stop); capture->addStretch();
    step2Layout->addLayout(capture);
    step2Layout->addWidget(m_actualRows);
    m_intensityPreview = new QLabel(tr("最新亮度图：—"));
    m_intensityPreview->setMinimumHeight(100);
    m_intensityPreview->setAlignment(Qt::AlignCenter);
    m_intensityPreview->setStyleSheet(QStringLiteral("background:#20242a;color:#d8dee9;"));
    step2Layout->addWidget(m_intensityPreview);
    m_status = new QLabel(tr("请连接相机，或打开已有采集会话。")); m_status->setWordWrap(true);
    auto *limits = new QLabel(tr("队列最多2帧/512 MiB。异常只停止软件接收，已保存帧会保留；机器人不会由本程序自动停止。"));
    limits->setWordWrap(true); step2Layout->addWidget(limits);
    step2Layout->addStretch(); m_steps->addWidget(step2);

    auto *step3 = new QWidget;
    auto *step3Layout = new QVBoxLayout(step3);
    auto *xml = new QHBoxLayout;
    m_xml = new QLineEdit; m_xml->setPlaceholderText(tr("选择共用手眼 XML"));
    m_browseXml = new QPushButton(tr("选择 XML…")); xml->addWidget(new QLabel(tr("手眼标定"))); xml->addWidget(m_xml, 1); xml->addWidget(m_browseXml);
    step3Layout->addLayout(xml);
    m_table = new QTableWidget(0, 9);
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels({tr("参与"), tr("帧ID"), tr("设备时间戳"), tr("实际宽×高"), tr("有效点数"), tr("Start [X Y Z A B C]"), tr("End [X Y Z A B C]"), tr("位姿/几何状态"), tr("SDK序号"), tr("亮度图状态")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(9, QHeaderView::ResizeToContents);
    m_table->setMinimumHeight(170); step3Layout->addWidget(m_table, 1);
    m_intensityReview = new QLabel(tr("选中帧亮度图：—"));
    m_intensityReview->setMinimumHeight(90);
    m_intensityReview->setAlignment(Qt::AlignCenter);
    m_intensityReview->setStyleSheet(QStringLiteral("background:#20242a;color:#d8dee9;"));
    step3Layout->addWidget(m_intensityReview);
    auto *tableActions = new QHBoxLayout;
    m_copyPrevious = new QPushButton(tr("复制上一行")); m_copySelected = new QPushButton(tr("复制到选中行")); m_validate = new QPushButton(tr("一键校验"));
    m_up = new QPushButton(tr("上移")); m_down = new QPushButton(tr("下移")); m_preview = new QPushButton(tr("预览当前帧"));
    for (auto *button : {m_copyPrevious, m_copySelected, m_validate, m_up, m_down, m_preview}) tableActions->addWidget(button);
    step3Layout->addLayout(tableActions);
    m_validationStatus = new QLabel(tr("请填写并校验参与拼接帧的 Start/End 位姿。")); m_validationStatus->setWordWrap(true); step3Layout->addWidget(m_validationStatus);
    m_textureEnabled = new QCheckBox(tr("生成纹理点云（可选，需要所有参与帧的亮度图有效）"));
    m_textureEnabled->setChecked(false);
    step3Layout->addWidget(m_textureEnabled);
    auto *stitchActions = new QHBoxLayout;
    m_stitch = new QPushButton(tr("拼接并自动发布到工作台")); m_cancel = new QPushButton(tr("取消拼接")); stitchActions->addWidget(m_stitch); stitchActions->addWidget(m_cancel); stitchActions->addStretch();
    step3Layout->addLayout(stitchActions); m_steps->addWidget(step3);

    auto *poseHint = new QLabel(tr("每组位姿填写六个数：X Y Z A B C（mm / °）。首行对应 Start，末行对应 End；扫描行沿行程应近似均匀。直拼不修正机器人位姿误差。"));
    poseHint->setWordWrap(true); step3Layout->addWidget(poseHint);

    auto *navigation = new QHBoxLayout;
    m_back = new QPushButton(tr("上一步")); m_next = new QPushButton(tr("下一步"));
    navigation->addWidget(m_back); navigation->addStretch(); navigation->addWidget(m_next); body->addLayout(navigation);
    body->addWidget(m_status);
    m_timer = new QTimer(this); m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] { pollCapture(); });
    m_watcher = new QFutureWatcher<pcv::interface::StitchingResult>(this);
    m_drainWatcher = new QFutureWatcher<void>(this);
    connect(m_drainWatcher, &QFutureWatcher<void>::finished, this, [this] { finishCapture(); });
    connect(m_watcher, &QFutureWatcher<pcv::interface::StitchingResult>::finished, this, [this] {
        m_publishing = false;
        const auto result = m_watcher->result();
        if (!m_closing && m_runningRevision == m_revision) {
            m_status->setText(result.success ? tr("位姿直拼文件已保存：%1；亮度图目录：%2；未执行几何质量核验")
                .arg(QFileInfo(result.outputPly).absolutePath(), result.textureSourceDirectory.isEmpty()
                    ? tr("本次未启用亮度采集") : result.textureSourceDirectory)
                : (result.cancelled ? tr("拼接已取消，未发布部分结果") : result.message));
            setState(result.success ? State::Completed : (result.cancelled ? State::Reviewing : State::Error));
            if (!result.success && !result.cancelled) m_firstError = result.message;
            updateActions();
            if (result.success && publishResult) publishResult(result);
        } else if (!m_closing) {
            setState(State::Reviewing);
            m_status->setText(tr("输入已变化，旧拼接结果未发布；请核对后重试。"));
        }
        updateActions();
    });
    connect(m_start, &QPushButton::clicked, this, [this] { startCapture(); });
    connect(m_stop, &QPushButton::clicked, this, [this] { stopCapture(); });
    connect(m_back, &QPushButton::clicked, this, [this] { backStep(); });
    connect(m_next, &QPushButton::clicked, this, [this] { advanceStep(); });
    connect(m_copyPrevious, &QPushButton::clicked, this, [this] { copyPoseRow(false); });
    connect(m_copySelected, &QPushButton::clicked, this, [this] { copyPoseRow(true); });
    connect(m_validate, &QPushButton::clicked, this, [this] { validatePoses(true); });
    connect(m_open, &QPushButton::clicked, this, [this] { loadSession(); });
    connect(m_stitch, &QPushButton::clicked, this, [this] { stitch(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_token) m_token->store(true); });
    connect(m_up, &QPushButton::clicked, this, [this] { moveRow(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveRow(1); });
    connect(m_browseDirectory, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("采集保存目录"), m_directory->text());
        if (!path.isEmpty()) m_directory->setText(path);
    });
    connect(m_browseXml, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("手眼 XML"), {}, "XML (*.xml)");
        if (!path.isEmpty()) m_xml->setText(path);
    });
    connect(m_preflight, &QCheckBox::toggled, this, [this](bool checked) {
        m_sdkVerified->setChecked(checked); m_gateInactive->setChecked(checked);
        updateActions();
    });
    connect(m_boundaryMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool sdk = m_boundaryMode->currentIndex() == 0;
        m_rows->setVisible(!sdk); m_fixedRowsLabel->setVisible(!sdk);
        m_actualRows->setVisible(sdk);
        m_intensityEnabled->setEnabled(sdk);
        if (!sdk) m_intensityEnabled->setChecked(false);
        m_preflight->setText(sdk
            ? tr("我已完成现场预检：门控已结束，且当前配置一次扫描对应一幅完整SDK图像")
            : tr("我已完成现场预检：门控已结束，且固定扫描行数配置正确"));
        m_preflight->setChecked(false); m_sdkVerified->setChecked(false); m_gateInactive->setChecked(false);
        updateActions();
    });
    connect(m_preview, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow(); if (row < 0 || isBusy()) return;
        const auto frame = pcv::camera::readCapturedFrame(m_frames.at(row).directory, {},
            pcv::camera::CaptureFrameReadMode::GeometryPreview);
        if (!frame.ok) m_status->setText(frame.error);
        else {
            showSelectedIntensity();
            if (frame.rawFrame && previewDepthFrame) {
                const auto withImage = pcv::camera::readCapturedFrame(m_frames.at(row).directory, {}, pcv::camera::CaptureFrameReadMode::PreviewOnly);
                previewDepthFrame(withImage.ok ? withImage.rawFrame : frame.rawFrame, m_frames.at(row).directory, true);
            }
            else if (previewFrame) { previewFrame(frame.points); m_status->setText(tr("历史帧没有原始深度，使用原有点云预览")); }
        }
    });
    const auto changed = [this] {
        ++m_revision;
        validatePoses(false);
        if (!isBusy() && !m_session.isEmpty()) {
            QString error; if (!saveParameters(&error)) m_status->setText(error);
        }
        updateActions();
    };
    connect(m_xml, &QLineEdit::textChanged, this, [this, changed] {
        m_validatedXml.clear();
        changed();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [changed](QTableWidgetItem *) { changed(); });
    connect(m_textureEnabled, &QCheckBox::toggled, this, [changed] { changed(); });
    connect(m_table, &QTableWidget::currentCellChanged, this, [this] { showSelectedIntensity(); });
    connect(m_directory, &QLineEdit::textChanged, this, [this] {
        m_preflight->setChecked(false);
        updateActions();
    });
    goToStep(0);
    updateActions();
}
CameraCaptureWizard::~CameraCaptureWizard() { cancelAndWait(); }
bool CameraCaptureWizard::isBusy() const { return isCapturing() || m_publishing || m_watcher->isRunning(); }
void CameraCaptureWizard::setState(State state)
{
    m_state = state;
}
void CameraCaptureWizard::setConnecting()
{
    setState(State::Connecting);
    m_connectionReady = false;
    updateActions();
}
void CameraCaptureWizard::setConnectionState(bool ready, const QString &description)
{
    const bool changed = m_connectionReady != ready;
    m_connectionReady = ready;
    m_connectionStatus->setText(description);
    if (changed) m_preflight->setChecked(false);
    if (!isBusy() && m_step == 0) setState(ready ? State::Connected : State::Disconnected);
    updateActions();
}
bool CameraCaptureWizard::captureSettingsReady() const
{
    const QFileInfo directory(m_directory->text().trimmed());
    const QString resolved = directory.canonicalFilePath();
    const QString source = QDir(QString::fromUtf8(PCV_CAPTURE_UI_SOURCE_ROOT)).canonicalPath();
    const bool outside = !resolved.isEmpty() && resolved.compare(source, Qt::CaseInsensitive) != 0
        && !resolved.startsWith(source + "/", Qt::CaseInsensitive);
    return m_connectionReady && directory.isDir() && directory.isWritable() && outside
        && m_gateInactive->isChecked()
        && (m_boundaryMode->currentIndex() != 0 || m_sdkVerified->isChecked())
        && (!m_externalLight->isChecked() || m_externalLightConfirmed->isChecked());
}
void CameraCaptureWizard::goToStep(int step)
{
    m_step = qBound(0, step, 3);
    m_steps->setCurrentIndex(m_step);
    if (!isBusy()) {
        if (m_step == 0) setState(m_connectionReady ? State::Connected : State::Disconnected);
        else if (m_step == 1) setState(captureSettingsReady() ? State::ReadyToCapture : State::Configuring);
        else if (m_step == 2) setState(State::ReadyToCapture);
        else setState(State::Reviewing);
    }
    updateStepUi();
    updateActions();
}
void CameraCaptureWizard::advanceStep()
{
    if (isBusy()) return;
    if (m_step == 0 && m_connectionReady) goToStep(1);
    else if (m_step == 1 && captureSettingsReady()) {
        m_status->setText(tr("配置已就绪。点击开始采集，再启动现场扫描。"));
        goToStep(2);
    } else if (m_step == 2 && !m_frames.isEmpty()) goToStep(3);
    else if (m_step == 3) goToStep(1);
}
void CameraCaptureWizard::backStep()
{
    if (!isBusy() && m_step > 0) goToStep(m_step - 1);
}
void CameraCaptureWizard::updateStepUi()
{
    const QStringList titles{tr("步骤 1/4：连接相机"), tr("步骤 2/4：采集设置与预检"),
        tr("步骤 3/4：采集并等待保存"), tr("步骤 4/4：确认位姿并发布")};
    m_stepTitle->setText(titles.at(m_step));
    m_next->setText(m_step == 0 ? tr("进入采集设置") : m_step == 1 ? tr("进入采集")
        : m_step == 2 ? tr("确认位姿") : tr("新一轮采集"));
}
bool CameraCaptureWizard::validatePoses(bool locateError)
{
    const QSignalBlocker blocker(m_table);
    int included = 0, invalid = 0, textureInvalid = 0, imageWarnings = 0, firstInvalid = -1;
    int firstColumn = 5;
    for (int row = 0; row < m_frames.size(); ++row) {
        auto *include = m_table->item(row, 0);
        auto *start = m_table->item(row, 5);
        auto *end = m_table->item(row, 6);
        auto *status = m_table->item(row, 7);
        if (!include || !start || !end || !status) continue;
        QString text;
        if (include->checkState() != Qt::Checked) text = tr("已排除");
        else {
            ++included;
            const QString directory = m_frames.at(row).directory;
            if (locateError) {
                const auto geometry = pcv::camera::readCapturedFrame(directory, {},
                    pcv::camera::CaptureFrameReadMode::GeometryPreview);
                m_geometryErrors.insert(directory, geometry.ok ? QString() : geometry.error);
                const auto image = pcv::camera::readCapturedFrame(directory, {}, pcv::camera::CaptureFrameReadMode::PreviewOnly);
                m_frames[row].intensityAvailable = image.ok && !image.intensityMono8.isEmpty();
                m_table->item(row, 9)->setText(m_frames.at(row).intensityAvailable
                    ? tr("有效 %1×%2\n%3").arg(m_frames.at(row).width).arg(m_frames.at(row).height)
                        .arg(QDir(directory).filePath("intensity.png"))
                    : tr("缺失/损坏/未采集\n%1").arg(image.error));
            }
            pointcloud::RobotPose startPose, endPose;
            const bool poseValid = parsePose(start->text(), &startPose) && parsePose(end->text(), &endPose);
            const bool geometryValid = m_geometryErrors.value(directory).isEmpty();
            const bool imageValid = !m_textureEnabled->isChecked() || m_frames.at(row).intensityAvailable;
            const bool valid = poseValid && geometryValid && imageValid;
            if (!m_frames.at(row).intensityAvailable) ++imageWarnings;
            if (!poseValid)
                text = (start->text().trimmed().isEmpty() || end->text().trimmed().isEmpty())
                    ? tr("未填写") : tr("格式错误");
            else
                text = tr("有效");
            if (!geometryValid) text = tr("几何无效：%1").arg(m_geometryErrors.value(directory));
            if (!imageValid) ++textureInvalid;
            if (!valid) {
                ++invalid;
                if (firstInvalid < 0) {
                    firstInvalid = row;
                    firstColumn = !geometryValid ? 7 : !poseValid ? 5 : 9;
                }
            }
        }
        status->setText(text);
    }
    if (locateError || m_validatedXml != m_xml->text() || m_validatedXml.isEmpty()) {
        pointcloud::HandEyeCalibration calibration;
        m_xmlError.clear();
        if (m_xml->text().trimmed().isEmpty()) m_xmlError = tr("尚未选择手眼XML");
        else if (pointcloud::loadHandEyeCalibration(m_xml->text(), &calibration, &m_xmlError))
            m_validatedXml = m_xml->text();
        else m_validatedXml.clear();
    }
    const bool xmlReady = !m_validatedXml.isEmpty() && QFileInfo::exists(m_validatedXml);
    const bool ok = included >= 2 && invalid == 0 && xmlReady;
    const QString validationMessage = ok
        ? tr("校验通过：%1 帧位姿有效，可拼接并发布%2。复制的位姿请与真实机器人记录核对。")
            .arg(included).arg(m_textureEnabled->isChecked() ? tr("纹理") : tr("几何点云"))
        : tr("参与 %1 帧；无效 %2 帧（纹理亮度图 %3 帧）。%4")
            .arg(included).arg(invalid).arg(textureInvalid)
            .arg(!xmlReady ? tr("请选择有效的手眼XML：%1").arg(m_xmlError)
                : included < 2 ? tr("至少勾选两帧。") : tr("请修复位姿、几何数据或纹理所需的亮度图。"));
    m_validationStatus->setText(validationMessage + (!m_textureEnabled->isChecked() && imageWarnings
        ? tr("\n%1 帧亮度图缺失、损坏或未采集，不阻断几何拼接。").arg(imageWarnings) : QString()));
    if (locateError && firstInvalid >= 0) {
        m_table->setCurrentCell(firstInvalid, firstColumn);
        m_table->scrollToItem(m_table->item(firstInvalid, firstColumn));
    }
    if (locateError) m_stitch->setEnabled(!isBusy() && ok);
    return ok;
}
void CameraCaptureWizard::showSelectedIntensity()
{
    const int row = m_table ? m_table->currentRow() : -1;
    if (row < 0 || row >= m_frames.size()) {
        m_intensityReview->setPixmap(QPixmap());
        m_intensityReview->setText(tr("选中帧亮度图：—"));
        m_intensityReview->setToolTip(QString());
        return;
    }
    const auto frame = pcv::camera::readCapturedFrame(m_frames.at(row).directory, {}, pcv::camera::CaptureFrameReadMode::PreviewOnly);
    if (!frame.ok || frame.intensityMono8.isEmpty()) {
        m_intensityReview->setPixmap(QPixmap());
        m_intensityReview->setText(tr("选中帧亮度图：缺失/不可用"));
        m_intensityReview->setToolTip(frame.error);
        return;
    }
    const QImage image(reinterpret_cast<const uchar *>(frame.intensityMono8.constData()),
                       int(frame.intensityWidth), int(frame.intensityHeight),
                       int(frame.intensityWidth), QImage::Format_Grayscale8);
    m_intensityReview->setText(QString());
    m_intensityReview->setPixmap(QPixmap::fromImage(image.copy()).scaled(
        m_intensityReview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_intensityReview->setToolTip(frame.intensityPngPath);
}
void CameraCaptureWizard::copyPoseRow(bool toSelected)
{
    if (isBusy()) return;
    const int source = m_table->currentRow();
    if (source < 0 || source >= m_frames.size() || (!toSelected && source == 0)) return;
    const int from = toSelected ? source : source - 1;
    const QString start = m_table->item(from, 5)->text();
    const QString end = m_table->item(from, 6)->text();
    const auto rows = m_table->selectionModel()->selectedRows();
    {
        const QSignalBlocker blocker(m_table);
        if (toSelected) {
            for (const auto &index : rows) {
                m_table->item(index.row(), 5)->setText(start);
                m_table->item(index.row(), 6)->setText(end);
            }
        } else {
            m_table->item(source, 5)->setText(start);
            m_table->item(source, 6)->setText(end);
        }
    }
    ++m_revision;
    QString error;
    if (!m_session.isEmpty() && !saveParameters(&error)) m_status->setText(error);
    validatePoses(false);
    updateActions();
}
void CameraCaptureWizard::cancelAndWait() {
    m_closing = true; ++m_revision;
    if (m_token) m_token->store(true);
    stopCapture(); m_drainWatcher->waitForFinished(); finishCapture(); m_watcher->waitForFinished();
    m_publishing = false;
}
void CameraCaptureWizard::startCapture() {
    if (isBusy() || !startAcquisition || !m_connectionReady) return;
    if (!captureSettingsReady()) {
        m_status->setText(tr("请选择有效的源码树外保存目录，并重新完成现场预检。"));
        goToStep(1); return;
    }
    if (!m_frames.isEmpty() && QMessageBox::question(this, tr("新一轮采集"),
        tr("当前会话已保存在磁盘。开始新一轮会清空界面列表，原会话不会被删除。是否继续？")) != QMessageBox::Yes) return;
    pcv::camera::CaptureConfiguration configuration;
    configuration.boundaryMode = m_boundaryMode->currentIndex() == 0
        ? pcv::camera::CaptureBoundaryMode::SdkImageFrame : pcv::camera::CaptureBoundaryMode::FixedRows;
    configuration.fixedRows = m_rows->value();
    configuration.sdkImageBoundaryVerified = m_sdkVerified->isChecked();
    configuration.requireIntensity = m_intensityEnabled->isChecked();
    configuration.externalLightEnabled = m_externalLight->isChecked();
    configuration.externalLightOperatorConfirmed = m_externalLightConfirmed->isChecked();
    configuration.storeRawDepth = configuration.boundaryMode == pcv::camera::CaptureBoundaryMode::SdkImageFrame
        && prefersDepthStorage && prefersDepthStorage();
    if (!m_gateInactive->isChecked() || (configuration.boundaryMode == pcv::camera::CaptureBoundaryMode::SdkImageFrame
        && !configuration.sdkImageBoundaryVerified)) {
        m_status->setText(tr("开始前请确认门控无效；SDK模式还需确认当前设备的一次扫描对应一幅完整图像")); return;
    }
    QString error;
    const QString session = pcv::camera::createCaptureSession(m_directory->text(), configuration,
        deviceDescription ? deviceDescription() : QStringLiteral("camera.lp"), &error);
    if (session.isEmpty()) { m_status->setText(error); setState(State::Error); return; }
    m_session = session; m_sessionIntensityEnabled = configuration.requireIntensity;
    m_geometryErrors.clear();
    { const QSignalBlocker blocker(m_textureEnabled); m_textureEnabled->setChecked(false); }
    m_frames.clear(); m_table->setRowCount(0); rebuildTable(); ++m_revision; m_closing = false;
    m_intensityPreview->clear(); m_intensityPreview->setToolTip(QString());
    m_actualRows->setText(tr("实际接收行数：—"));
    m_recorder = std::make_unique<pcv::camera::CaptureRecorder>(session, configuration);
    m_firstError.clear();
    if (m_externalLight->isChecked())
        m_status->setText(tr("请保持外置光源开启且稳定，再启动现场扫描。"));
    m_preflight->setChecked(false); m_sdkVerified->setChecked(false); m_gateInactive->setChecked(false);
    goToStep(2); setState(State::Capturing);
    if (!startAcquisition(m_recorder->queue(), configuration, &error)) {
        m_firstError = error;
        m_recorder->queue()->fail(error); stopCapture(); return;
    }
    m_status->setText(m_externalLight->isChecked()
        ? tr("正在等待完整扫描。外置光源应保持开启；结束门控并确认已接收后再停止。")
        : tr("正在等待完整扫描。请启动现场扫描，结束门控并确认已接收后再停止。"));
    m_timer->start(); updateActions();
}
void CameraCaptureWizard::pollCapture() {
    if (!m_recorder) return;
    m_frames = m_recorder->savedFrames();
    if (m_table->rowCount() != m_frames.size()) {
        rebuildTable();
        const auto depth = m_recorder->latestDepthFrame();
        if (depth && previewDepthFrame && !m_frames.isEmpty()) previewDepthFrame(depth, m_frames.constLast().directory, false);
        else if (previewFrame) previewFrame(m_recorder->latestPreview());
    }
    const auto queue = m_recorder->queue()->statistics();
    const auto storage = m_recorder->statistics();
    if (m_sessionIntensityEnabled) {
        const auto bytes = m_recorder->latestIntensity();
        const auto size = m_recorder->latestIntensitySize();
        if (!bytes.isEmpty() && size.isValid()) {
            const quint32 sdkFrameNumber = m_recorder->latestIntensityFrameNumber();
            const QImage image(reinterpret_cast<const uchar *>(bytes.constData()), size.width(), size.height(),
                               size.width(), QImage::Format_Grayscale8);
            m_intensityPreview->setPixmap(QPixmap::fromImage(image.copy()).scaled(
                m_intensityPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_intensityPreview->setToolTip(tr("最新亮度图：%1 × %2 px；Mono8；SDK帧号 %3")
                .arg(size.width()).arg(size.height()).arg(sdkFrameNumber));
            m_actualRows->setText(tr("实际点云行数：%1；图像 %2 × %3；亮度 Mono8；SDK帧号 %4")
                .arg(queue.lastHeight).arg(size.width()).arg(size.height()).arg(sdkFrameNumber));
        }
    }
    m_actualRows->setText(queue.received ? tr("实际接收行数：%1；当前图像 %2 × %3").arg(queue.lastHeight)
        .arg(queue.lastWidth).arg(queue.lastHeight) : tr("实际接收行数：—"));
    const double throughput = storage.elapsedNs ? double(storage.savedBytes) * 1.e9 / storage.elapsedNs / (1024 * 1024) : 0;
    m_status->setText(tr("已接收 %1（亮度 %2），等待保存 %3（%4 MiB），已保存 %5（亮度 %6）；平均写入 %7 MiB/s；队列峰值 %8 帧/%9 MiB\n%10")
        .arg(queue.received).arg(queue.intensityReceived).arg(queue.pending)
        .arg(double(queue.pendingBytes) / (1024 * 1024), 0, 'f', 1)
        .arg(storage.saved).arg(storage.intensitySaved).arg(throughput, 0, 'f', 1)
        .arg(queue.peakPending).arg(double(queue.peakBytes) / (1024 * 1024), 0, 'f', 1)
        .arg(m_session));
    if (!m_recorder->queue()->error().isEmpty()) stopCapture();
}
void CameraCaptureWizard::stopCapture() {
    if (!m_recorder || m_draining) return;
    m_draining = true;
    setState(State::Draining);
    m_timer->stop();
    m_status->setText(tr("正在停止接收并等待完整帧保存，请勿关闭程序…"));
    updateActions();
    if (stopAcquisition) stopAcquisition(); // Quiesce SDK callbacks before draining.
    auto *recorder = m_recorder.get();
    m_drainWatcher->setFuture(QtConcurrent::run([recorder] { recorder->finish(); }));
}
void CameraCaptureWizard::finishCapture() {
    if (!m_recorder || !m_draining || m_drainWatcher->isRunning()) return;
    m_frames = m_recorder->savedFrames();
    const QString error = m_recorder->queue()->error();
    const auto queue = m_recorder->queue()->statistics();
    const auto storage = m_recorder->statistics();
    if (queue.received) m_actualRows->setText(tr("实际接收行数：%1").arg(queue.lastHeight));
    qInfo() << "Capture session metrics" << m_session << "received=" << queue.received << "saved=" << storage.saved
            << "intensityReceived=" << queue.intensityReceived << "intensitySaved=" << storage.intensitySaved
            << "receiveWorkNs=" << queue.receiveWorkNs << "sdkConversionNs=" << queue.sdkConversionNs
            << "queueWaitNs=" << queue.queueWaitNs << "pointConversionNs=" << storage.pointConversionNs
            << "writeWorkNs=" << storage.writeWorkNs << "elapsedNs=" << storage.elapsedNs
            << "savedBytes=" << storage.savedBytes << "peakPending=" << queue.peakPending << "peakBytes=" << queue.peakBytes;
    m_recorder.reset(); m_draining = false; rebuildTable();
    if (!error.isEmpty()) m_firstError = error;
    m_status->setText(error.isEmpty() ? tr("采集已停止，已保存 %1 帧：%2").arg(m_frames.size()).arg(m_session)
        : tr("软件已停止接收，机器人可能仍在运行：%1；保留 %2 个已保存帧").arg(error).arg(m_frames.size()));
    const double rate = storage.elapsedNs ? double(storage.savedBytes) * 1.e9 / storage.elapsedNs / (1024 * 1024) : 0;
    m_status->setText(m_status->text() + tr("\n已接收 %1；保存吞吐 %2 MiB/s；队列峰值 %3 帧/%4 MiB")
        .arg(queue.received).arg(rate, 0, 'f', 1).arg(queue.peakPending).arg(double(queue.peakBytes) / (1024 * 1024), 0, 'f', 1));
    if (m_externalLight->isChecked())
        m_status->setText(m_status->text() + tr("\n如现场扫描已结束，可关闭外置光源。"));
    QString detail; if (!saveParameters(&detail)) m_status->setText(m_status->text() + "\n" + detail);
    if (!m_closing) goToStep(m_frames.isEmpty() ? 0 : 3);
    setState(error.isEmpty() ? State::Reviewing : State::Error);
    validatePoses(false);
    updateActions();
}
void CameraCaptureWizard::rebuildTable() {
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(m_frames.size());
    for (int row = 0; row < m_frames.size(); ++row) {
        const auto &frame = m_frames.at(row);
        const QString intensityDetails = frame.intensityAvailable
            ? tr("有效 %1×%2\n%3").arg(frame.width).arg(frame.height)
                .arg(QDir(frame.directory).filePath(QStringLiteral("intensity.png")))
            : tr("缺失/不可用");
        const QStringList values{QString(), QString::number(frame.id), QString::number(frame.timeStamp),
            QStringLiteral("%1×%2").arg(frame.width).arg(frame.height), frame.depthAvailable ? tr("按需转换") : QString::number(frame.pointCount), {}, {},
            tr("已保存"), QString::number(frame.sdkFrameNumber), intensityDetails};
        for (int col = 0; col < values.size(); ++col) {
            if (m_table->item(row, col)) continue;
            auto *item = new QTableWidgetItem(values.at(col));
            if (col == 0) { item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked); }
            else if (col != 5 && col != 6) item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_table->setItem(row, col, item);
        }
    }
}
bool CameraCaptureWizard::saveParameters(QString *error) {
    QJsonArray frames;
    for (int row = 0; row < m_frames.size(); ++row)
        frames.push_back(QJsonObject{{"id", qint64(m_frames.at(row).id)},
            {"included", m_table->item(row, 0)->checkState() == Qt::Checked},
            {"start", m_table->item(row, 5)->text()}, {"end", m_table->item(row, 6)->text()}});
    return pcv::camera::writeCaptureParameters(m_session,
        {{"schema", "pcv-camera-parameters-v1"}, {"calibration", m_xml->text()}, {"frames", frames},
         {"intensity_enabled", m_sessionIntensityEnabled},
         {"external_light_enabled", m_externalLight->isChecked()},
         {"external_light_operator_confirmed", m_externalLightConfirmed->isChecked()},
         {"texture_mapping_enabled", m_textureEnabled->isChecked()}}, error);
}
void CameraCaptureWizard::loadSession() {
    if (isBusy()) return;
    const QString path = QFileDialog::getExistingDirectory(this, tr("选择采集会话目录")); if (path.isEmpty()) return;
    loadSessionDirectory(path);
}
void CameraCaptureWizard::loadSessionDirectory(const QString &path) {
    if (isBusy()) return;
    QString error; auto records = pcv::camera::readCaptureSession(path, &error);
    if (!error.isEmpty()) { m_status->setText(error); return; }
    const auto parameters = pcv::camera::readCaptureParameters(path);
    const QSignalBlocker textureBlocker(m_textureEnabled);
    m_textureEnabled->setChecked(parameters.value(QStringLiteral("texture_mapping_enabled")).toBool(false));
    QFile sessionFile(QDir(path).filePath("session.json"));
    QJsonObject sessionMetadata;
    if (sessionFile.open(QIODevice::ReadOnly)) sessionMetadata = QJsonDocument::fromJson(sessionFile.readAll()).object();
    m_sessionIntensityEnabled = sessionMetadata.value("intensity_enabled").toBool(
        parameters.value("intensity_enabled").toBool(false));
    m_intensityEnabled->setChecked(m_sessionIntensityEnabled);
    if (parameters.contains(QStringLiteral("external_light_enabled")))
        m_externalLight->setChecked(parameters.value(QStringLiteral("external_light_enabled")).toBool());
    if (parameters.contains(QStringLiteral("external_light_operator_confirmed")))
        m_externalLightConfirmed->setChecked(parameters.value(QStringLiteral("external_light_operator_confirmed")).toBool());
    QVector<pcv::camera::CaptureFrameRecord> ordered;
    for (const auto &value : parameters.value("frames").toArray())
        for (const auto &record : records)
            if (qint64(record.id) == value.toObject().value("id").toInteger()) ordered.push_back(record);
    for (const auto &record : records) {
        bool found = false; for (const auto &item : ordered) if (item.id == record.id) found = true;
        if (!found) ordered.push_back(record);
    }
    m_session = path; m_frames = ordered; m_geometryErrors.clear();
    m_actualRows->setText(m_frames.isEmpty() ? tr("实际接收行数：—") : tr("实际接收行数：%1").arg(m_frames.last().height));
    const QSignalBlocker tableBlocker(m_table), xmlBlocker(m_xml);
    m_table->setRowCount(0); rebuildTable();
    const QString calibration = parameters.value("calibration").toString();
    m_xml->setText(calibration.isEmpty() || QFileInfo(calibration).isAbsolute()
        ? calibration : QDir(path).filePath(calibration));
    for (int row = 0; row < m_frames.size(); ++row)
        for (const auto &value : parameters.value("frames").toArray()) {
            const auto entry = value.toObject();
            if (entry.value("id").toInteger() != qint64(m_frames.at(row).id)) continue;
            m_table->item(row, 0)->setCheckState(entry.value("included").toBool(true) ? Qt::Checked : Qt::Unchecked);
            m_table->item(row, 5)->setText(entry.value("start").toString()); m_table->item(row, 6)->setText(entry.value("end").toString());
        }
    ++m_revision; m_status->setText(tr("会话已打开：%1 帧").arg(m_frames.size()));
    m_validatedXml.clear(); goToStep(3); validatePoses(false); updateActions();
}
void CameraCaptureWizard::moveRow(int offset) {
    const int row = m_table->currentRow(), to = row + offset;
    if (isBusy() || row < 0 || to < 0 || to >= m_frames.size()) return;
    const QSignalBlocker blocker(m_table);
    m_frames.swapItemsAt(row, to);
    for (int col = 0; col < m_table->columnCount(); ++col) {
        auto *a = m_table->takeItem(row, col), *b = m_table->takeItem(to, col);
        m_table->setItem(row, col, b); m_table->setItem(to, col, a);
    }
    m_table->setCurrentCell(to, 0); ++m_revision;
    QString error; if (!saveParameters(&error)) m_status->setText(error);
}
void CameraCaptureWizard::stitch() {
    if (isBusy() || !validatePoses(true)) return;
    pcv::interface::StitchingRequest request; request.input.calibrationPath = m_xml->text();
    QVector<QString> textureDirectories;
    for (int row = 0; row < m_frames.size(); ++row) {
        if (m_table->item(row, 0)->checkState() != Qt::Checked) continue;
        if (m_textureEnabled->isChecked()) {
            const auto textureFrame = pcv::camera::readCapturedFrame(m_frames.at(row).directory, {}, pcv::camera::CaptureFrameReadMode::PreviewOnly);
            if (!textureFrame.ok || textureFrame.intensityMono8.isEmpty()) {
                m_status->setText(tr("第 %1 行亮度图校验失败：%2")
                    .arg(row + 1).arg(textureFrame.error.isEmpty()
                        ? tr("亮度图缺失或无效") : textureFrame.error));
                m_table->setCurrentCell(row, 9);
                return;
            }
        }
        pcv::interface::StitchingFrameInput input;
        if (!parsePose(m_table->item(row, 5)->text(), &input.startPose)
            || !parsePose(m_table->item(row, 6)->text(), &input.endPose)) {
            m_status->setText(tr("第 %1 行 Start/End 必须各包含六个有限数值").arg(row + 1)); return;
        }
        input.captureFrameDirectory = m_frames.at(row).directory;
        request.input.frames.push_back(input);
        textureDirectories.push_back(input.captureFrameDirectory);
    }
    if (request.input.frames.size() < 2) { m_status->setText(tr("至少勾选两帧并填写位姿")); return; }
    pointcloud::HandEyeCalibration calibration; QString error;
    if (!pointcloud::loadHandEyeCalibration(m_xml->text(), &calibration, &error)) { m_status->setText(error); return; }
    if (!saveParameters(&error)) { m_status->setText(error); return; }
    if (canStitch && !canStitch()) return;
    QString name = QStringLiteral("pose_stitch_v1_%1").arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz"));
    const QDir root(m_session); QString unique = name;
    for (int i = 1; root.exists(unique); ++i) unique = name + "_" + QString::number(i);
    request.artifacts.outputDirectory = root.filePath(unique);
    request.registration.options.enabled = false;
    request.registration.collectDiagnosticFrames = false;
    request.seam.enabled = false;
    m_token = std::make_shared<std::atomic_bool>(false);
    const auto token = m_token; request.isCancelled = [token] { return token->load(); };
    m_runningRevision = m_revision; m_closing = false;
    m_publishing = true;
    setState(State::Publishing);
    const bool textureEnabled = m_textureEnabled->isChecked();
    const QString intensityDirectory = m_sessionIntensityEnabled ? m_session : QString();
    m_watcher->setFuture(QtConcurrent::run([request, textureDirectories, textureEnabled, token, intensityDirectory] {
        try {
            auto result = pcv::interface::stitch(request);
            result.textureSourceDirectory = intensityDirectory;
            if (result.success && token->load()) {
                result.success = false;
                result.cancelled = true;
                result.message = QStringLiteral("拼接已取消，未发布结果");
            }
            if (result.success && textureEnabled) {
                QString textureError;
                if (!attachCaptureTexture(&result, textureDirectories, token, &textureError)) {
                    result.success = false;
                    result.errorCode = token->load() ? QStringLiteral("PCV_TEXTURE_CANCELLED")
                                                     : QStringLiteral("PCV_TEXTURE_001");
                    result.cancelled = token->load();
                    result.textureError = textureError;
                    result.message = textureError;
                }
            }
            return result;
        } catch (const std::exception &e) { pcv::interface::StitchingResult result; result.message = QString::fromLocal8Bit(e.what()); return result; }
    }));
    m_status->setText(tr("正在位姿直拼并事务保存…")); updateActions();
}
void CameraCaptureWizard::updateActions() {
    const bool busy = isBusy();
    if (!busy && m_step == 1) setState(captureSettingsReady() ? State::ReadyToCapture : State::Configuring);
    m_start->setEnabled(!busy && captureSettingsReady());
    m_open->setEnabled(!busy);
    m_stop->setEnabled(isCapturing() && !m_draining);
    m_stitch->setEnabled(!busy && validatePoses(false));
    m_cancel->setEnabled(m_watcher->isRunning());
    for (QWidget *widget : std::initializer_list<QWidget *>{m_table, m_directory, m_xml, m_rows,
        m_up, m_down, m_preview, m_browseDirectory, m_browseXml, m_copyPrevious,
        m_copySelected, m_validate, m_preflight, m_advanced,
        m_externalLight, m_externalLightConfirmed, m_intensityReview, m_textureEnabled}) widget->setEnabled(!busy);
    m_intensityEnabled->setEnabled(!busy && m_boundaryMode->currentIndex() == 0);
    m_boundaryMode->setEnabled(!busy); m_sdkVerified->setEnabled(!busy); m_gateInactive->setEnabled(!busy);
    m_back->setEnabled(!busy && m_step > 0);
    m_next->setEnabled(!busy && (m_step == 0 ? m_connectionReady : m_step == 1
        ? captureSettingsReady() : m_step == 2 ? !m_frames.isEmpty() : m_connectionReady));
    if (workflowChanged) workflowChanged();
}
