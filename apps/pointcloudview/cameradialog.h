#pragma once

#include <pcv/camera/frame_data.h>
#include <pcv/camera/frame_converter.h>
#include "live_pipeline.h"
#include <pcv/core/point_types.h>

#include <QDialog>
#include <QFutureWatcher>
#include <QString>

#include <functional>
#include <memory>

class QCloseEvent;
class QShowEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QThread;
class QTimer;

namespace pointcloud { struct Point3D; }

namespace pcv::camera { class FrameSource; }
namespace Ui { class CameraDialog; }

// Live point-cloud acquisition dialog. Enumerates Hikvision Mv3dLp cameras,
// connects, starts measurement and streams the latest point-cloud frame to
// the injected publish callback (the application binds it to the canvas).
// A mock source is available so the UI can be exercised without a camera.
class CameraDialog final : public QDialog {
public:
    explicit CameraDialog(QWidget *parent = nullptr);
    ~CameraDialog() override;

    // The application binds this to PointCloudCanvas::setCloudStreaming.
    std::function<void(const QVector<pointcloud::Point3D> &)> cloudPublished;
    std::function<void(const QVector<pointcloud::Point3D> &)> cloudFrozen;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refreshDevices();
    void toggleConnection();
    void toggleStreaming();
    void freezeCurrentFrame();
    void onFrameAvailable();
    void triggerFrame();
    void onException(const QString &description);
    void attemptReconnect();
    void installFrameNotification();
    void launchConversion(std::shared_ptr<const pcv::camera::CameraFrame> frame);
    void stopSourceSafely();
    void setBusy(bool busy);
    void appendLog(const QString &message);
    void refreshDeviceDetails(int index);
    static QString frameTypeName(pcv::camera::FrameType type);
    static QString frameTypeHex(pcv::camera::FrameType type);

    QComboBox *m_deviceList = nullptr;
    QComboBox *m_modeList = nullptr;
    QComboBox *m_triggerList = nullptr;
    QPushButton *m_connect = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_triggerButton = nullptr;
    QPushButton *m_freezeButton = nullptr;
    QPushButton *m_refresh = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_frameInfo = nullptr;
    QLabel *m_rateInfo = nullptr;
    QLabel *m_deviceModel = nullptr;
    QLabel *m_deviceSerial = nullptr;
    QLabel *m_deviceIp = nullptr;
    QLabel *m_deviceVirtual = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_frameWatchdogTimer = nullptr;
    std::unique_ptr<pcv::camera::FrameSource> m_source;
    std::unique_ptr<QFutureWatcher<pcv::camera::PointCloudConvertResult>> m_convertWatcher;
    std::unique_ptr<pointcloud::PipelineRunner> m_pipeline;
    std::shared_ptr<const pcv::camera::CameraFrame> m_pendingFrame;
    QVector<pcv::camera::DeviceInfo> m_devices;
    bool m_connected = false;
    bool m_streaming = false;
    bool m_closing = false;
    std::uint32_t m_lastFrameNumber = 0;
    qint64 m_lastPollMs = 0;
    int m_frameCount = 0;
    std::uint64_t m_streamGeneration = 0;
    std::uint64_t m_conversionGeneration = 0;
    std::uint32_t m_lastConvertedFrameNumber = 0;
    std::uint32_t m_lastConvertedExpectedRows = 0;
    std::uint32_t m_lastConvertedFragments = 0;
    bool m_lastConvertedComplete = false;
    QVector<pointcloud::Point3D> m_lastConvertedPoints;
    Ui::CameraDialog *ui = nullptr;
};
