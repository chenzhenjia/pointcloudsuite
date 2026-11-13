#pragma once
#include <selected_plane_tasks.h>
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
    ScanGeometryContractV1 contract; // 可选SR-2026导出；runtimeRoot为空时关闭。
    QVector<InputFrameV2> frames; // 有序输入>=2；XYZ经50转换到robot_base/mm。
    QString calibration, outputRoot; // 共用XML（可逐帧覆盖）；源码树外绝对输出根目录。
    float tolerance = 0.4f, maximumGap = 3.0f; // mm；tolerance>0，gap在[0,50]。
    float searchDistance = 10.0f, searchAngle = 5.0f; // mm/deg；距离>0，角度(0,90]。
    int maximumSeeds = 16; // [1,64]。
    float highSideMinimumHeightMm = 2.0f; // >=0；只生成提醒，不自动确认干涉。
    int highSideMinimumPointCount = 100; // >=1。
    bool poseOnly = false, threePoint = false; // 仅位姿拼接；三点选面（默认单击）。
};
enum class SourceVerificationV3 { Unverified, Checking, Verified, Failed };
enum class ResultRegionStateV1 { Empty, Building, Ready, Failed, Cancelled };
// GUI线程借用的区域视图；几何为robot_base/mm，版本三元组对应生成时的输入。
struct ResultRegionSnapshotV1 {
    pcv::planefitting::PlaneMaskMeshV1 workpiece, interference;
    ResultRegionBasisV1 basis;
    ResultRegionStateV1 state = ResultRegionStateV1::Empty;
    QString error; // state可能Failed/Cancelled；部分已有有效区域可保留。
    quint64 resultRevision = 0, trimRevision = 0, annotationRevision = 0;
};
// 会话内部可变帧状态，经frames()/activeFrame()只读借用；不是持久快照。
struct SessionFrameV2 {
    SourceVerificationV3 sourceVerification = SourceVerificationV3::Unverified;
    quint64 id = 0, loadAttempt = 0; // 会话分配稳定ID；每次入加载队列递增attempt。
    bool indexQueued = false, indexPreparing = false;
    QElapsedTimer requestedAt, indexQueuedAt;
    QPair<float,float> heightRange{0.0f,1.0f}; // robot_base完整点Z范围，hasHeightRange控制有效性。
    bool hasHeightRange = false;
    QJsonObject loadTiming; // 诊断耗时，不进入正式输出契约。
    std::shared_ptr<const pcv::camera::CameraFrame> rawDepthFrame; // SDK原始深度预览来源，可先于XYZ就绪。
    pointcloud::RobotBaseFrame cloud; // 当前完整XYZ及来源映射；确认后可能释放。
    QVector<pointcloud::Point3D> display; // <=100万确定性实测展示点，不作正式计算输入。
    QVector<int> displayToFull; // display下标 -> cloud.fullPoints下标。
    pcv::planefitting::PlaneSelectionIndexPtr index; // 与当前完整点数据配套。
    std::shared_ptr<pcv::ProcessingBudget::Lease> displayLease; // 内部预算生命周期，不可由调用方修改。
    QByteArray digest; // PLY为完整转换数据摘要；深度为原始C16/强度摘要，用于来源核验。
    pcv::stitching::SelectedPlaneFrame candidate; // 独立完整选面快照，不等于display子集。
    QVector<int> planeIndices, interferenceIndices, pendingInterference; // 当前完整帧下标；确认/待确认干涉分开。
    pcv::planefitting::PlaneMaskMeshV1 workpieceMask, interferenceMask, pendingInterferenceMask;
    QString workpieceMaskError, interferenceMaskError, pendingInterferenceMaskError;
    QVector<pcv::planefitting::HighSidePlaneCandidateV1> highSideCandidates;
    QSet<int> dismissedHighSideSeeds;
    QString highSideError;
    bool highSideChecking = false;
    bool hasCandidate = false, automatic = false, reviewed = false, confirmed = false; // 自动候选仍须人工审核。
    bool skipped = false, problem = false, loading = false;
    bool depthReady = false; // 原始深度预览就绪，不代表XYZ/索引/来源校验完成。
    QString status = QStringLiteral("未加载");
};

// All mutable state belongs to the GUI thread. Workers receive const snapshots.
class ScanGeometrySessionV2 final : public QObject {
    Q_OBJECT
public:
    explicit ScanGeometrySessionV2(QObject *parent = nullptr);
    ~ScanGeometrySessionV2() override;
    // 1. 配置与输入：帧/轮次参数均为0-based序号，不能传SessionFrameV2::id。
    const InputConfigV2 &config() const { return m_config; }
    // 同步校验；成功替换会话但不加载。false时可写error；成功不保证清空旧error。
    bool configure(const InputConfigV2 &config, QString *error);
    // 异步加载当前六帧窗口；忙/匹配中时忽略；轮次越界时夹取到有效范围。
    void loadRound(int round);
    // SDK深度预览物化为完整XYZ并建索引；无可处理深度源时忽略。
    void processActive();
    // 切换当前轮内帧，必要时异步加载；阻塞/自动匹配中或越界时忽略。
    void activate(int frame);

