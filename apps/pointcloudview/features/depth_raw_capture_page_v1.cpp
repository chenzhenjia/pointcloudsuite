#include "depth_raw_capture_page_v1.h"

#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/mv3d_lp_camera.h>
#include <pcv/render/depth_image_canvas_v1.h>

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QStandardPaths>

using namespace pcv::camera;

DepthRawCapturePageV1::DepthRawCapturePageV1(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_devices = new QComboBox(this);
    auto *refresh = new QPushButton(tr("刷新设备"), this);
    auto *deviceRow = new QHBoxLayout; deviceRow->addWidget(m_devices, 1); deviceRow->addWidget(refresh);
    auto *deviceHost = new QWidget(this); deviceHost->setLayout(deviceRow);
    form->addRow(tr("深度相机"), deviceHost);
    m_output = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), this);
    auto *chooseOutput = new QPushButton(tr("选择…"), this);
    auto *outputRow = new QHBoxLayout; outputRow->addWidget(m_output, 1); outputRow->addWidget(chooseOutput);
    auto *outputHost = new QWidget(this); outputHost->setLayout(outputRow);
    form->addRow(tr("输出目录"), outputHost);
    m_calibration = new QLineEdit(this); m_calibration->setReadOnly(true);
    auto *chooseCalibration = new QPushButton(tr("选择…"), this);
    auto *calibRow = new QHBoxLayout; calibRow->addWidget(m_calibration, 1); calibRow->addWidget(chooseCalibration);
    auto *calibHost = new QWidget(this); calibHost->setLayout(calibRow);
    form->addRow(tr("厂商标定附件"), calibHost);
    root->addLayout(form);
    m_capture = new QPushButton(tr("开始深度取流"), this);
    root->addWidget(m_capture);
    m_preview = new pcv::render::DepthImageCanvasV1(this);
    m_preview->setMinimumHeight(240); root->addWidget(m_preview, 1);
    m_status = new QLabel(tr("就绪；相机内部标定参数由当前 SDK 接口不可读取。"), this);
    m_status->setWordWrap(true); m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status); root->addStretch();
    connect(refresh, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(chooseOutput, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("选择 RAW 输出目录"), m_output->text());
        if (!path.isEmpty()) m_output->setText(path);
    });
    connect(chooseCalibration, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("选择厂商标定附件"));
        if (!path.isEmpty()) m_calibration->setText(path);
    });
    connect(m_capture, &QPushButton::clicked, this, [this] { toggleCapture(); });
    m_timer = new QTimer(this); m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, [this] { updateStatus(); });
    refreshDevices();
}

DepthRawCapturePageV1::~DepthRawCapturePageV1() { stopAndWait(); }
bool DepthRawCapturePageV1::isCapturing() const { return bool(m_recorder); }

void DepthRawCapturePageV1::refreshDevices() {
#ifdef PCV_CAMERA_HAVE_HIKSDK
    CameraError error;
    const auto devices = Mv3dLpCamera::enumerateDevices(&error);
    m_devices->clear();
    for (const auto &device : devices)
        m_devices->addItem(QStringLiteral("%1 · %2%3").arg(device.modelName, device.serialNumber,
            device.isVirtual ? tr("（虚拟）") : QString()), device.serialNumber);
    if (m_devices->count() == 0) m_status->setText(error.message.isEmpty() ? tr("未发现相机设备") : error.message);
#else
    m_devices->clear(); m_status->setText(tr("当前构建未包含 Hikvision SDK。"));
#endif
}

