#pragma once

#include "demo_cloud_v1.h"
#include <QWidget>
#include <QRect>
#include <QSet>
#include <functional>

class PointCloudCanvas;
class QRubberBand;
class QPushButton;
class QLabel;

namespace pcv::demo {
class DemoCanvasV1 final : public QWidget {
public:
    enum class Tool { Browse, Workpiece, Interference, Rectangle };
    explicit DemoCanvasV1(QWidget *parent = nullptr);
    void publish(const QVector<DemoPointV1> &points, DemoPlaneV1 workpiece,
                 const QSet<DemoPlaneV1> &interferences, DemoPlaneV1 suspect, bool fit);
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void clearSelection();
    QVector<int> selectedIds() const { return m_selectedIds; }
    void showEmptyMessage(const QString &text);
    PointCloudCanvas *canvas() const { return m_canvas; }
    std::function<void(DemoPlaneV1)> planePicked;
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
    QRubberBand *m_rubberBand = nullptr;
    QPushButton *m_erase = nullptr;
    QLabel *m_empty = nullptr;
    QVector<DemoPointV1> m_displayPoints;
    QVector<int> m_selectedIds;
    Tool m_tool = Tool::Browse;
    bool m_dragging = false;
    QPoint m_start;
    QRect m_rectangle;
};
}
