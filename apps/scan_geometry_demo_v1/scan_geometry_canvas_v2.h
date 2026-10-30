#pragma once

#include <pcv/core/point_types.h>
#include <pcv/planefitting/plane_mask_v1.h>
#include <pcv/camera/frame_data.h>
#include <QWidget>
#include <QRect>
#include <QSet>
#include <functional>

class PointCloudCanvas;
class QRubberBand;
class QPushButton;
class QLabel;
class QStackedWidget;
namespace pcv::render { class DepthImageCanvasV1; }

namespace pcv::demo {
class ScanGeometryCanvasV2 final : public QWidget {
public:
    enum class Tool { Browse, Workpiece, Interference, Rectangle };
    explicit ScanGeometryCanvasV2(QWidget *parent = nullptr);
    void publish(const QVector<pointcloud::Point3D> &points, const QVector<int> &displayToFull,
                 qsizetype fullCount, const QVector<int> &workpiece,
                 const QVector<int> &interference, const QVector<int> &pending, bool candidate, bool fit,quint64 revision=0,
                 const QVector<pointcloud::Point3D> &fullPoints={},
                 const pcv::planefitting::PlaneMaskMeshV1 *workpieceMask=nullptr,
                 const pcv::planefitting::PlaneMaskMeshV1 *interferenceMask=nullptr,
                 const pcv::planefitting::PlaneMaskMeshV1 *pendingMask=nullptr);
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void clearSelection();
    QVector<int> selectedIds() const { return m_selectedIds; }
    quint64 selectedRevision() const { return m_selectionRevision; }
    void showEmptyMessage(const QString &text);
    void showDepthFrame(const std::shared_ptr<const pcv::camera::CameraFrame> &frame,
                        const QVector<qsizetype> &highlightedPixels = {});
    void showPointCloud();
    bool showingDepth() const;
    PointCloudCanvas *canvas() const { return m_canvas; }
    std::function<void(int)> planePicked;
    std::function<void()> eraseRequested;
    std::function<void()> selectionChanged;
    std::function<void()> toolCancelled;
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void finishRectangle(const QPoint &point);
    void placeOverlays();
    PointCloudCanvas *m_canvas = nullptr;
    pcv::render::DepthImageCanvasV1 *m_depthCanvas = nullptr;
    QStackedWidget *m_stack = nullptr;
    QRubberBand *m_rubberBand = nullptr;
    QPushButton *m_erase = nullptr;
    QLabel *m_empty = nullptr;
    qsizetype m_fullCount = 0;
    quint64 m_revision = 0, m_selectionRevision = 0;
    QVector<int> m_selectedIds;
    Tool m_tool = Tool::Browse;
    bool m_dragging = false;
    QPoint m_start;
    QRect m_rectangle;
};
}
