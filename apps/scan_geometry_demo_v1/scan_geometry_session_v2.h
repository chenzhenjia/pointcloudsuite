#pragma once
#include <selected_plane_tasks_v2.h>
#include "scan_geometry_contract_v1.h"
#include "result_regions_v1.h"
#include <pcv/roi/automatic_workpiece_frame_v1.h>
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QSet>
#include <QStringList>
#include <QJsonObject>
#include <deque>
#include <utility>

namespace pcv::demo {
using InputFrameV2 = pcv::ui::SelectedPlaneInputV2;
struct InputConfigV2 {
    ScanGeometryContractV1 contract;
    QVector<InputFrameV2> frames;
    QString calibration, outputRoot;
    float tolerance = 0.4f, maximumGap = 3.0f;
    float searchDistance = 10.0f, searchAngle = 5.0f;
    int maximumSeeds = 16;
    float highSideMinimumHeightMm = 2.0f;
    int highSideMinimumPointCount = 100;
    bool poseOnly = false, threePoint = false;
};
enum class SourceVerificationV3 { Unverified, Checking, Verified, Failed };
enum class ResultRegionStateV1 { Empty, Building, Ready, Failed, Cancelled };
struct ResultRegionSnapshotV1 {
    pcv::planefitting::PlaneMaskMeshV1 workpiece, interference;
    ResultRegionBasisV1 basis;
    ResultRegionStateV1 state = ResultRegionStateV1::Empty;
    QString error;
    quint64 resultRevision = 0, trimRevision = 0, annotationRevision = 0;
};
struct SessionFrameV2 {
    SourceVerificationV3 sourceVerification = SourceVerificationV3::Unverified;
    quint64 id = 0, loadAttempt = 0;
    bool indexQueued = false, indexPreparing = false;
    QElapsedTimer requestedAt, indexQueuedAt;
    QPair<float,float> heightRange{0.0f,1.0f};
    bool hasHeightRange = false;
    QJsonObject loadTiming;
    std::shared_ptr<const pcv::camera::CameraFrame> rawDepthFrame;
    pointcloud::RobotBaseFrame cloud;
    QVector<pointcloud::Point3D> display;
    QVector<int> displayToFull;
    pcv::planefitting::PlaneSelectionIndexPtr index;
    std::shared_ptr<pcv::ProcessingBudget::Lease> displayLease;
    QByteArray digest;
    pcv::stitching::SelectedPlaneFrame candidate;
    QVector<int> planeIndices, interferenceIndices, pendingInterference;
    pcv::planefitting::PlaneMaskMeshV1 workpieceMask, interferenceMask, pendingInterferenceMask;
    QString workpieceMaskError, interferenceMaskError, pendingInterferenceMaskError;
    QVector<pcv::planefitting::HighSidePlaneCandidateV1> highSideCandidates;
    QSet<int> dismissedHighSideSeeds;
    QString highSideError;
    bool highSideChecking = false;
    bool hasCandidate = false, automatic = false, reviewed = false, confirmed = false;
    bool skipped = false, problem = false, loading = false;
    bool depthReady = false;
    QString status = QStringLiteral("未加载");
};

// All mutable state belongs to the GUI thread. Workers receive const snapshots.
class ScanGeometrySessionV2 final : public QObject {
    Q_OBJECT
public:
    explicit ScanGeometrySessionV2(QObject *parent = nullptr);
    ~ScanGeometrySessionV2() override;
    const InputConfigV2 &config() const { return m_config; }
    const QVector<SessionFrameV2> &frames() const { return m_frames; }
    const SessionFrameV2 *activeFrame() const;
    int active() const { return m_active; }
    int round() const { return m_round; }
    int reference() const { return m_reference; }
    int runningCount() const { return m_jobs.size(); }
    bool busy() const { return !m_jobs.isEmpty() || !m_pending.empty() || !m_loadQueue.empty(); }
    bool interactionBlocked() const;
    bool activeFrameBusy() const;
    bool canSelectActive() const;
    QString timingSummary() const;
    QString recentLog() const { return m_recentLog.join('\n'); }
    void recordOperation(const QString &operation);
    QString timingLogDirectory() const { return m_logDirectory; }
    void observeCanvasTiming(quint64 frameId, quint64 attempt, const QJsonObject &timing);
    void observeFirstFrame(quint64 frameId, quint64 attempt);
    QPair<float,float> resultHeightRange() const { return m_resultHeightRange; }
    bool matching() const { return m_matching; }
    bool paused() const { return m_paused; }
    QString status() const { return m_status; }
    QString summary() const;
    bool canReviewAll() const;
    bool canStitch() const;
    bool canConfirmGeometry() const;
    bool geometryConfirmed() const { return m_geometryConfirmed; }
    const pcv::roi::WorkpieceCoordinateSystem &wobj1() const { return m_wobj1; }
    QString savedGeometryPath() const { return m_savedGeometryPath; }
    bool hasResult() const { return m_hasResult; }
    const pcv::stitching::SelectedPlaneStitchResult &result() const { return m_result; }
    const QStringList &committedOutputs() const { return m_committedOutputs; }
    const QSet<int> &removed() const { return m_removed; }
    quint64 resultRevision() const { return m_resultRevision; }
    quint64 trimRevision() const { return m_trimRevision; }
    std::shared_ptr<pcv::ProcessingBudget> processingBudget() const { return m_budget; }
    const ResultRegionSnapshotV1 &resultRegions() const { return m_resultRegions; }
    void rebuildResultRegions();
    bool canUndo() const { return !m_undo.isEmpty(); }
    int progress() const { return m_progress; } // -1: no backend percentage
    bool configure(const InputConfigV2 &config, QString *error);
    void loadRound(int round);
    void processActive();
    void activate(int frame);
    void showResult();
    void retry();
    void cancel();
    void shutdown();
    void reset();
    void pick(int fullIndex, bool interference);
    void cancelSelection();
    const QVector<int> &pendingSeeds() const { return m_seeds; }
    void setThreePoint(bool enabled);
    bool confirmCandidate();
    bool confirmInterference();
    void clearInterference();
    bool selectHighSideCandidate(int seedIndex);
    bool ignoreHighSideCandidate(int seedIndex);
    void clearPlane();
    void matchRemaining();
    void resumeMatching();
    void skipActive();
    void confirmAll();
    void stitch();
    void erase(const QVector<int> &originalIndices,quint64 resultRevision);
    void undo();
    void restore();
    void confirmGeometry();
    bool generateWobj1();
    void saveGeometry();
signals:
    void changed(bool cloudChanged);
private:
    friend struct ScanGeometrySessionTestAccessV2;
    struct Task : pcv::ui::SelectedPlaneTaskV2 {
        Task() = default;
        Task(pcv::ui::SelectedPlaneTaskV2 value) : pcv::ui::SelectedPlaneTaskV2(std::move(value)) {}
        std::shared_ptr<ResultRegionsResultV1> regions;
        quint64 resultRevision = 0, trimRevision = 0, annotationRevision = 0;
    };
    void submit(int frame, Task::Kind kind, std::function<Task()> work);
    void finish(int frame, Task::Kind kind, quint64 revision, quint64 referenceRevision,
                const pcv::ui::CancellationTokenV2 &token, Task task);
    void pump();
    void dispatchLoad(int frame);
    void dispatchIndex(int frame);
    void acceptSourceVerification(int frame, quint64 revision, quint64 attempt,
        const pcv::ui::CancellationTokenV2 &token, const QByteArray &digest, double digestMs, double verifiedMs);
    void enqueueLoad(int frame, bool front=false);
    void writeTiming(QJsonObject event);
    void finishRoundTiming();
    void dispatchMatch(int frame);
    void dispatchHighSideSearch(int frame);
    void releaseOutsideRound();
    void invalidateResult();
    void invalidateGeometry();
    ResultRegionsRequestV1 resultRegionRequest() const;
    void acceptResultRegions(const Task &task);
    void invalidateReferenceDependents();
    void pauseAt(int frame, const QString &reason);
    bool conflicts(const QVector<int> &a, const QVector<int> &b) const;
    InputConfigV2 m_config;
    QVector<SessionFrameV2> m_frames;
    QThreadPool m_pool;
    QVector<QFutureWatcher<Task> *> m_jobs;
    std::deque<int> m_loadQueue;
    struct PendingTask { int frame; Task::Kind kind; std::function<Task()> work; QElapsedTimer queuedAt; };
    std::deque<PendingTask> m_pending;
    QHash<QFutureWatcher<Task> *,QPair<int,Task::Kind>> m_jobKinds;
    QString m_logDirectory, m_logPath, m_logError;
    QStringList m_recentLog;
    QElapsedTimer m_roundTimer;
    bool m_roundTimingOpen = false;
    bool m_shutdown = false;
    QJsonObject m_roundTiming;
    QPair<float,float> m_resultHeightRange{0.0f,1.0f};
    pcv::ui::CancellationTokenV2 m_token;
    std::shared_ptr<pcv::ProcessingBudget> m_budget;
    quint64 m_revision = 0, m_referenceRevision = 0, m_nextFrameId = 1, m_resultRevision = 0;
    int m_active = -1, m_round = 0, m_reference = -1, m_nextMatch = 0;
    bool m_matching = false, m_paused = false, m_hasResult = false, m_geometryConfirmed = false;
    pcv::roi::WorkpieceCoordinateSystem m_wobj1;
    QString m_savedGeometryPath;
    QString m_status = QStringLiteral("请配置 PLY/SDK深度帧、手眼 XML 和逐帧 Start/End 位姿");
    int m_progress = -1;
    QVector<int> m_seeds;
    bool m_seedInterference = false;
    pcv::stitching::SelectedPlaneStitchResult m_result;
    ResultRegionSnapshotV1 m_resultRegions;
    quint64 m_trimRevision = 0, m_annotationRevision = 0;
    QStringList m_committedOutputs;
    QSet<int> m_removed;
    QVector<QSet<int>> m_undo;
};
} // namespace pcv::demo
