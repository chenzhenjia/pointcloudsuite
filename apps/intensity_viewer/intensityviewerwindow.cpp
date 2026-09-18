#include "intensityviewerwindow.h"
#include "ui_intensityviewerwindow.h"

#include <pcv/camera/frame_source.h>
#include <pcv/camera/intensity_image.h>
#ifdef PCV_CAMERA_HAVE_HIKSDK
#include <pcv/camera/mv3d_lp_camera.h>
#endif

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>

IntensityViewerWindow::IntensityViewerWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::IntensityViewerWindow)
{
    ui->setupUi(this);
    m_deviceList = ui->cb_devices;
    m_refresh = ui->btn_refresh;
    m_connect = ui->btn_connect;
    m_start = ui->btn_start;
    m_save = ui->btn_save;
    m_preview = ui->lbl_preview;
    m_status = ui->lbl_status;
    m_frameInfo = ui->lbl_frame;

    connect(m_refresh, &QPushButton::clicked, this, &IntensityViewerWindow::refreshDevices);
    connect(m_connect, &QPushButton::clicked, this, &IntensityViewerWindow::toggleConnection);
    connect(m_start, &QPushButton::clicked, this, &IntensityViewerWindow::toggleStreaming);
    connect(m_save, &QPushButton::clicked, this, &IntensityViewerWindow::saveCurrentImage);
    refreshDevices();
}

IntensityViewerWindow::~IntensityViewerWindow()
{
    stopSourceSafely();
    delete ui;
}

void IntensityViewerWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    stopSourceSafely();
    QMainWindow::closeEvent(event);
}

void IntensityViewerWindow::refreshDevices()
{
    if (m_connected) return;
    m_devices.clear();
#ifdef PCV_CAMERA_HAVE_HIKSDK
    pcv::camera::CameraError error;
    m_devices = pcv::camera::Mv3dLpCamera::enumerateDevices(&error);
    if (!error.ok()) setStatus(QStringLiteral("设备枚举失败：%1").arg(error.message));
#else
    setStatus(QStringLiteral("未编译 Mv3dLp SDK，无法连接真实相机"));
#endif

    m_deviceList->clear();
    for (const auto &device : m_devices) {
        m_deviceList->addItem(
            QStringLiteral("%1  %2  %3")
                .arg(device.modelName, device.serialNumber, device.currentIp),
            device.serialNumber);
    }
    if (m_devices.isEmpty()) {
        m_deviceList->addItem(QStringLiteral("未发现 Mv3dLp 相机"));
        m_connect->setEnabled(false);
    } else {
        m_connect->setEnabled(true);
        setStatus(QStringLiteral("已发现 %1 台相机").arg(m_devices.size()));
    }
}

void IntensityViewerWindow::toggleConnection()
{
    if (m_connected) {
        stopSourceSafely();
        m_source.reset();
        m_connected = false;
        m_connect->setText(QStringLiteral("连接"));
        m_start->setEnabled(false);
        m_save->setEnabled(false);
        setStatus(QStringLiteral("未连接"));
        return;
    }

    const int index = m_deviceList->currentIndex();
    if (index < 0 || index >= m_devices.size()) return;
#ifdef PCV_CAMERA_HAVE_HIKSDK
    m_source = std::make_unique<pcv::camera::Mv3dLpCamera>(
        m_devices.at(index).serialNumber);
    static_cast<pcv::camera::Mv3dLpCamera *>(m_source.get())
        ->requestImageMode(pcv::camera::ImageMode::RangeImage);
    m_connected = true;
    m_connect->setText(QStringLiteral("断开"));
    m_start->setEnabled(true);
    setStatus(QStringLiteral("已连接，等待开始取流"));
#else
    setStatus(QStringLiteral("未编译 Mv3dLp SDK，无法连接真实相机"));
#endif
}

void IntensityViewerWindow::toggleStreaming()
{
    if (!m_source) return;
    if (m_streaming) {
        stopSourceSafely();
        m_streaming = false;
        m_start->setText(QStringLiteral("开始取流"));
        setStatus(QStringLiteral("已停止取流"));
        return;
    }

    m_source->frameSlot().setFramePublishedCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() { onFrameAvailable(); },
                                  Qt::QueuedConnection);
    });
    const auto error = m_source->start();
    if (!error.ok()) {
        m_source->frameSlot().clearFramePublishedCallback();
        setStatus(QStringLiteral("启动失败：%1").arg(error.message));
        return;
    }
    m_streaming = true;
    m_start->setText(QStringLiteral("停止取流"));
    setStatus(QStringLiteral("取流中"));
}

void IntensityViewerWindow::onFrameAvailable()
{
    if (m_closing || !m_streaming || !m_source) return;
    const auto frame = m_source->frameSlot().take();
    if (!frame) return;
    if (frame->kind == pcv::camera::FrameKind::Exception) {
        setStatus(QStringLiteral("相机异常：%1").arg(frame->exceptionDescription));
        return;
    }

    const auto result = pcv::camera::extractIntensityImage(*frame);
    if (!result.ok) {
        setStatus(QStringLiteral("亮度帧无效：%1").arg(result.error));
        return;
    }
    m_currentImage = QImage(reinterpret_cast<const uchar *>(result.mono8.constData()),
                             int(result.width), int(result.height), int(result.width),
                             QImage::Format_Grayscale8).copy();
    m_currentFrameNumber = result.frameNumber;
    m_preview->setPixmap(QPixmap::fromImage(m_currentImage).scaled(
        m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_frameInfo->setText(QStringLiteral("帧 #%1  尺寸 %2 x %3  有效像素 %4  丢帧 %5")
                         .arg(result.frameNumber)
                         .arg(result.width)
                         .arg(result.height)
                         .arg(result.mono8.size())
                         .arg(m_source->frameSlot().droppedCount()));
    m_save->setEnabled(true);
}

void IntensityViewerWindow::saveCurrentImage()
{
    if (m_currentImage.isNull()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存亮度图"), QString(), QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("文件已存在，不覆盖原文件"));
        return;
    }
    if (!m_currentImage.save(path, "PNG")) {
        QMessageBox::critical(this, QStringLiteral("保存失败"),
                              QStringLiteral("无法写入 PNG：%1").arg(path));
        return;
    }
    setStatus(QStringLiteral("已保存帧 #%1：%2").arg(m_currentFrameNumber).arg(path));
}

void IntensityViewerWindow::stopSourceSafely()
{
    if (!m_source) return;
    m_source->frameSlot().clearFramePublishedCallback();
    m_source->stop();
    m_streaming = false;
}

void IntensityViewerWindow::setStatus(const QString &text)
{
    if (m_status) m_status->setText(text);
}
