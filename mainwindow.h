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
    void downsample();
    void loadFinished();
    void updateRenderSettings();
    void setDownsampleRatio(int denominator);
    void applyNoiseRemoval();
    void noiseFinished();

private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void refreshDisplayCloud();
    QListWidget *m_fileList = nullptr;
    QLabel *m_fileInfo = nullptr;
    QLabel *m_canvasInfo = nullptr;
    QProgressBar *m_progress = nullptr;
    QComboBox *m_ratio = nullptr;
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
    QCheckBox *m_radiusNoise = nullptr;
    QDoubleSpinBox *m_radius = nullptr;
    QSpinBox *m_minNeighbors = nullptr;
    QPushButton *m_noiseApply = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_filteredPoints;
    QVector<pointcloud::Point3D> m_points;
    bool m_loading = false;
    QString m_pendingPath;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<pointcloud::NoiseResult> *m_noiseWatcher = nullptr;
    int m_downsampleDenominator = 1;
    qsizetype m_noiseInputCount = 0;
    QList<QAction *> m_ratioActions;
};
