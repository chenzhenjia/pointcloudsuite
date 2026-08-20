#pragma once

#include <QMainWindow>
#include <QElapsedTimer>
#include <QStringList>
#include "pointcloudprocessor.h"

class QListWidget;
class QLabel;
class QAction;
class QCloseEvent;
class QDialog;
class QProgressBar;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class PointCloudCanvas;
namespace Ui { class MainWindow; }
namespace pointcloud { struct LoadResult; }
template <typename T> class QFutureWatcher;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openPointCloud();
    void openPointCloudSource();
    void loadSelectedSource();
    void loadFinished();
    void updateRenderSettings();
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
    void detectObstacles();
    void obstacleDetectionFinished();
    void clearObstacleDetection();
    void applyPlaneEdgeSegmentation();
    void planeEdgeSegmentationFinished();
    void startEdgePointSelection();
    void clearEdgePointSelection();
    void handleCanvasEdgePointPicked(int index);
    void extractPlaneImage();
    void planeImageExtractionFinished();
    void savePlaneImage();
    void startSecondPlanePointSelection();
    void cancelSecondPlanePointSelection();

private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void publishCanvasCache(QVector<pointcloud::Point3D> points);
    void updatePlaneExtractionUi();
    void updateObstacleDetectionUi();
    void updatePlaneEdgeUi();
    void clearPlaneEdgeUi();
    void validateSecondPlaneSelection();
    void clearObstacleDetectionUi();
    void runPlaneExtraction(bool deferFinalClassification);
    void scheduleAutomaticObstacleDetection();
    bool pointTaskRunning() const;
    bool obstacleDetected() const;
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
    QDoubleSpinBox *m_obstacleHeight = nullptr;
    QDoubleSpinBox *m_obstacleGridSize = nullptr;
    QSpinBox *m_obstacleMinimumPointCount = nullptr;
    QDoubleSpinBox *m_obstacleMinimumArea = nullptr;
    QPushButton *m_obstacleDetectButton = nullptr;
    QPushButton *m_obstacleClearButton = nullptr;
    QLabel *m_obstacleStatus = nullptr;
    QPlainTextEdit *m_obstacleOutput = nullptr;
    QDoubleSpinBox *m_edgeGridSize = nullptr;
    QDoubleSpinBox *m_planeImageWidth = nullptr;
    QDoubleSpinBox *m_planeImageHeight = nullptr;
    QSpinBox *m_edgeCloseRadius = nullptr;
    QSpinBox *m_edgeOpenRadius = nullptr;
    QPushButton *m_edgeApplyButton = nullptr;
    QPushButton *m_selectEdgeButton = nullptr;
    QPushButton *m_clearEdgeSelectionButton = nullptr;
    QPushButton *m_extractPlaneImageButton = nullptr;
    QPushButton *m_savePlaneImageButton = nullptr;
    QLabel *m_planeImagePreview = nullptr;
    QPlainTextEdit *m_edgeOutput = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_points;
    bool m_loading = false;
    QElapsedTimer m_loadElapsedTimer;
    QString m_pendingPath;
    QStringList m_sourceFiles;
    QString m_sourceDirectory;
    bool m_folderScanOnly = false;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<pointcloud::NoiseResult> *m_noiseWatcher = nullptr;
    QFutureWatcher<pointcloud::ThreePointPlaneResult> *m_threePlaneWatcher = nullptr;
    QFutureWatcher<pointcloud::ObstacleDetectionResult> *m_obstacleWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneEdgeResult> *m_edgeWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneImageResult> *m_planeImageWatcher = nullptr;
    qsizetype m_noiseInputCount = 0;
    quint64 m_canvasRevision = 0;
    quint64 m_noiseInputRevision = 0;
    quint64 m_threePlaneInputRevision = 0;
    quint64 m_obstacleInputRevision = 0;
    quint64 m_edgeInputRevision = 0;
    quint64 m_planeImageInputRevision = 0;
    quint64 m_coordinateFrameRevision = 0;
    quint64 m_planeImageCoordinateRevision = 0;
    pointcloud::ThreePointPlaneResult m_threePlaneResult;
    pointcloud::WorkpieceCoordinateSystem m_workpieceCoordinate;
    pointcloud::ObstacleDetectionResult m_obstacleResult;
    pointcloud::PlaneEdgeResult m_planeEdgeResult;
    pointcloud::PlaneImageResult m_planeImageResult;
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
    int m_xAxisPointIndex = -1;
    int m_yAxisPointIndex = -1;
    bool m_planeCandidateConfirmed = false;
    bool m_planeFinalizationPending = false;
    bool m_closing = false;
    Ui::MainWindow *ui = nullptr;
};
