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
#include <algorithm>

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
    m_canvas->setDisplayOptions(0,1.0,0.0,1.0);
    m_canvas->setStandardView(2);
    auto options = m_canvas->overlayOptions();
    options.showColorBar = true;
    options.showCoordinateAxes = false;
    options.showAxisLabels = false;
    m_canvas->setOverlayOptions(options);
    m_depthCanvas = new pcv::render::DepthImageCanvasV1(m_stack);
    m_stack->addWidget(m_canvas);m_stack->addWidget(m_depthCanvas);layout->addWidget(m_stack);
    m_canvas->installEventFilter(this);
    m_canvas->displayTiming=[this](quint64 revision,const QJsonObject &timing) {
        if(timing.value("event").toString()=="canvas_publish_cpu")m_observedCloud=revision;
        if(m_observedFrame&&revision==m_observedCloud&&loadCanvasTiming)
            loadCanvasTiming(m_observedFrame,m_observedAttempt,timing);
    };
    connect(m_canvas,&QOpenGLWidget::frameSwapped,this,[this] {
        if(m_canvas->isVisible()&&!showingDepth()&&m_observedFrame&&!m_observedFirst&&m_canvas->displayCloudRevision()==m_observedCloud
            &&m_canvas->hasRenderedCurrentCloud()) {
            m_observedFirst=true;
            if(loadCanvasTiming&&m_viewStarted.isValid())loadCanvasTiming(m_observedFrame,m_observedAttempt,
                {{"event","view_frame_swapped"},{"view_sequence",QString::number(m_viewSequence)},
                 {"display_revision",QString::number(m_observedCloud)},
                 {"view_wait_ms",double(m_viewStarted.elapsed())},
                 {"visible_wait_ms",double(m_visibleMs+(m_visibleStarted.isValid()?m_visibleStarted.elapsed():0))}});
            if(loadFirstFrame)loadFirstFrame(m_observedFrame,m_observedAttempt);
        }
    });
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
void ScanGeometryCanvasV2::bindLoadObservation(quint64 frameId,quint64 attempt) {
    if(m_observedFrame==frameId&&m_observedAttempt==attempt)return;
    m_observedFrame=frameId;m_observedAttempt=attempt;m_observedCloud=0;m_observedFirst=false;
    ++m_viewSequence;m_viewStarted.start();m_visibleMs=0;m_visibleStarted.invalidate();
    if(m_canvas->isVisible())m_visibleStarted.start();
}
void ScanGeometryCanvasV2::beginViewObservation(quint64 frameId,quint64 attempt) {
    m_observedFrame=0;m_observedAttempt=0;bindLoadObservation(frameId,attempt);
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
    const pcv::planefitting::PlaneMaskMeshV1 *pendingMask,DisplayMode mode,const QByteArray &heightSource,std::optional<QPair<float,float>> preparedHeightRange) {
    Q_UNUSED(candidate);
    Q_UNUSED(workpiece);Q_UNUSED(interference);Q_UNUSED(pending);
    const auto &heightPoints=fullPoints.isEmpty()?points:fullPoints;
    const auto measureHeight=[&] {
        float lower=std::numeric_limits<float>::infinity(),upper=-lower;
        for(const auto &point:heightPoints)if(std::isfinite(point.z)) {
            lower=std::min(lower,point.z);upper=std::max(upper,point.z);
        }
        if(!std::isfinite(lower))return qMakePair(0.0f,1.0f);
        if(upper<=lower)upper=lower+1.0f;
        if(upper<=lower)upper=std::nextafter(lower,std::numeric_limits<float>::infinity());
        ++m_heightRangeRevision;
        return qMakePair(lower,upper);
    };
    QPair<float,float> heightRange;
    if(preparedHeightRange) {
        heightRange=*preparedHeightRange;
        if(!heightSource.isEmpty())m_heightRanges.insert(heightSource,heightRange);
    } else if(!heightSource.isEmpty()) {
        auto found=m_heightRanges.constFind(heightSource);
        if(found==m_heightRanges.cend()) {
            heightRange=measureHeight();m_heightRanges.insert(heightSource,heightRange);
        } else heightRange=found.value();
        m_unkeyedHeightSnapshot.clear();
        m_unkeyedHeightRange={0.0f,1.0f};
    } else {
        if(m_unkeyedHeightSnapshot.constData()!=heightPoints.constData()
            ||m_unkeyedHeightSnapshot.size()!=heightPoints.size()) {
            m_unkeyedHeightRange=measureHeight();m_unkeyedHeightSnapshot=heightPoints;
        }
        heightRange=m_unkeyedHeightRange;
    }
    const bool cloudChanged = m_displayRevision==0 || fit || m_fullCount!=fullCount || m_revision!=revision
        || m_displaySnapshot.constData()!=points.constData() || m_displaySnapshot.size()!=points.size()
        || m_mappingSnapshot.constData()!=displayToFull.constData() || m_mappingSnapshot.size()!=displayToFull.size();
    bool regionsChanged=cloudChanged;
    const pcv::planefitting::PlaneMaskMeshV1 emptyMask;
    const pcv::planefitting::PlaneMaskMeshV1 *sources[]={workpieceMask,interferenceMask,pendingMask};
    for(int i=0;i<3;++i) {
        const auto &source=sources[i]?*sources[i]:emptyMask;
        const auto &old=m_maskSnapshots[i];
        regionsChanged=regionsChanged || old.vertices.constData()!=source.vertices.constData()
            || old.triangleIndices.constData()!=source.triangleIndices.constData()
            || old.boundaryVertices.constData()!=source.boundaryVertices.constData()
            || old.boundaryRanges.constData()!=source.boundaryRanges.constData();
    }
    if(cloudChanged) {
        clearSelection();m_fullCount=fullCount;m_revision=revision;
        m_canvas->setDisplayCloud(points,displayToFull,fullCount);
        m_displaySnapshot=points;m_mappingSnapshot=displayToFull;++m_displayRevision;
    }
    if(!cloudChanged&&m_observedFrame&&m_observedCloud==0)m_observedCloud=m_canvas->displayCloudRevision();
    if(regionsChanged) {
        QVector<pcv::render::PlanarMaskOverlay> masks;
        const auto appendMask=[&](const pcv::planefitting::PlaneMaskMeshV1 *mask,const QColor &color) {
            if(!mask||mask->triangleIndices.isEmpty())return;
            pcv::render::PlanarMaskOverlay overlay;overlay.vertices=mask->vertices;overlay.triangleIndices=mask->triangleIndices;
            overlay.boundaryVertices=mask->boundaryVertices;overlay.boundaryRanges=mask->boundaryRanges;
            overlay.color=QVector4D(color.redF(),color.greenF(),color.blueF(),1.0f);overlay.opacity=0.25f;masks.append(std::move(overlay));
        };
        appendMask(workpieceMask,QColor("#1675e8"));appendMask(interferenceMask,QColor("#f04452"));appendMask(pendingMask,QColor("#f04452"));
        m_canvas->setPlanarMaskOverlays(masks);
        for(int i=0;i<3;++i)m_maskSnapshots[i]=sources[i]?*sources[i]:emptyMask;
        ++m_regionRevision;
    }
    // setDisplayCloud resets map bounds to the display subset: restore the
    // complete source range afterwards, including when every result is trimmed.
    m_canvas->setDisplayOptions(0,1.0,heightRange.first,heightRange.second);
    if(mode==DisplayMode::UniformGray)m_canvas->setUniformPointColor(QColor("#59636f"));
    else m_canvas->clearUniformPointColor();
    auto options=m_canvas->overlayOptions();options.showColorBar=mode==DisplayMode::Height;
    m_canvas->setOverlayOptions(options);
    if (fit) m_canvas->fitView();
}
void ScanGeometryCanvasV2::setTool(Tool tool) {
    clearSelection(); m_tool = tool;
    m_canvas->setControlPointIndices({});
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
    m_rubberBand->setGeometry(m_rectangle); m_rubberBand->show();
    m_erase->setText(tr("擦除 %1 点").arg(m_selectedIds.size()));
    m_erase->setEnabled(!m_selectedIds.isEmpty()); m_erase->show(); placeOverlays();
    if (selectionChanged) selectionChanged();
}
bool ScanGeometryCanvasV2::eventFilter(QObject *object, QEvent *event) {
    if (object != m_canvas) return QWidget::eventFilter(object, event);
    if(event->type()==QEvent::Hide&&m_visibleStarted.isValid()) {
        m_visibleMs+=m_visibleStarted.elapsed();m_visibleStarted.invalidate();
    } else if(event->type()==QEvent::Show&&m_viewStarted.isValid()&&!m_observedFirst&&!m_visibleStarted.isValid())m_visibleStarted.start();
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