void DepthRawCapturePageV1::toggleCapture() {
    if (m_recorder) { stopAndWait(); return; }
#ifndef PCV_CAMERA_HAVE_HIKSDK
    m_status->setText(tr("当前构建未包含 Hikvision SDK。")); return;
#else
    if (m_devices->currentIndex() < 0 || !QDir(m_output->text()).exists()) {
        m_status->setText(tr("请选择已存在的源码树外输出目录和相机设备。")); return;
    }
    if (canStartCapture && !canStartCapture()) { m_status->setText(tr("其他采集或处理任务正在使用相机。")); return; }
    if (QMessageBox::question(this, tr("确认完整图像边界"),
        tr("本次采集保留设备现有触发/门控配置，并把每个 SDK RangeImage 回调作为一张完整图保存。请确认当前相机配置满足此边界。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    pcv::camera::CaptureConfiguration config;
    config.boundaryMode = pcv::camera::CaptureBoundaryMode::SdkImageFrame;
    config.sdkImageBoundaryVerified = true;
    config.requireIntensity = true;
    config.storeRawDepth = true;
    config.exportPureRaw = true;
    config.calibrationAttachment = m_calibration->text();
    config.deviceSerial = m_devices->currentData().toString();
    config.deviceModel = m_devices->currentText().section(QStringLiteral(" · "), 0, 0);
    QString error;
    m_session = pcv::camera::createCaptureSession(m_output->text(), config,
        m_devices->currentText(), &error);
    if (m_session.isEmpty()) { m_status->setText(error); return; }
    m_recorder = std::make_unique<pcv::camera::CaptureRecorder>(m_session, config);
    m_camera = std::make_unique<pcv::camera::Mv3dLpCamera>(m_devices->currentData().toString());
    m_camera->requestImageMode(pcv::camera::ImageMode::RangeImage);
    const auto configured = m_camera->configureCapture(config);
    if (!configured.ok()) { m_status->setText(configured.message); stopAndWait(); return; }
    m_camera->frameSlot().setCaptureQueue(m_recorder->queue());
    const auto started = m_camera->start();
    if (!started.ok()) { m_status->setText(started.message); stopAndWait(); return; }
    m_devices->setEnabled(false); m_output->setEnabled(false); m_calibration->setEnabled(false);
    m_capture->setText(tr("停止并保存剩余帧")); m_timer->start();
    m_status->setText(tr("正在连续保存 SDK 完整 C16 图像：%1").arg(m_session));
#endif
}

void DepthRawCapturePageV1::stopAndWait() {
    if (!m_recorder) return;
    m_timer->stop();
    if (m_camera) { m_camera->stop(); m_camera.reset(); }
    m_recorder->queue()->finish(); m_recorder->finish();
    const auto stats = m_recorder->statistics();
    const auto error = m_recorder->queue()->error();
    m_recorder.reset();
    m_devices->setEnabled(true); m_output->setEnabled(true); m_calibration->setEnabled(true);
    m_capture->setText(tr("开始深度取流"));
    m_status->setText(error.isEmpty()
        ? tr("采集完成：%1 帧，%2 字节。%3").arg(stats.saved).arg(stats.savedBytes).arg(m_session)
        : tr("采集已停止：%1 帧已保存；错误：%2；%3").arg(stats.saved).arg(error, m_session));
}

void DepthRawCapturePageV1::updateStatus() {
    if (!m_recorder) return;
#ifdef PCV_CAMERA_HAVE_HIKSDK
    if (const auto frame = m_camera->frameSlot().take()) {
        pcv::render::DepthImageDisplayV1 image;
        image.width = frame->width; image.height = frame->height;
        image.c16 = frame->data; image.mono8 = frame->intensityData;
        m_preview->setDepth(image);
    }
    const auto stats = m_recorder->queue()->statistics();
    m_status->setText(tr("收到 %1 帧 · 待保存 %2 · 已保存 %3 · 最近尺寸 %4 × %5 · %6 字节\n%7")
        .arg(stats.received).arg(stats.pending).arg(m_recorder->statistics().saved)
        .arg(stats.lastWidth).arg(stats.lastHeight).arg(m_recorder->statistics().savedBytes).arg(m_session));
    if (!m_recorder->queue()->error().isEmpty()) stopAndWait();
#endif
}
