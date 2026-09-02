#include "cameradialog.h"

#include <pcv/camera/frame_converter.h>
#include <pcv/camera/mock_frame_source.h>
#ifdef PCV_CAMERA_HAVE_HIKSDK
#include <pcv/camera/mv3d_lp_camera.h>
#endif

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QShowEvent>

#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <cstdint>

CameraDialog::CameraDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("相机实时取流（点云图）"));
    resize(560, 320);

    auto *deviceRow = new QHBoxLayout;
    m_deviceList = new QComboBox(this);
    m_deviceList->setMinimumWidth(320);
    m_refresh = new QPushButton(tr("刷新设备"), this);
    deviceRow->addWidget(new QLabel(tr("设备："), this));
    deviceRow->addWidget(m_deviceList, 1);
    deviceRow->addWidget(m_refresh);

    auto *modeRow = new QHBoxLayout;
    m_modeList = new QComboBox(this);
    m_modeList->addItem(tr("深度图 Range_Image（推荐）"), int(pcv::camera::ImageMode::RangeImage));
    m_modeList->addItem(tr("点云图 PointCloud_Image"), int(pcv::camera::ImageMode::PointCloudImage));
    m_modeList->addItem(tr("自动（按帧类型自适应）"), int(pcv::camera::ImageMode::PointCloudImage));
    m_modeList->setCurrentIndex(0);
    modeRow->addWidget(new QLabel(tr("取流模式："), this));
    modeRow->addWidget(m_modeList);
    modeRow->addStretch(1);

    auto *triggerRow = new QHBoxLayout;
    m_triggerList = new QComboBox(this);
    m_triggerList->addItem(tr("连续取流"), 0);
    m_triggerList->addItem(tr("软触发"), 1);
    m_triggerButton = new QPushButton(tr("触发一帧"), this);
    m_triggerButton->setEnabled(false);
    triggerRow->addWidget(new QLabel(tr("触发："), this));
    triggerRow->addWidget(m_triggerList);
    triggerRow->addWidget(m_triggerButton);
    triggerRow->addStretch(1);

    auto *actionRow = new QHBoxLayout;
    m_connect = new QPushButton(tr("连接"), this);
    m_start = new QPushButton(tr("开始取流"), this);
    m_start->setEnabled(false);
    actionRow->addWidget(m_connect);
    actionRow->addWidget(m_start);
    actionRow->addStretch(1);

    m_status = new QLabel(tr("未连接"), this);
    m_frameInfo = new QLabel(tr("帧：-"), this);
    m_rateInfo = new QLabel(tr("帧率：-"), this);

    auto *infoRow = new QHBoxLayout;
    infoRow->addWidget(m_status);
    infoRow->addWidget(m_frameInfo);
    infoRow->addWidget(m_rateInfo);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(deviceRow);
    layout->addLayout(modeRow);
    layout->addLayout(triggerRow);
    layout->addLayout(actionRow);
    layout->addLayout(infoRow);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &CameraDialog::attemptReconnect);

    connect(m_refresh, &QPushButton::clicked, this, &CameraDialog::refreshDevices);
    connect(m_connect, &QPushButton::clicked, this, &CameraDialog::toggleConnection);
    connect(m_start, &QPushButton::clicked, this, &CameraDialog::toggleStreaming);
    connect(m_triggerButton, &QPushButton::clicked, this, &CameraDialog::triggerFrame);

    m_convertWatcher = std::make_unique<QFutureWatcher<pcv::camera::PointCloudConvertResult>>(this);
    connect(m_convertWatcher.get(), &QFutureWatcher<pcv::camera::PointCloudConvertResult>::finished,
            this, [this]() {
        const auto converted = m_convertWatcher->result();
        if (!m_closing && m_streaming && m_conversionGeneration == m_streamGeneration
            && converted.ok) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const auto frameNumber = m_lastConvertedFrameNumber;
            if (m_lastFrameNumber != 0 && frameNumber > m_lastFrameNumber) {
                ++m_frameCount;
                if (m_lastPollMs != 0 && now > m_lastPollMs) {
                    const qreal seconds = qreal(now - m_lastPollMs) / 1000.0;
                    if (seconds > 0.0)
                        m_rateInfo->setText(tr("帧率：%1 FPS").arg(qreal(m_frameCount) / seconds, 0, 'f', 1));
                }
            } else if (m_lastFrameNumber == 0) {
                ++m_frameCount;
            }
            m_lastFrameNumber = frameNumber;
            m_lastPollMs = now;
            m_frameInfo->setText(tr("帧 #%1  点数 %2").arg(frameNumber).arg(converted.points.size()));
            pointcloud::LiveFrame frame;
            frame.points = converted.points;
            frame.sourceMode = QStringLiteral("camera.lp");
            frame.frameNumber = frameNumber;
            frame.streamGeneration = m_conversionGeneration;
            frame.hasFrame = true;
            frame.ok = true;
            if (m_pipeline) {
                const pointcloud::LiveFrame output = m_pipeline->run(frame);
                if (!output.ok && !m_closing)
                    m_frameInfo->setText(tr("帧：发布失败（%1）").arg(output.error));
            } else if (cloudPublished) {
                cloudPublished(converted.points);
            }
        } else if (!m_closing && m_streaming && m_conversionGeneration == m_streamGeneration
                   && !converted.ok) {
            m_frameInfo->setText(tr("帧：无效（%1）").arg(converted.error));
        }
        if (m_pendingFrame && !m_closing && m_streaming) {
            auto next = std::move(m_pendingFrame);
            launchConversion(std::move(next));
        }
    });

    refreshDevices();
}

