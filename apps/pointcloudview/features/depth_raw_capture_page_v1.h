#pragma once

#include <QWidget>
#include <memory>
#include <functional>

namespace pcv::camera { class Mv3dLpCamera; class CaptureRecorder; struct DeviceInfo; }
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
namespace pcv::render { class DepthImageCanvasV1; }

class DepthRawCapturePageV1 final : public QWidget {
public:
    explicit DepthRawCapturePageV1(QWidget *parent = nullptr);
    ~DepthRawCapturePageV1() override;
    bool isCapturing() const;
    std::function<bool()> canStartCapture;
    void stopAndWait();
private:
    void refreshDevices();
    void toggleCapture();
    void updateStatus();
    QComboBox *m_devices = nullptr;
    QLineEdit *m_output = nullptr;
    QLineEdit *m_calibration = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_capture = nullptr;
    QTimer *m_timer = nullptr;
    pcv::render::DepthImageCanvasV1 *m_preview = nullptr;
    std::unique_ptr<pcv::camera::Mv3dLpCamera> m_camera;
    std::unique_ptr<pcv::camera::CaptureRecorder> m_recorder;
    QString m_session;
};
