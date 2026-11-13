#pragma once
#include <pcv/camera/frame_converter.h>
#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QWidget>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QImage>
#include <atomic>

class QLabel;
class QPushButton;
class QTimer;
namespace pcv::render { class DepthImageCanvasV1; }

class CameraDepthPreviewV1 final : public QWidget {
public:
    explicit CameraDepthPreviewV1(QWidget *parent = nullptr);
    ~CameraDepthPreviewV1() override;
    void setFrame(std::shared_ptr<const pcv::camera::CameraFrame> frame, const QString &directory = {});
    void cancelAndWait();
    std::function<void(const QVector<pointcloud::Point3D> &)> publishCloud;
    std::function<bool()> canPublish;
private:
    friend struct CameraDepthPreviewTestAccess;
    struct Prepared {
        QVector<pointcloud::Point3D> points;
        QVector<qsizetype> indices;
        pcv::planefitting::PlaneSelectionIndexPtr index;
        std::shared_ptr<pcv::ProcessingBudget> budget;
    };
    struct WorkResult {
        std::shared_ptr<const Prepared> prepared;
        pcv::planefitting::SingleClickResult fit;
        QImage mask;
        QVector<pointcloud::Point3D> output;
        QString error;
        double conversionMs = 0, indexMs = 0, selectMs = 0;
    };
    void toggleSelection();
    void hover(QPoint pixel);
    void schedule();
    void convert(bool selected, bool exportPly);
    void saveSelection();
    void saveSelectionTo(const QString &parent);
    void updateActions();
    void run(bool prepareIndex, bool selected, bool exportPly, bool selectionJob);
    void releasePrepared();
    pcv::render::DepthImageCanvasV1 *m_canvas;
    QLabel *m_status;
    QPushButton *m_select, *m_confirm, *m_save, *m_full, *m_plane, *m_exportFull, *m_exportPlane;
    QTimer *m_debounce;
    QFutureWatcher<WorkResult> *m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_token;
    std::shared_ptr<const pcv::camera::CameraFrame> m_frame, m_latest;
    std::shared_ptr<const Prepared> m_prepared;
    QString m_directory, m_latestDirectory;
    QPoint m_hover{-1,-1};
    QImage m_candidateMask, m_confirmedMask;
    pcv::planefitting::PlaneModel m_candidateModel, m_confirmedModel;
    quint64 m_revision = 0, m_hoverRevision = 0;
    bool m_selecting = false, m_locked = false, m_closing = false;
    QElapsedTimer m_hoverClock;
    double m_conversionMs = 0, m_indexMs = 0, m_selectionMs = 0;
    pcv::planefitting::SingleClickResult m_lastDiagnostics;
};
