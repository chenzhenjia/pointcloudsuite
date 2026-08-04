#pragma once

#include <QMainWindow>
#include "pointcloudprocessor.h"

class QListWidget;
class QLabel;
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

private slots:
    void openPointCloud();
    void downsample();
    void loadFinished();
    void lodFinished();
    void updateRenderSettings();

private:
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
    QCheckBox *m_lodEnabled = nullptr;
    PointCloudCanvas *m_canvas = nullptr;
    QVector<pointcloud::Point3D> m_rawPoints;
    QVector<pointcloud::Point3D> m_points;
    bool m_loading = false;
    QString m_pendingPath;
    QFutureWatcher<pointcloud::LoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<QVector<pointcloud::Point3D>> *m_lodWatcher = nullptr;
    bool m_lodQueued = false;
};
