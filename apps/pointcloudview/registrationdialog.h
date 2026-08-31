#pragma once

#include <pcv/core/point_types.h>

#include <QDialog>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

class QCheckBox;
class QCloseEvent;
class QDoubleSpinBox;
template <typename T> class QFutureWatcher;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace pcv::interface { struct StitchingResult; }

class RegistrationDialog final : public QDialog {
public:
    explicit RegistrationDialog(QWidget *parent = nullptr);
    ~RegistrationDialog() override;

    bool hasSuccessfulResult() const;
    QString outputPly() const;
    QVector<pointcloud::Point3D> takePoints();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void addPlyPaths(const QStringList &paths);
    void browseCalibration();
    void browseOutputDirectory();
    void addFiles();
    void addFolder();
    void moveSelectedRow(int offset);
    void removeSelectedRows();
    void startRegistration();
    void cancelRegistration();
    void registrationFinished();
    void setBusy(bool busy);
    void appendLog(const QString &message);

    QLineEdit *m_calibrationPath = nullptr;
    QLineEdit *m_outputDirectory = nullptr;
    QTableWidget *m_frames = nullptr;
    QSpinBox *m_sampleStride = nullptr;
    QCheckBox *m_seamEnabled = nullptr;
    QDoubleSpinBox *m_seamHalfWidth = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_cancel = nullptr;
    QFutureWatcher<pcv::interface::StitchingResult> *m_watcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    QVector<pointcloud::Point3D> m_points;
    QString m_outputPly;
    bool m_success = false;
    bool m_closing = false;
};
