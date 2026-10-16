#pragma once
#include <pcv/camera/capture_session_v1.h>
#include <pcv/interface/stitching_interface.h>
#include <QFutureWatcher>
#include <QWidget>
#include <atomic>

class QLineEdit;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QTimer;
class QComboBox;
class QCheckBox;
class QStackedWidget;

class CameraCaptureWizard final : public QWidget {
public:
    enum class State {
        Disconnected, Connecting, Connected, Configuring, ReadyToCapture,
        Capturing, Draining, Reviewing, Publishing, Completed, Error
    };
    struct Snapshot {
        State state;
        int step;
        QString session;
        QString firstError;
        qsizetype savedFrames;
    };
    explicit CameraCaptureWizard(QWidget *parent = nullptr);
    ~CameraCaptureWizard() override;
    std::function<bool(std::shared_ptr<pcv::camera::CaptureFrameQueue>, const pcv::camera::CaptureConfiguration &, QString *)> startAcquisition;
    std::function<void()> stopAcquisition;
    std::function<QString()> deviceDescription;
    std::function<bool()> canStitch;
    std::function<void(const pcv::interface::StitchingResult &)> publishResult;
    std::function<void(const QVector<pointcloud::Point3D> &)> previewFrame;
    std::function<void()> workflowChanged;
    bool isBusy() const;
    bool isCapturing() const { return bool(m_recorder); }
    void stopCapture();
    void cancelAndWait();
    void setConnectionState(bool ready, const QString &description);
    void setConnecting();
    Snapshot snapshot() const { return {m_state, m_step, m_session, m_firstError, m_frames.size()}; }
private:
    friend struct CameraCapturePanelTestAccess;
    void startCapture();
    void finishCapture();
    void pollCapture();
    void loadSession();
    void rebuildTable();
    void moveRow(int offset);
    void stitch();
    void advanceStep();
    void backStep();
    void goToStep(int step);
    bool validatePoses(bool updateStatus);
    void copyPoseRow(bool toSelected);
    void updateActions();
    void updateStepUi();
    bool captureSettingsReady() const;
    void setState(State state);
    bool saveParameters(QString *error);
    QLineEdit *m_directory, *m_xml;
    QSpinBox *m_rows;
    QComboBox *m_boundaryMode;
    QCheckBox *m_sdkVerified, *m_gateInactive;
    QLabel *m_actualRows, *m_fixedRowsLabel;
    QTableWidget *m_table;
    QPushButton *m_start, *m_stop, *m_open, *m_stitch, *m_cancel, *m_up, *m_down, *m_preview;
    QPushButton *m_browseDirectory, *m_browseXml;
    QPushButton *m_back, *m_next, *m_copyPrevious, *m_copySelected, *m_validate;
    QStackedWidget *m_steps;
    QLabel *m_stepTitle, *m_connectionStatus, *m_validationStatus;
    QCheckBox *m_preflight;
    QWidget *m_captureAdvanced;
    QPushButton *m_advanced;
    QLabel *m_status;
    QTimer *m_timer;
    QString m_session;
    QVector<pcv::camera::CaptureFrameRecord> m_frames;
    std::unique_ptr<pcv::camera::CaptureRecorder> m_recorder;
    QFutureWatcher<pcv::interface::StitchingResult> *m_watcher;
    QFutureWatcher<void> *m_drainWatcher;
    std::shared_ptr<std::atomic_bool> m_token;
    quint64 m_revision = 0, m_runningRevision = 0;
    bool m_closing = false;
    int m_step = 0;
    bool m_connectionReady = false;
    State m_state = State::Disconnected;
    QString m_firstError;
    QString m_validatedXml;
    QString m_xmlError;
    bool m_draining = false;
    bool m_publishing = false;
};
