#pragma once
#include <pcv/stitching/selected_plane_stitch.h>
#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QDialog>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

class QLineEdit;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class PointCloudCanvas;
template<class T> class QFutureWatcher;

struct SelectedPlaneTask {
    enum Kind { Load, Fit, Stitch } kind = Load;
    pointcloud::RobotBaseFrame loaded;
    pcv::stitching::SelectedPlaneFrame selected;
    pcv::stitching::SelectedPlaneStitchResult stitched;
    QVector<int> planeIndices;
    pcv::planefitting::PlaneSelectionIndexPtr index;
    double preparationMs = 0;
    QString error;
    bool ok = false;
    bool cancelled = false;
};

// Selection state is scoped to this dialog; it cannot trigger temp-workpiece
// WObj/ROI/scan output or change the existing full-cloud registration defaults.
class SelectedPlaneDialog final : public QDialog {
public:
    explicit SelectedPlaneDialog(float surfaceToleranceMm, QWidget *parent=nullptr);
    ~SelectedPlaneDialog() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend struct SelectedPlaneDialogTestAccess;
    void addFiles();
    void moveFrame(int offset);
    void removeFrame();
    void invalidateLoaded();
    void invalidateSelection(int row);
    void loadFrame();
    void picked(int index);
    void discardPreview();
    void confirmPreview();
    void updateActions();
    void stitch();
    void finishTask();
    void cancelAndWait();
    void busy(bool value);
    void log(const QString &text);
    QLineEdit *m_calibration=nullptr;
    QLineEdit *m_output=nullptr;
    QTableWidget *m_table=nullptr;
    PointCloudCanvas *m_canvas=nullptr;
    QPlainTextEdit *m_log=nullptr;
    QDoubleSpinBox *m_tolerance=nullptr;
    QDoubleSpinBox *m_maximumGap=nullptr;
    QCheckBox *m_poseOnly=nullptr;
    QComboBox *m_method=nullptr;
    QLabel *m_previewLabel=nullptr;
    QPushButton *m_confirm=nullptr;
    QPushButton *m_start=nullptr;
    QVector<QPushButton *> m_controls;
    QFutureWatcher<SelectedPlaneTask> *m_watcher=nullptr;
    std::shared_ptr<std::atomic_bool> m_token;
    QVector<pcv::stitching::SelectedPlaneFrame> m_selections;
    QVector<QVector<int>> m_confirmedIndices;
    pcv::stitching::SelectedPlaneFrame m_candidate;
    QVector<int> m_previewIndices;
    pcv::planefitting::PlaneSelectionIndexPtr m_index;
    pointcloud::RobotBaseFrame m_loaded;
    QVector<int> m_seeds;
    int m_loadedRow=-1;
    int m_taskRow=-1;
    quint64 m_revision=0;
    quint64 m_taskRevision=0;
    bool m_busy=false;
    bool m_closing=false;
    bool m_hasCandidate=false;
    bool m_previewRenderedPending=false;
    QElapsedTimer m_previewTimer;
    QElapsedTimer m_loadTimer;
};