    // 2. 状态查询：只能在所属GUI线程读取；引用/指针不能跨状态变更保存。
    const QVector<SessionFrameV2> &frames() const { return m_frames; }
    const SessionFrameV2 *activeFrame() const; // 结果视图/无活动帧时为nullptr。
    int active() const { return m_active; } // -1表示无活动帧（可能为结果视图）。
    int round() const { return m_round; }
    int reference() const { return m_reference; } // -1表示尚无参考帧。
    int runningCount() const { return m_jobs.size(); } // 不含排队项。
    bool busy() const { return !m_jobs.isEmpty() || !m_pending.empty() || !m_loadQueue.empty(); }
    bool interactionBlocked() const; // 后台读取/索引不一定阻塞全局交互。
    bool activeFrameBusy() const;
    bool canSelectActive() const; // 需完整XYZ、索引、来源Verified且未跳过。
    bool matching() const { return m_matching; }
    bool paused() const { return m_paused; }
    QString status() const { return m_status; } // 显示文本，不作为错误码解析。
    QString summary() const;
    bool canReviewAll() const; // 至少两帧已审核、无冲突且来源已验证。
    bool canStitch() const; // canReviewAll且所有未跳过帧confirmed。
    bool canConfirmGeometry() const; // 保存前几何条件；不保证IO/契约校验成功。
    bool geometryConfirmed() const { return m_geometryConfirmed; } // 当前几何保存已成功发布。
    int progress() const { return m_progress; } // -1未知，否则0..100；不是完成事件。

    // 3. 选面与审核：bool表示本次同步接受/确认，不代表后续worker成功。
    void pick(int fullIndex, bool interference); // 完整当前帧下标；单击或收齐三点后异步拟合。
    void cancelSelection(); // 清控制点并继续调度，不取消已运行worker。
    const QVector<int> &pendingSeeds() const { return m_seeds; }
    void setThreePoint(bool enabled); // 当前帧忙时忽略；接受时清控制点。
    bool confirmCandidate(); // 首个未跳过帧先成为参考；深度帧确认后可释放XYZ缓存。
    bool confirmInterference(); // 提交待确认干涉；与工件面重叠则false。
    void clearInterference(); // 清已确认/待确认干涉，失效几何并可异步重建区域。
    bool selectHighSideCandidate(int seedIndex); // 完整帧seed；true仅表示异步拟合已提交。
    bool ignoreHighSideCandidate(int seedIndex); // 同步移除提醒，不等于确认干涉。
    void clearPlane(); // 清工件候选；参考变化使依赖候选和结果失效。
    void matchRemaining(); // 已确认参考面后异步匹配，异常暂停等待人工。
    void resumeMatching(); // 当前异常帧须已审核或跳过，且无任务/控制点。
    void skipActive(); // 显式跳过并使已有结果失效。
    void confirmAll(); // canReviewAll时批量确认；不自动拼接。

    // 4. 结果处理：只读借用，使用前检查hasResult及对应revision。
    void showResult(); // 空闲且有结果时激活结果视图（active=-1）。
    bool hasResult() const { return m_hasResult; }
    const pcv::stitching::SelectedPlaneStitchResult &result() const { return m_result; }
    QPair<float,float> resultHeightRange() const { return m_resultHeightRange; } // 显示辅助Z范围，默认(0,1)，非业务极值。
    const QSet<int> &removed() const { return m_removed; } // 原始完整拼接结果下标。
    quint64 resultRevision() const { return m_resultRevision; }
    quint64 trimRevision() const { return m_trimRevision; }
    const ResultRegionSnapshotV1 &resultRegions() const { return m_resultRegions; }
    bool canUndo() const { return !m_undo.isEmpty(); } // 仅有撤销记录，执行仍需空闲结果视图。
    const pcv::roi::WorkpieceCoordinateSystem &wobj1() const { return m_wobj1; }
    void stitch(); // canStitch时异步调用30并提交完整产物，再计算展示区域。
    void rebuildResultRegions(); // 有结果且空闲/未关闭时异步重建，不改磁盘点云。
    void erase(const QVector<int> &originalIndices,quint64 resultRevision); // 过期revision拒绝；越界项忽略。
    void undo(); // 撤销一轮修剪并异步重建区域。
    void restore(); // 恢复全部点，可再撤销；异步重建区域。
    bool generateWobj1(); // 同步从完整参考快照生成；true不表示已保存。

    // 5. 保存与生命周期：saveGeometry是唯一会话保存入口。
    QString savedGeometryPath() const { return m_savedGeometryPath; } // 已发布sidecar绝对路径。
    const QStringList &committedOutputs() const { return m_committedOutputs; } // 产物入口，非所有文件清单。
    void saveGeometry(); // 几何条件满足时异步保存；必要时先同步生成WObj1。
    void retry(); // 当前帧重试校验/索引或重读来源；不会重试最终保存。
    void cancel(); // 非阻塞请求；busy归false前仍需事件循环收尾。
    void reset(); // 仅空闲时清会话；不删除已提交文件。
    void shutdown(); // 幂等cancel/join，阻塞等待；终止后不得复用此对象。

    // 6. 诊断：会写源码树外JSONL，不属于正式业务产物/schema。
    QString timingSummary() const;
    QString recentLog() const { return m_recentLog.join('\n'); }
    void recordOperation(const QString &operation);
    QString timingLogDirectory() const { return m_logDirectory; }
    void observeCanvasTiming(quint64 frameId, quint64 attempt, const QJsonObject &timing);
    void observeFirstFrame(quint64 frameId, quint64 attempt);
signals:
    // true请求刷新点云/选择/区域；false刷新状态。不是每次调用都发，也不是成功事件。
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
