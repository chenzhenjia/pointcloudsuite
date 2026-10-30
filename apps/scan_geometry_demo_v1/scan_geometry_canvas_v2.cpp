#include "scan_geometry_canvas_v2.h"
#include <pcv/render/pointcloud_canvas.h>
#include <pcv/render/depth_image_canvas_v1.h>
#include <QLabel>
#include <QPushButton>
#include <QRubberBand>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <limits>

namespace pcv::demo {
ScanGeometryCanvasV2::ScanGeometryCanvasV2(QWidget *parent) : QWidget(parent) {
    setObjectName("demoCanvasHost");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    m_canvas = new PointCloudCanvas(m_stack);
    m_canvas->setObjectName("demoPointCloudCanvas");
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setAppearance(QColor("#eef1f4"), QColor("#344255"));
    m_canvas->setEmptyMessage(QString());
    m_canvas->setPointSize(2);
    m_canvas->setStandardView(2);
    auto options = m_canvas->overlayOptions();
    options.showColorBar = true;
    options.showCoordinateAxes = false;
    options.showAxisLabels = false;
    m_canvas->setOverlayOptions(options);
    m_depthCanvas = new pcv::render::DepthImageCanvasV1(m_stack);
    m_stack->addWidget(m_canvas);m_stack->addWidget(m_depthCanvas);layout->addWidget(m_stack);
    m_canvas->installEventFilter(this);
    m_canvas->pointPicked = [this](int index) {
        if ((m_tool == Tool::Workpiece || m_tool == Tool::Interference)
            && index >= 0 && index < m_fullCount && planePicked)
            planePicked(index);
    };
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_canvas);
    m_erase = new QPushButton(tr("擦除框内点云"), m_canvas);
    m_erase->setObjectName("eraseSelection"); m_erase->hide();
    connect(m_erase, &QPushButton::clicked, this, [this] { if (eraseRequested) eraseRequested(); });
    m_empty = new QLabel(m_canvas); m_empty->setObjectName("emptyCanvasMessage");
    m_empty->setAlignment(Qt::AlignCenter); m_empty->setWordWrap(true);
    m_empty->setStyleSheet("color:#526174; font-size:14px; background:transparent;");
    m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    setTool(Tool::Browse);
}
void ScanGeometryCanvasV2::showDepthFrame(const std::shared_ptr<const pcv::camera::CameraFrame> &frame,
                                           const QVector<qsizetype> &highlightedPixels) {
    if(!frame||frame->frameType!=pcv::camera::FrameType::DepthC16)return;
    if(!m_depthCanvas->setDepth({frame->width,frame->height,frame->data,frame->intensityData}))return;
    QImage mask(int(frame->width),int(frame->height),QImage::Format_Grayscale8);mask.fill(0);
    for(qsizetype pixel:highlightedPixels)if(pixel>=0&&quint64(pixel)<quint64(frame->width)*frame->height)
        mask.scanLine(int(pixel/frame->width))[pixel%frame->width]=255;
    m_depthCanvas->setMask(mask);m_stack->setCurrentWidget(m_depthCanvas);
    m_empty->hide();
}
void ScanGeometryCanvasV2::showPointCloud() {m_stack->setCurrentWidget(m_canvas);}
bool ScanGeometryCanvasV2::showingDepth() const {return m_stack->currentWidget()==m_depthCanvas;}
void ScanGeometryCanvasV2::publish(const QVector<pointcloud::Point3D> &points, const QVector<int> &displayToFull,
    qsizetype fullCount,const QVector<int> &workpiece,const QVector<int> &interference,
    const QVector<int> &pending,bool candidate,bool fit,quint64 revision,const QVector<pointcloud::Point3D> &fullPoints,
    const pcv::planefitting::PlaneMaskMeshV1 *workpieceMask,
    const pcv::planefitting::PlaneMaskMeshV1 *interferenceMask,
    const pcv::planefitting::PlaneMaskMeshV1 *pendingMask) {
    Q_UNUSED(candidate);
    clearSelection();m_fullCount=fullCount;m_revision=revision;
    pcv::render::PointColorOverlay markers;
    const auto rgba=[](const char *hex) {const QColor c(hex);return QVector4D(c.redF(),c.greenF(),c.blueF(),1);};
    markers.colors.fill(QVector4D(0,0,0,0),fullCount);markers.valid.fill(0,fullCount);
    for(int i:workpiece)if(i>=0&&i<fullCount){markers.colors[i]=rgba("#1675e8");markers.valid[i]=1;}
    for(int i:interference)if(i>=0&&i<fullCount){markers.colors[i]=rgba("#f04452");markers.valid[i]=1;}
    for(int i:pending)if(i>=0&&i<fullCount){markers.colors[i]=rgba("#f04452");markers.valid[i]=1;}
    m_canvas->setDisplayCloud(points,displayToFull,fullCount);
    m_canvas->setPointMarkerOverlay(markers);
    QVector<pcv::render::PlanarMaskOverlay> masks;
    const auto appendMask=[&](const pcv::planefitting::PlaneMaskMeshV1 *mask,const QColor &color) {
        if(!mask||mask->triangleIndices.isEmpty())return;
        pcv::render::PlanarMaskOverlay overlay;overlay.vertices=mask->vertices;overlay.triangleIndices=mask->triangleIndices;
        overlay.boundaryVertices=mask->boundaryVertices;overlay.boundaryRanges=mask->boundaryRanges;
        overlay.color=QVector4D(color.redF(),color.greenF(),color.blueF(),1.0f);overlay.opacity=0.25f;masks.append(std::move(overlay));
    };
    appendMask(workpieceMask,QColor("#1675e8"));appendMask(interferenceMask,QColor("#f04452"));appendMask(pendingMask,QColor("#f04452"));
    m_canvas->setPlanarMaskOverlays(masks);
    float minimum=std::numeric_limits<float>::max(),maximum=std::numeric_limits<float>::lowest();
    const auto &rangePoints=fullPoints.isEmpty()?points:fullPoints;
    for(const auto &point:rangePoints)if(std::isfinite(point.z)){minimum=qMin(minimum,point.z);maximum=qMax(maximum,point.z);}
    if(minimum==std::numeric_limits<float>::max()){minimum=0;maximum=1;}
    else if(!(maximum>minimum))maximum=minimum+1.0f;
    m_canvas->setDisplayOptions(0, 1, minimum, maximum);
    if (fit) m_canvas->fitView();
}
void ScanGeometryCanvasV2::setTool(Tool tool) {
    clearSelection(); m_tool = tool;
    m_canvas->setSelectionMode(tool == Tool::Workpiece || tool == Tool::Interference);
    m_canvas->setCursor(tool == Tool::Browse ? Qt::ArrowCursor : Qt::CrossCursor);
    m_canvas->setToolTip(tool == Tool::Browse ? tr("左键旋转，右键平移，滚轮缩放")
        : tool == Tool::Rectangle ? tr("拖框仅选当前可见点，确认后擦除；Esc 退出")
        : tr("单击点云选择所在平面；Esc 退出"));
}
void ScanGeometryCanvasV2::clearSelection() {
    m_dragging = false; m_rectangle = {}; m_selectedIds.clear();
    m_selectionRevision=0;
    if (m_rubberBand) m_rubberBand->hide();
    if (m_erase) m_erase->hide();
    if (m_canvas) m_canvas->setSelectedIndices({});
}
void ScanGeometryCanvasV2::showEmptyMessage(const QString &text) {
    m_empty->setText(text); m_empty->setVisible(!text.isEmpty()); placeOverlays();
}
void ScanGeometryCanvasV2::placeOverlays() {
    m_empty->setGeometry(m_canvas->rect().adjusted(35, 70, -35, -70));
    if (!m_rectangle.isEmpty()) {
        m_erase->adjustSize();
        const QPoint point(qBound(0, m_rectangle.right() + 5, qMax(0, m_canvas->width() - m_erase->width())),
                           qBound(0, m_rectangle.bottom() + 5, qMax(0, m_canvas->height() - m_erase->height())));
        m_erase->move(point); m_erase->raise();
    }
}
void ScanGeometryCanvasV2::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); placeOverlays(); }
void ScanGeometryCanvasV2::finishRectangle(const QPoint &point) {
    m_dragging = false;
    m_rectangle = QRect(m_start, point).normalized().intersected(m_canvas->rect());
    if (m_rectangle.width() < 3 || m_rectangle.height() < 3) { clearSelection(); return; }
    const QVector<int> indices = m_canvas->pickRectangleForSelection(m_rectangle);
    m_selectionRevision=m_revision;
    m_selectedIds.clear();
    for (int index : indices) if (index >= 0 && index < m_fullCount) m_selectedIds.append(index);
    m_canvas->setSelectedIndices(indices);
    m_rubberBand->setGeometry(m_rectangle); m_rubberBand->show();
    m_erase->setEnabled(!m_selectedIds.isEmpty()); m_erase->show(); placeOverlays();
    if (selectionChanged) selectionChanged();
}
bool ScanGeometryCanvasV2::eventFilter(QObject *object, QEvent *event) {
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