CameraDialog::~CameraDialog()
{
    stopSourceSafely();
    m_reconnectTimer->stop();
}

void CameraDialog::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    stopSourceSafely();
    m_reconnectTimer->stop();
    m_streaming = false;
    QDialog::closeEvent(event);
}

void CameraDialog::showEvent(QShowEvent *event)
{
    m_closing = false;
    QDialog::showEvent(event);
}

void CameraDialog::refreshDevices()
{
    if (m_connected) return;
#ifdef PCV_CAMERA_HAVE_HIKSDK
    m_devices = pcv::camera::Mv3dLpCamera::enumerateDevices();
#else
    m_devices.clear();
#endif
    m_deviceList->clear();
    if (m_devices.isEmpty()) {
        m_deviceList->addItem(tr("未发现相机（仅 Mock 源可用）"));
    }
    for (const auto &device : m_devices) {
        m_deviceList->addItem(QStringLiteral("%1%2  %3  %4")
                                  .arg(device.modelName)
                                  .arg(device.isVirtual ? tr("（虚拟相机）") : QString())
                                  .arg(device.serialNumber)
                                  .arg(device.currentIp));
    }
    // Always offer the mock source for camera-free verification.
    m_deviceList->addItem(tr("Mock 合成点云源（调试）"));
}

void CameraDialog::toggleConnection()
{
    if (m_connected) {
        stopSourceSafely();
        m_source.reset();
        m_connected = false;
        m_connect->setText(tr("连接"));
        m_start->setEnabled(false);
        m_status->setText(tr("未连接"));
        m_frameInfo->setText(tr("帧：-"));
        m_rateInfo->setText(tr("帧率：-"));
        return;
    }

    const int index = m_deviceList->currentIndex();
    const bool useMock = index < 0
        || index >= m_devices.size()
        || m_devices.isEmpty();
    setBusy(true);
    if (useMock) {
        m_source = std::make_unique<pcv::camera::MockFrameSource>(640, 480);
    } else {
#ifdef PCV_CAMERA_HAVE_HIKSDK
        m_source = std::make_unique<pcv::camera::Mv3dLpCamera>(
            m_devices[index].serialNumber);
        const int modeIndex = m_modeList->currentIndex();
        // index 0 = Range_Image (default, 3DMVS-compatible); 1 = PointCloud;
        // 2 = adaptive (no explicit request).
        if (modeIndex == 0) {
            static_cast<pcv::camera::Mv3dLpCamera *>(m_source.get())
                ->requestImageMode(pcv::camera::ImageMode::RangeImage);
        } else if (modeIndex == 1) {
            static_cast<pcv::camera::Mv3dLpCamera *>(m_source.get())
                ->requestImageMode(pcv::camera::ImageMode::PointCloudImage);
        }
#else
        m_source = std::make_unique<pcv::camera::MockFrameSource>(640, 480);
        m_status->setText(tr("SDK 未启用，回退 Mock 源"));
#endif
    }
    m_connected = true;
    m_connect->setText(tr("断开"));
    m_start->setEnabled(true);
    m_triggerButton->setEnabled(m_source->supportsSoftTrigger());
    m_status->setText(useMock ? tr("已连接（Mock 源）") : tr("已连接"));
    setBusy(false);
}

void CameraDialog::toggleStreaming()
{
    if (!m_connected || !m_source) return;
    if (m_streaming) {
        stopSourceSafely();
        m_reconnectTimer->stop();
        m_streaming = false;
        m_start->setText(tr("开始取流"));
        m_rateInfo->setText(tr("帧率：-"));
        return;
    }
    ++m_streamGeneration;
    pointcloud::PipelineContext context;
    context.frameSource = [this]() { return m_source.get(); };
    context.publish = [this](const pointcloud::LiveFrame &frame) {
        if (m_closing || !m_streaming || !cloudPublished) return false;
        cloudPublished(frame.points);
        return true;
    };
    QStringList pipelineWarnings;
    m_pipeline = pointcloud::createProductionPipeline(context, &pipelineWarnings);
    for (const QString &warning : pipelineWarnings) {
        appendLog(warning);
        m_status->setText(tr("Pipeline 警告：%1").arg(warning));
    }
    m_source->frameSlot().setFramePublishedCallback([this]() {
        QMetaObject::invokeMethod(this, &CameraDialog::onFrameAvailable, Qt::QueuedConnection);
    });
    const pcv::camera::CameraError error = m_source->start();
    if (!error.ok()) {
        m_source->frameSlot().clearFramePublishedCallback();
        m_status->setText(tr("启动失败：%1").arg(error.message));
        return;
    }
    m_streaming = true;
    m_lastFrameNumber = 0;
    m_lastPollMs = 0;
    m_frameCount = 0;
    m_start->setText(tr("停止取流"));
    m_reconnectTimer->stop();
    m_status->setText(tr("取流中"));

}

