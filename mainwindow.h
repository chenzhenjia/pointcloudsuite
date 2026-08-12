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
    void applyPlaneSegmentation();
    void planeSegmentationFinished();

private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void publishCanvasCache(QVector<pointcloud::Point3D> points);
    void clearPlaneSegmentation();
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
    QDoubleSpinBox *m_planeDistanceThreshold = nullptr;
    QSpinBox *m_planeMaxCount = nullptr;
    QSpinBox *m_planeIterations = nullptr;
    QSpinBox *m_planeMinInliers = nullptr;
    QComboBox *m_planeSampleRatio = nullptr;
    QPushButton *m_planeApply = nullptr;
    QPlainTextEdit *m_planeOutput = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_points;
    bool m_loading = false;
    QString m_pendingPath;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<pointcloud::NoiseResult> *m_noiseWatcher = nullptr;
    QFutureWatcher<pointcloud::PlaneSegmentationResult> *m_planeWatcher = nullptr;
    qsizetype m_noiseInputCount = 0;
    quint64 m_canvasRevision = 0;
    quint64 m_noiseInputRevision = 0;
    quint64 m_planeInputRevision = 0;
    pointcloud::PlaneSegmentationResult m_planeResult;
};
