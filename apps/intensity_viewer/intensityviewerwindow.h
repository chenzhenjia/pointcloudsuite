#pragma once

#include <pcv/camera/frame_data.h>

#include <QMainWindow>
#include <QImage>

#include <memory>

class QLabel;
class QPushButton;
class QComboBox;
class QCloseEvent;
class QShowEvent;
class QPixmap;

namespace pcv::camera { class FrameSource; }
namespace Ui { class IntensityViewerWindow; }

class IntensityViewerWindow final : public QMainWindow {
public:
    explicit IntensityViewerWindow(QWidget *parent = nullptr);
    ~IntensityViewerWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void refreshDevices();
    void toggleConnection();
    void toggleStreaming();
    void onFrameAvailable();
    void saveCurrentImage();
    void stopSourceSafely();
    void setStatus(const QString &text);

    Ui::IntensityViewerWindow *ui = nullptr;
    QComboBox *m_deviceList = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_connect = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_save = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_frameInfo = nullptr;
    std::unique_ptr<pcv::camera::FrameSource> m_source;
    QVector<pcv::camera::DeviceInfo> m_devices;
    QImage m_currentImage;
    std::uint32_t m_currentFrameNumber = 0;
    bool m_connected = false;
    bool m_streaming = false;
    bool m_closing = false;
};
