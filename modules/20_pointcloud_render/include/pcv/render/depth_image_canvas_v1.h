#pragma once
#include <QWidget>
#include <QImage>
#include <QByteArray>
#include <functional>

namespace pcv::render {
// SDK-independent, immutable row-major C16 display input. Values are signed LE.
struct DepthImageDisplayV1 {
    quint32 width = 0, height = 0;
    QByteArray c16;
    QByteArray mono8;
};
class DepthImageCanvasV1 final : public QWidget {
public:
    explicit DepthImageCanvasV1(QWidget *parent = nullptr);
    bool setDepth(const DepthImageDisplayV1 &image);
    void setMask(const QImage &mask);
    void showIntensity(bool enabled);
    QPoint pixelAt(const QPointF &position) const;
    std::function<void(QPoint)> hovered;
    std::function<void(QPoint)> clicked;
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    QRectF imageRect() const;
    QImage m_depth, m_intensity, m_mask;
    double m_zoom = 1.0;
    QPointF m_pan, m_last;
    bool m_panning = false, m_showIntensity = false;
};
}
