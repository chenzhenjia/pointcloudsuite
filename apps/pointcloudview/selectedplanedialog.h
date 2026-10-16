#pragma once
#include <pcv/stitching/selected_plane_stitch.h>
#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <pcv/workpiece/workpiece_template_v1.h>
#include <QDialog>
#include <QElapsedTimer>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

class QLineEdit;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QGridLayout;
class QStackedWidget;
class PointCloudCanvas;
template<class T> class QFutureWatcher;

struct SelectedPlaneTask {
    enum Kind { LoadDisplay, PrepareIndex, Fit, Stitch, SaveTemplate } kind = LoadDisplay;
    pointcloud::RobotBaseFrame loaded;
    QVector<pointcloud::Point3D> displayPoints;
    QVector<int> displayToFullIndices;
    pcv::stitching::SelectedPlaneFrame selected;
    pcv::stitching::SelectedPlaneStitchResult stitched;
    pcv::workpiece::WorkpieceTemplateResult savedTemplate;
    pcv::workpiece::WorkpieceTemplateInfo templateSummary;
    QVector<int> planeIndices;
    pcv::planefitting::PlaneSelectionIndexPtr index;
    double preparationMs = 0, gridMs = 0, normalsMs = 0;
    qint64 plyReadMs = 0, fingerprintMs = 0, cacheReadMs = 0, cacheWriteMs = 0;
    qint64 transformMs = 0, displaySamplingMs = 0;
    qint64 plyParseMs = 0;
    double transformMergeMs = 0, snapshotMs = 0, projectionMs = 0, normalConnectivityMs = 0, gapMs = 0;
    int transformWorkers = 1;
    qint64 workerMs = 0;
    QElapsedTimer startedAt, completedAt;
    size_t optimizationBytes = 0;
    QString error;
    std::shared_ptr<pcv::ProcessingBudget::Lease> displayLease;
    bool ok = false, cancelled = false, outOfMemory = false, usedCache = false;
};

// All frame identity/revision and QWidget state belongs to the GUI thread.
// Workers capture immutable snapshots and only return private task results.
class SelectedPlaneDialog final : public QDialog {
public:
    explicit SelectedPlaneDialog(float surfaceToleranceMm, QWidget *parent=nullptr);
    ~SelectedPlaneDialog() override;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    friend struct SelectedPlaneDialogTestAccess;
    struct Frame {
        quint64 id = 0, revision = 0;
        pointcloud::RobotBaseFrame loaded;
        pcv::planefitting::PlaneSelectionIndexPtr index;
        pcv::stitching::SelectedPlaneFrame confirmed, candidate;
        QVector<int> confirmedIndices, candidateIndices, seeds;
        std::shared_ptr<pcv::ProcessingBudget::Lease> displayLease;
        bool processed = false, committedSkip = false, skip = false, dirty = true;
        bool hasCandidate = false, loadQueued = false, indexQueued = false, fitQueued = false;
        bool running = false, displayPending = false, highlightPending = false;
        QString status;
        QElapsedTimer loadTimer, clickTimer, queueTimer;
    };
    struct Panel {
        QWidget *widget = nullptr;
        PointCloudCanvas *canvas = nullptr;
        QLabel *label = nullptr;
        QPushButton *skip = nullptr, *retry = nullptr, *discard = nullptr, *cancel = nullptr;
    };
    struct Job {
        QFutureWatcher<SelectedPlaneTask> *watcher = nullptr;
        std::shared_ptr<Frame> frame;
        std::shared_ptr<std::atomic_bool> token;
        quint64 frameId = 0, revision = 0;
        qint64 queueMs = 0;
        QElapsedTimer queuedAt;
        int workers = 1;
        SelectedPlaneTask::Kind kind = SelectedPlaneTask::LoadDisplay;
    };
    using FramePtr = std::shared_ptr<Frame>;
    FramePtr frameAtPanel(int panel) const;
    int rowOf(const FramePtr &frame) const;
    int panelOf(const FramePtr &frame) const;
    int jobSlotOfFrame(const FramePtr &frame) const;
    int activeJobCount() const;
    QVector<int> roundRows(int round) const;
    void releaseFrameData(const FramePtr &frame);
    void publishDisplay(int panel, const FramePtr &frame, const SelectedPlaneTask &task);
    void requestFit(const FramePtr &frame);
    void refreshPanelVisibility();
    QJsonObject buildResultSource() const;
    void addFiles();
    void appendFiles(const QStringList &files);
    void moveFrame(int offset);
    void removeFrame();
    void loadRound(int round);
    void releaseRound();
    void retryFrame(int panel);
    void picked(int panel, int fullIndex);
    void discardPreview(int panel);
    void toggleSkip(int panel);
    void cancelFrame(int panel);
    void confirmRound();
    bool roundReady() const;
    bool stitchReady() const;
    void parametersChanged();
    void invalidateFrame(const FramePtr &frame);
    void invalidateResult();
    void enlarge(int panel);
    void activatePanel(int panel);
    FramePtr nextQueuedFrame(SelectedPlaneTask::Kind *kind) const;
    void schedule();
    void startJob(Job &job, const FramePtr &frame, SelectedPlaneTask::Kind kind, int workers);
    void finishJob(int slot);
    void finishGlobal();
    void cancelAndWait();
    void updateActions();
    void refreshPanel(int panel);
    void stitch();
    void saveTemplate();
    void log(const QString &text);
    static bool prepareDisplayCloud(SelectedPlaneTask *task, const std::function<bool()> &cancelled);
    QLineEdit *m_calibration=nullptr, *m_output=nullptr;
    QTableWidget *m_table=nullptr;
    QPlainTextEdit *m_log=nullptr;
    QDoubleSpinBox *m_tolerance=nullptr, *m_maximumGap=nullptr;
    QCheckBox *m_poseOnly=nullptr;
    QComboBox *m_method=nullptr;
    QLabel *m_roundLabel=nullptr;
    QPushButton *m_confirm=nullptr, *m_start=nullptr, *m_previous=nullptr, *m_saveTemplate=nullptr;
    QPushButton *m_loadRound=nullptr;
    QVector<QPushButton *> m_editControls;
    QStackedWidget *m_views=nullptr;
    QGridLayout *m_grid=nullptr;
    PointCloudCanvas *m_resultCanvas=nullptr;
    std::array<Panel,6> m_panels;
    std::array<Job,2> m_jobs;
    QVector<FramePtr> m_frames;
    std::shared_ptr<pcv::ProcessingBudget> m_budget;
    QFutureWatcher<SelectedPlaneTask> *m_globalWatcher=nullptr;
    std::shared_ptr<std::atomic_bool> m_globalToken;
    pcv::stitching::SelectedPlaneStitchResult m_result;
    pcv::workpiece::WorkpieceTemplateInfo m_templateSummary;
    QJsonObject m_resultSource;
    quint64 m_nextId=1, m_revision=0, m_globalRevision=0;
    int m_round=-1, m_activePanel=-1, m_enlarged=-1;
    bool m_closing=false, m_globalBusy=false, m_resultValid=false, m_memoryBlocked=false;
};
