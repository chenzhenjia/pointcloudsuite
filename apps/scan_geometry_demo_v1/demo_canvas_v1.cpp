#include "demo_canvas_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QLabel>
#include <QPushButton>
#include <QRubberBand>
#include <QVBoxLayout>

namespace pcv::demo {
DemoCanvasV1::DemoCanvasV1(QWidget *parent) : QWidget(parent) {
    setObjectName("demoCanvasHost");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new PointCloudCanvas(this);
    m_canvas->setObjectName("demoPointCloudCanvas");
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setAppearance(QColor("#f5f9fc"), QColor("#b33436"));
    m_canvas->setPointSize(4);
    m_canvas->setStandardView(2);
    auto options = m_canvas->overlayOptions();
    options.showColorBar = false;
    options.showCoordinateAxes = false;
    options.showAxisLabels = false;
    m_canvas->setOverlayOptions(options);
    layout->addWidget(m_canvas);
    m_canvas->installEventFilter(this);
    m_canvas->pointPicked = [this](int index) {
        if ((m_tool == Tool::Workpiece || m_tool == Tool::Interference)
            && index >= 0 && index < m_displayPoints.size() && planePicked)
            planePicked(m_displayPoints.at(index).plane);
    };
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_canvas);
    m_erase = new QPushButton(tr("擦除框内点云"), m_canvas);
    m_erase->setObjectName("eraseSelection"); m_erase->hide();
    connect(m_erase, &QPushButton::clicked, this, [this] { if (eraseRequested) eraseRequested(); });
    m_empty = new QLabel(m_canvas); m_empty->setObjectName("emptyCanvasMessage");
    m_empty->setAlignment(Qt::AlignCenter); m_empty->setWordWrap(true);
    m_empty->setStyleSheet("color:#66788d; font-size:14px; background:transparent;");
    m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    setTool(Tool::Browse);
}
void DemoCanvasV1::publish(const QVector<DemoPointV1> &points, DemoPlaneV1 workpiece,
                         const QSet<DemoPlaneV1> &interferences, DemoPlaneV1 suspect, bool fit) {
    clearSelection(); m_displayPoints = points;
    QVector<pointcloud::Point3D> cloud;
    pcv::render::PointColorOverlay overlay;
    for (const auto &p : points) {
        cloud.append(p.position);
        QColor color("#7898af");
        if (p.plane == workpiece) color = QColor("#1769c2");
        else if (interferences.contains(p.plane)) color = QColor("#b17b12");
        if (p.plane == suspect) color = QColor("#d56a23");
        overlay.colors.append(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), 1));
        overlay.valid.append(1);
    }
    m_canvas->setCloud(std::move(cloud));
    m_canvas->setPointColorOverlay(overlay);
    m_canvas->setDisplayOptions(3, 1, -14, 24);
    if (fit) m_canvas->fitView();
}
void DemoCanvasV1::setTool(Tool tool) {
    clearSelection(); m_tool = tool;
    m_canvas->setSelectionMode(tool == Tool::Workpiece || tool == Tool::Interference);
    m_canvas->setCursor(tool == Tool::Browse ? Qt::ArrowCursor : Qt::CrossCursor);
    m_canvas->setToolTip(tool == Tool::Browse ? tr("左键旋转，右键平移，滚轮缩放")
        : tool == Tool::Rectangle ? tr("拖框仅选当前可见点，确认后擦除；Esc 退出")
        : tr("单击点云选择所在平面；Esc 退出"));
}
void DemoCanvasV1::clearSelection() {
    m_dragging = false; m_rectangle = {}; m_selectedIds.clear();
    if (m_rubberBand) m_rubberBand->hide();
    if (m_erase) m_erase->hide();
    if (m_canvas) m_canvas->setSelectedIndices({});
}
void DemoCanvasV1::showEmptyMessage(const QString &text) {
    m_empty->setText(text); m_empty->setVisible(!text.isEmpty()); placeOverlays();
}
void DemoCanvasV1::placeOverlays() {
    m_empty->setGeometry(m_canvas->rect().adjusted(35, 70, -35, -70));
    if (!m_rectangle.isEmpty()) {
        m_erase->adjustSize();
        const QPoint point(qBound(0, m_rectangle.right() + 5, qMax(0, m_canvas->width() - m_erase->width())),
                           qBound(0, m_rectangle.bottom() + 5, qMax(0, m_canvas->height() - m_erase->height())));
        m_erase->move(point); m_erase->raise();
    }
}
void DemoCanvasV1::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); placeOverlays(); }
void DemoCanvasV1::finishRectangle(const QPoint &point) {
    m_dragging = false;
    m_rectangle = QRect(m_start, point).normalized().intersected(m_canvas->rect());
    if (m_rectangle.width() < 3 || m_rectangle.height() < 3) { clearSelection(); return; }
    const QVector<int> indices = m_canvas->pickRectangleForSelection(m_rectangle);
    m_selectedIds.clear();
    for (int index : indices) if (index >= 0 && index < m_displayPoints.size()) m_selectedIds.append(m_displayPoints.at(index).id);
    m_canvas->setSelectedIndices(indices);
    m_rubberBand->setGeometry(m_rectangle); m_rubberBand->show();
    m_erase->setEnabled(!m_selectedIds.isEmpty()); m_erase->show(); placeOverlays();
    if (selectionChanged) selectionChanged();
}
bool DemoCanvasV1::eventFilter(QObject *object, QEvent *event) {
    if (object != m_canvas) return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        setTool(Tool::Browse); if (toolCancelled) toolCancelled(); return true;
    }
    if (m_tool == Tool::Rectangle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                clearSelection(); m_start = mouse->position().toPoint(); m_dragging = true;
                m_canvas->setFocus(); return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            m_rectangle = QRect(m_start, static_cast<QMouseEvent *>(event)->position().toPoint()).normalized();
            m_rubberBand->setGeometry(m_rectangle); m_rubberBand->show(); return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && m_dragging) { finishRectangle(mouse->position().toPoint()); return true; }
        }
    }
    // A pending screen-space selection must never survive a camera change.
    if (event->type() == QEvent::Wheel || (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)) clearSelection();
    if (event->type() == QEvent::Resize) clearSelection();
    return QWidget::eventFilter(object, event);
}
}
