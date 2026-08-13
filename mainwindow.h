#pragma once

#include <QMainWindow>
#include "pointcloudprocessor.h"

class QListWidget;
class QLabel;
class QAction;
class QCloseEvent;
class QProgressBar;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class PointCloudCanvas;
namespace pointcloud { struct LoadResult; }
template <typename T> class QFutureWatcher;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openPointCloud();
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
    void planeExtractionFinished();
    void applyPlaneEdgeSegmentation();
    void planeEdgeSegmentationFinished();
    void savePlaneImage();

private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void publishCanvasCache(QVector<pointcloud::Point3D> points);
    void updatePlaneExtractionUi();
    void updatePlaneEdgeUi();
    void clearPlaneEdgeUi();
    bool pointTaskRunning() const;
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
    QPushButton *m_determinePlaneButton = nullptr;
    QPushButton *m_confirmCandidateButton = nullptr;
    QPushButton *m_cancelCandidateButton = nullptr;
    QPlainTextEdit *m_threeOutput = nullptr;
    QDoubleSpinBox *m_edgeGridSize = nullptr;
    QSpinBox *m_edgeCloseRadius = nullptr;
    QSpinBox *m_edgeOpenRadius = nullptr;
    QPushButton *m_edgeApplyButton = nullptr;
    QPushButton *m_savePlaneImageButton = nullptr;
    QLabel *m_planeImagePreview = nullptr;
    QPlainTextEdit *m_edgeOutput = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_points;
    bool m_loading = false;
    QString m_pendingPath;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<pointcloud::NoiseResult> *m_noiseWatcher = nullptr;
    QFutureWatcher<pointcloud::ThreePointPlaneResult> *m_threePlaneWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneEdgeResult> *m_edgeWatcher = nullptr;
    qsizetype m_noiseInputCount = 0;
    quint64 m_canvasRevision = 0;
    quint64 m_noiseInputRevision = 0;
    quint64 m_threePlaneInputRevision = 0;
    quint64 m_edgeInputRevision = 0;
    pointcloud::ThreePointPlaneResult m_threePlaneResult;
    pointcloud::PlaneEdgeResult m_planeEdgeResult;
    QVector<int> m_selectedPointIndices;
    bool m_threePointSelectionActive = false;
    bool m_planeCandidateConfirmed = false;
};
