#pragma once

#include <pcv/core/cancellation.h>
#include <pcv/infrastructure/application_config.h>
#include <pcv/scan/scan_planner.h>
#include <pcv/texture/texture_mapping.h>

#include <QMainWindow>
#include <QElapsedTimer>
#include <QStringList>
#include "pointcloudprocessor.h"

class QListWidget;
class QLabel;
class QAction;
class QCloseEvent;
class QDialog;
class QHBoxLayout;
class QProgressBar;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class QToolButton;
class QWidget;
class RenderDialog;
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
class CameraDialog;
#endif
class PointCloudCanvas;
namespace Ui { class MainWindow; }
namespace pointcloud { struct LoadResult; }
namespace pcv::interface {
struct TempWorkpieceOptions;
struct TempWorkpiecePreparation;
struct TempWorkpieceResult;
}
template <typename T> class QFutureWatcher;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    MainWindow(const pcv::config::PointCloudViewDefaults &defaults,
               QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    void openCameraStream();
#endif
    void openPointCloud();
    void openTextureExperimentPly();
    void openPointCloudSource();
    void openTempScanningInfo();
    void openMultiFrameRegistration();
    void loadSelectedSource();
    void loadFinished();
    void tempWorkpieceFinished();
    void tempWorkpieceFinalizeFinished();
    void updateRenderSettings();
    void openRenderSettings();
    void applyNoiseRemoval();
    void noiseFinished();
    void startPlanePointSelection();
    void abandonPlanePointSelection();
    void undoPlanePointSelection();
    void determinePlaneCandidate();
    void confirmPlaneCandidate();
    void cancelPlaneCandidate();
    void handleCanvasPointPicked(int index);
    void startWorkpieceAxisSelection();
    void clearWorkpieceAxisSelection();
    void planeExtractionFinished();
    void applyPlaneEdgeSegmentation();
    void planeEdgeSegmentationFinished();
    void startEdgePointSelection();
    void clearEdgePointSelection();
    void handleCanvasEdgePointPicked(int index);
    void extractPlaneImage();
    void planeImageExtractionFinished();
    void selectTextureImage();
    void textureMappingFinished();
    void savePlaneImage();
    void planScanTrajectory();
    void scanPlanningFinished();
    void exportScanTrajectory();
    void exportInoRobotTrajectory();
    void startSecondPlanePointSelection();
    void cancelSecondPlanePointSelection();

private:
    // Behavioural state of the single temporary-workpiece workflow. Both data
    // sources (scanning JSON and plain PLY) share it; it gates new data-source
    // opens and keeps cancelled or stale sessions from publishing results.
    // The preparation worker performs ReadingPly and Transforming in one
    // phase, so the observable states are ReadingPly (worker running) and
    // ValidatingPrimaryPlane / FinalizingArtifacts for the later stages.
    enum class WorkflowState {
        Idle,
        ReadingPly,
        Transforming,
        SelectingPrimaryPlanePoints,
        ValidatingPrimaryPlane,
        SelectingSecondaryPlanePoints,
        ValidatingSecondaryPlane,
        FinalizingArtifacts,
        Completed,
        Failed,
        Cancelled
    };
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    bool workflowBlocksNewSource() const;
    QString workflowStateName() const;
    void setWorkflowState(WorkflowState state, const QString &statusText = QString());
    void startPlyLinePreparation(const pcv::interface::TempWorkpieceOptions &options);
    void reportPlyLinePreparation(const pcv::interface::TempWorkpiecePreparation &prepared);
    enum class TempWorkpieceSource { None, Json, Ply };
    bool hasTempWorkpieceSession() const;
    bool confirmReplaceTempWorkpieceSession();
    void invalidateTempWorkpieceSession();
    void failAutomaticTempWorkpieceStage(const QString &stage, const QString &detail);
    void appendTempWorkpieceOutput(const QString &text);
    void applyDepthRenderRange(const QVector<pointcloud::Point3D> &points);
    void publishCanvasCache(QVector<pointcloud::Point3D> points,
                            bool preserveTempWorkpieceSession = false);
    bool textureRasterMappingReady(QString *error = nullptr) const;
    void finalizeTempWorkpiece();
    void resetSecondPlaneVerification();
    void updatePlaneExtractionUi();
    void updatePlaneEdgeUi();
    void invalidateTextureOverlay();
    void invalidateTextureExperiment();
    void clearPlaneEdgeUi();
    void invalidateScanTrajectory(const QString &reason = QString());
    bool planeImageSaveReady(QString *error = nullptr) const;
    bool tempWorkpieceFinalizeReady(QString *error = nullptr) const;
    void validateSecondPlaneSelection();
    void runPlaneExtraction(bool deferFinalClassification, bool fromThirdPick = false);
    bool pointTaskRunning() const;
    void applyConfiguredDefaults();
    void applyRenderDialogSettings();
    void refreshRenderDialogContent();
    void showSourceDialog();
    void showCleanDialog();
    void showPlaneDialog();
    void showEdgeDialog();
    void showScanDialog();
    void setPickingOperation(const QString &text, bool visible);
    void undoPickingOperation();
    void cancelPickingOperation();
    void confirmPickingOperation();
    void finishEdgePointSelection();
    QListWidget *m_fileList = nullptr;
    QLabel *m_fileInfo = nullptr;
    QLabel *m_canvasInfo = nullptr;
    QProgressBar *m_progress = nullptr;
    QSpinBox *m_pointSize = nullptr;
    QComboBox *m_colorMode = nullptr;
    QDoubleSpinBox *m_overlay = nullptr;
    QDoubleSpinBox *m_mapMin = nullptr;
    QDoubleSpinBox *m_mapMax = nullptr;
    QCheckBox *m_voxelNoise = nullptr;
    QDoubleSpinBox *m_voxelSize = nullptr;
    QCheckBox *m_statisticalNoise = nullptr;
    QSpinBox *m_meanK = nullptr;
    QDoubleSpinBox *m_stddev = nullptr;
    QPushButton *m_noiseApply = nullptr;
    QPushButton *m_pickPointsButton = nullptr;
    QPushButton *m_abandonPointsButton = nullptr;
    QPushButton *m_undoPointButton = nullptr;
    QPushButton *m_pickSecondPlaneButton = nullptr;
    QPushButton *m_cancelSecondPlaneButton = nullptr;
    QPushButton *m_determinePlaneButton = nullptr;
    QPushButton *m_confirmCandidateButton = nullptr;
    QPushButton *m_cancelCandidateButton = nullptr;
    QPlainTextEdit *m_threeOutput = nullptr;
    QDoubleSpinBox *m_edgeGridSize = nullptr;
    QDoubleSpinBox *m_planeImageWidth = nullptr;
    QDoubleSpinBox *m_planeImageHeight = nullptr;
    QSpinBox *m_edgeCloseRadius = nullptr;
    QSpinBox *m_edgeOpenRadius = nullptr;
    QPushButton *m_edgeApplyButton = nullptr;
    QPushButton *m_selectEdgeButton = nullptr;
    QPushButton *m_clearEdgeSelectionButton = nullptr;
    QPushButton *m_extractPlaneImageButton = nullptr;
    QPushButton *m_selectTextureImageButton = nullptr;
    QPushButton *m_savePlaneImageButton = nullptr;
    QPushButton *m_finalizeTempWorkpieceButton = nullptr;
    QLabel *m_planeImagePreview = nullptr;
    QPlainTextEdit *m_edgeOutput = nullptr;
    QWidget *m_scanPlanningPage = nullptr;
    QComboBox *m_scanMode = nullptr;
    QDoubleSpinBox *m_scanHeight = nullptr;
    QDoubleSpinBox *m_scanCameraWidth = nullptr;
    QDoubleSpinBox *m_scanOverlap = nullptr;
    QLabel *m_scanCenter = nullptr;
    QLabel *m_scanRange = nullptr;
    QPushButton *m_planScanButton = nullptr;
    QPushButton *m_exportScanButton = nullptr;
    QPushButton *m_exportInoRobotButton = nullptr;
    QPlainTextEdit *m_scanOutput = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    RenderDialog *m_renderDialog = nullptr;
    QDialog *m_sourceDialog = nullptr;
    QDialog *m_cleanDialog = nullptr;
    QDialog *m_planeDialog = nullptr;
    QDialog *m_edgeDialog = nullptr;
    QDialog *m_scanDialog = nullptr;
    QWidget *m_operationBar = nullptr;
    QLabel *m_operationLabel = nullptr;
    QLabel *m_operationCount = nullptr;
    QPushButton *m_operationUndo = nullptr;
    QPushButton *m_operationCancel = nullptr;
    QPushButton *m_operationConfirm = nullptr;
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    CameraDialog *m_cameraDialog = nullptr;
#endif
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_points;
    qsizetype m_livePreviewPointCount = 0;
    bool m_loading = false;
    QString m_pendingPath;
    bool m_pendingTextureExperiment = false;
    bool m_textureExperimentActive = false;
    QString m_pendingSourcePlyEncoding = QStringLiteral("unknown");
    QStringList m_sourceFiles;
    QString m_sourceDirectory;
    bool m_folderScanOnly = false;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<pcv::interface::TempWorkpiecePreparation> *m_tempWorkpieceWatcher = nullptr;
    QFutureWatcher<pcv::interface::TempWorkpieceResult> *m_tempWorkpieceFinalizeWatcher = nullptr;
    QFutureWatcher<pointcloud::NoiseResult> *m_noiseWatcher = nullptr;
    QFutureWatcher<pointcloud::ThreePointPlaneResult> *m_threePlaneWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneEdgeResult> *m_edgeWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneImageResult> *m_planeImageWatcher = nullptr;
    QFutureWatcher<pcv::scan::ScanTrajectoryResult> *m_scanPlanningWatcher = nullptr;
    QFutureWatcher<pcv::texture::MappingResult> *m_textureMappingWatcher = nullptr;
    qsizetype m_noiseInputCount = 0;
    quint64 m_canvasRevision = 0;
    quint64 m_noiseInputRevision = 0;
    quint64 m_threePlaneInputRevision = 0;
    pointcloud::CancellationToken m_planeCancelToken;
    QElapsedTimer m_planeElapsed;
    bool m_planeFramePending = false;
    quint64 m_edgeInputRevision = 0;
    quint64 m_planeImageInputRevision = 0;
    quint64 m_coordinateFrameRevision = 0;
    quint64 m_planeImageCoordinateRevision = 0;
    quint64 m_tempWorkpieceSessionRevision = 0;
    quint64 m_roiRevision = 0;
    quint64 m_scanPlanningRequestRevision = 0;
    quint64 m_scanPlanningTaskRevision = 0;
    quint64 m_scanPlanningCanvasRevision = 0;
    quint64 m_scanPlanningCoordinateRevision = 0;
    quint64 m_scanPlanningPlaneRevision = 0;
    quint64 m_scanPlanningRoiRevision = 0;
    quint64 m_textureInputRevision = 0;
    pointcloud::ThreePointPlaneResult m_threePlaneResult;
    pointcloud::WorkpieceCoordinateSystem m_workpieceCoordinate;
    pointcloud::PlaneEdgeResult m_planeEdgeResult;
    pointcloud::PlaneImageResult m_planeImageResult;
    pcv::scan::ScanTrajectoryResult m_scanTrajectoryResult;
    pointcloud::CancellationToken m_textureCancelToken;
    QString m_textureImagePath;
    QString m_pendingTextureImagePath;
    QVector<int> m_selectedPointIndices;
    QVector<int> m_secondPlanePointIndices;
    QVector<int> m_selectedEdgeIndices;
    bool m_edgeSelectionActive = false;
    bool m_threePointSelectionActive = false;
    bool m_secondPlaneSelectionActive = false;
    bool m_secondPlaneValidated = false;
    bool m_secondPlaneSamePlane = false;
    float m_secondPlaneNormalAngle = 0.0f;
    float m_secondPlaneMaximumDistance = 0.0f;
    bool m_axisSelectionActive = false;
    QVector3D m_planeCenter;
    bool m_planeCenterValid = false;
    int m_xAxisPointIndex = -1;
    int m_yAxisPointIndex = -1;
    pointcloud::AutomaticAxisPointSelection m_automaticAxisPointSelection;
    bool m_planeCandidateConfirmed = false;
    bool m_planeFinalizationPending = false;
    QString m_tempWorkpieceScanId;
    QString m_tempWorkpieceOutputDirectory;
    QString m_tempWorkpieceCreatedAtIso8601;
    QVector<qsizetype> m_tempWorkpieceSourceIndices;
    qsizetype m_tempWorkpieceDeclaredPointCount = 0;
    qsizetype m_tempWorkpieceRejectedInvalidPointCount = 0;
    qsizetype m_tempWorkpieceRejectedRangePointCount = 0;
    bool m_tempWorkpieceRasterCompatible = false;
    bool m_closing = false;
    WorkflowState m_workflowState = WorkflowState::Idle;
    // The active source is the single source of truth for temporary-workpiece
    // session ownership. A pending source exists only while preparation runs.
    TempWorkpieceSource m_tempWorkpieceSource = TempWorkpieceSource::None;
    TempWorkpieceSource m_pendingTempWorkpieceSource = TempWorkpieceSource::None;
    // Cancellation callback handed to the long-running preparation worker.
    pointcloud::CancellationToken m_prepareCancelToken;
    pointcloud::CancellationToken m_scanPlanningCancelToken;
    pcv::config::PointCloudViewDefaults m_defaults;
    Ui::MainWindow *ui = nullptr;
};