void CameraDialog::onFrameAvailable()
{
    if (!m_streaming || !m_source || m_closing) return;
    auto frame = m_source->frameSlot().take();
    if (!frame) return;

    // Device events (disconnect) are handled separately.
    if (frame->kind == pcv::camera::FrameKind::Exception) {
        onException(frame->exceptionDescription);
        return;
    }

    qInfo() << "CameraDialog: frame"
            << "type=" << frameTypeHex(frame->frameType)
            << frameTypeName(frame->frameType)
            << "valid=" << frame->valid
            << "size=" << frame->width << "x" << frame->height
            << "dataLen=" << frame->data.size()
            << "frameNumber=" << frame->frameNumber;

    if (m_convertWatcher->isRunning()) {
        m_pendingFrame = std::move(frame);
        return;
    }
    launchConversion(std::move(frame));
}

void CameraDialog::launchConversion(std::shared_ptr<const pcv::camera::CameraFrame> frame)
{
    if (!m_source || !frame) return;
    const auto source = m_source.get();
    const auto generation = m_streamGeneration;
    m_conversionGeneration = generation;
    m_lastConvertedFrameNumber = frame->frameNumber;
    m_convertWatcher->setFuture(QtConcurrent::run([source, frame, generation]() {
        Q_UNUSED(generation);
        return source->convertFrame(*frame);
    }));
}

void CameraDialog::stopSourceSafely()
{
    ++m_streamGeneration;
    if (m_source) m_source->frameSlot().clearFramePublishedCallback();
    m_streaming = false;
    m_pendingFrame.reset();
    if (m_convertWatcher && m_convertWatcher->isRunning()) m_convertWatcher->waitForFinished();
    if (m_source) m_source->stop();
    m_pipeline.reset();
}

void CameraDialog::triggerFrame()
{
#ifdef PCV_CAMERA_HAVE_HIKSDK
    if (!m_connected || m_triggerList->currentIndex() != 1) return;
    if (!m_source || !m_source->supportsSoftTrigger()) return;
    const pcv::camera::CameraError error = m_source->softTrigger();
    if (!error.ok()) {
        m_status->setText(tr("触发失败：%1").arg(error.message));
    }
#endif
}

void CameraDialog::onException(const QString &description)
{
    m_streaming = false;
    m_start->setText(tr("开始取流"));
    m_status->setText(tr("设备离线（%1），正在重连...").arg(
        description.isEmpty() ? tr("未知异常") : description));
    m_rateInfo->setText(tr("帧率：-"));
    m_reconnectTimer->start();
}

void CameraDialog::attemptReconnect()
{
#ifdef PCV_CAMERA_HAVE_HIKSDK
    if (m_closing || !m_connected || !m_source) {
        m_reconnectTimer->stop();
        return;
    }
    if (!m_source->supportsReconnect()) return;
    const pcv::camera::CameraError error = m_source->reconnectOnce();
    if (!error.ok()) {
        m_status->setText(tr("重连中...（%1）").arg(error.message));
        return;
    }
    m_reconnectTimer->stop();
    m_streaming = true;
    m_start->setText(tr("停止取流"));
    m_status->setText(tr("已重连，取流中"));
#endif
}

void CameraDialog::setBusy(bool busy)
{
    m_refresh->setEnabled(!busy);
    m_connect->setEnabled(!busy);
    m_deviceList->setEnabled(!busy);
}

void CameraDialog::appendLog(const QString &message)
{
    qInfo().noquote() << "CameraDialog:" << message;
}

QString CameraDialog::frameTypeName(pcv::camera::FrameType type)
{
    switch (type) {
    case pcv::camera::FrameType::Mono8:
        return tr("亮度 Mono8");
    case pcv::camera::FrameType::DepthC16:
        return tr("深度图 C16");
    case pcv::camera::FrameType::ProfileABC16:
        return tr("轮廓 ABC16");
    case pcv::camera::FrameType::PointCloudABC32f:
        return tr("点云 ABC32f");
    case pcv::camera::FrameType::Rgb24Packed:
        return tr("RGB24");
    default:
        return tr("未知类型");
    }
}

QString CameraDialog::frameTypeHex(pcv::camera::FrameType type)
{
    return QStringLiteral("0x%1").arg(std::uint32_t(type), 8, 16, QLatin1Char('0'));
}
