#include <pcv/render/depth_image_canvas_v1.h>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtEndian>
#include <QColor>
#include <algorithm>

namespace pcv::render {
DepthImageCanvasV1::DepthImageCanvasV1(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true); setMinimumSize(320, 180);
    setToolTip(tr("滚轮缩放，右键拖动；选面模式下悬停识别、左键锁定"));
}
bool DepthImageCanvasV1::setDepth(const DepthImageDisplayV1 &image) {
    const quint64 count = quint64(image.width) * image.height;
    if (!count || count > (256 * 1024 * 1024 / 12) || image.c16.size() != qsizetype(count*2)) return false;
    const QSize size(int(image.width), int(image.height));
    const bool resized = m_depth.size() != size;
    QImage depth(size, QImage::Format_Indexed8);
    if (depth.isNull()) return false;
    QList<QRgb> palette{qRgb(24, 28, 33)};
    for (int i = 1; i < 256; ++i) palette.append(QColor::fromHsv((255-i)*240/254, 210, 240).rgb());
    depth.setColorTable(palette);
    qint16 minimum = 32767, maximum = -32767;
    const auto value = [&image](qsizetype i) { return qFromLittleEndian<qint16>(image.c16.constData()+2*i); };
    for (qsizetype i = 0; i < qsizetype(count); ++i) {
        const auto z = value(i); if (z != -32768) { minimum = std::min(minimum,z); maximum = std::max(maximum,z); }
    }
    const int span = std::max(1, int(maximum)-minimum);
    for (int y = 0; y < size.height(); ++y) {
        auto *line = depth.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const auto z = value(qsizetype(y)*size.width()+x);
            line[x] = z == -32768 ? 0 : uchar(1+(int(z)-minimum)*254/span);
        }
    }
    m_depth = std::move(depth); m_mask = {}; m_intensity = {};
    if (image.mono8.size() == qsizetype(count)) {
        // Own the backing storage; no borrowed memory escapes this call.
        m_intensity = QImage(reinterpret_cast<const uchar *>(image.mono8.constData()), size.width(), size.height(),
                             size.width(), QImage::Format_Grayscale8).copy();
    }
    if (resized) { m_zoom = 1; m_pan = {}; }
    update(); return true;
}
void DepthImageCanvasV1::setMask(const QImage &mask) {
    m_mask = mask.size() == m_depth.size() ? mask.convertToFormat(QImage::Format_Indexed8) : QImage();
    if (!m_mask.isNull()) {
        QList<QRgb> palette(256, qRgba(40,255,110,155)); palette[0] = qRgba(0,0,0,0);
        m_mask.setColorTable(palette);
    }
    update();
}
void DepthImageCanvasV1::showIntensity(bool enabled) { m_showIntensity = enabled; update(); }
QRectF DepthImageCanvasV1::imageRect() const {
    if (m_depth.isNull()) return {};
    const double scale = std::min(double(width())/m_depth.width(), double(height())/m_depth.height())*m_zoom;
    const QSizeF size(m_depth.width()*scale, m_depth.height()*scale);
    return {QPointF(width()/2.0,height()/2.0)-QPointF(size.width()/2,size.height()/2)+m_pan,size};
}
QPoint DepthImageCanvasV1::pixelAt(const QPointF &position) const {
    const auto rect = imageRect();
    if (rect.isEmpty() || !rect.contains(position)) return {-1,-1};
    const QPoint pixel(int((position.x()-rect.left())*m_depth.width()/rect.width()),
                       int((position.y()-rect.top())*m_depth.height()/rect.height()));
    if (!m_depth.rect().contains(pixel) || m_depth.constScanLine(pixel.y())[pixel.x()] == 0) return {-1,-1};
    return pixel;
}
void DepthImageCanvasV1::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.fillRect(rect(), QColor(24,28,33));
    const auto target = imageRect();
    if (target.isEmpty()) { painter.setPen(Qt::lightGray); painter.drawText(rect(),Qt::AlignCenter,tr("等待完整深度帧")); return; }
    painter.drawImage(target,m_showIntensity && !m_intensity.isNull() ? m_intensity : m_depth);
    if (!m_mask.isNull()) painter.drawImage(target,m_mask);
}
void DepthImageCanvasV1::mouseMoveEvent(QMouseEvent *event) {
    if (m_panning) { m_pan += event->position()-m_last; m_last = event->position(); update(); }
    else if (hovered) hovered(pixelAt(event->position()));
}
void DepthImageCanvasV1::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) { m_panning = true; m_last = event->position(); }
    else if (event->button() == Qt::LeftButton && clicked) clicked(pixelAt(event->position()));
}
void DepthImageCanvasV1::mouseReleaseEvent(QMouseEvent *event) { if (event->button() == Qt::RightButton) m_panning = false; }
void DepthImageCanvasV1::leaveEvent(QEvent *) { if (hovered) hovered({-1,-1}); }
void DepthImageCanvasV1::wheelEvent(QWheelEvent *event) {
    const auto center = QPointF(width()/2.0,height()/2.0);
    const auto before = event->position()-center-m_pan;
    const double zoom = std::clamp(m_zoom * (event->angleDelta().y()>0 ? 1.2 : 1/1.2),1.0,32.0);
    m_pan += before*(1-zoom/m_zoom); m_zoom = zoom; update();
}
}
