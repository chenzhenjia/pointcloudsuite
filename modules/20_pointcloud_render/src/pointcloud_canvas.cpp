#include <pcv/render/pointcloud_canvas.h>

#include "overlay_geometry.h"

#include <QHash>
#include <QSet>
#include <new>

namespace {
class CanvasOverlay final : public QWidget {
public:
    CanvasOverlay(QWidget *parent, std::function<void(QPaintDevice *)> draw)
        : QWidget(parent), m_draw(std::move(draw)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
protected:
    void paintEvent(QPaintEvent *) override { m_draw(this); }
private:
    std::function<void(QPaintDevice *)> m_draw;
};
}

PointCloudCanvas::PointCloudCanvas(QWidget *parent)
        : QOpenGLWidget(parent), m_vertexBuffer(QOpenGLBuffer::VertexBuffer),
          m_stateBuffer(QOpenGLBuffer::VertexBuffer),
          m_textureBuffer(QOpenGLBuffer::VertexBuffer),
          m_markerBuffer(QOpenGLBuffer::VertexBuffer),
          m_planarMaskVertexBuffer(QOpenGLBuffer::VertexBuffer),
          m_contourBuffer(QOpenGLBuffer::VertexBuffer) {
        setMinimumSize(620, 480);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("左键旋转，右键平移，滚轮缩放"));
        m_overlayWidget = new CanvasOverlay(this, [this](QPaintDevice *device) { drawOverlay(device); });
        m_overlayWidget->show();
    }

PointCloudCanvas::~PointCloudCanvas()  {
        if (QCoreApplication::closingDown() || !context() || !context()->isValid()) return;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return;
        m_pickingFbo.reset();
        if (m_contourBuffer.isCreated()) m_contourBuffer.destroy();
        if (m_contourVertexArray.isCreated()) m_contourVertexArray.destroy();
        if (m_planarMaskVertexArray.isCreated()) m_planarMaskVertexArray.destroy();
        if (m_stateBuffer.isCreated()) m_stateBuffer.destroy();
        if (m_textureBuffer.isCreated()) m_textureBuffer.destroy();
        if (m_markerBuffer.isCreated()) m_markerBuffer.destroy();
        if (m_planarMaskIndexBuffer.isCreated()) m_planarMaskIndexBuffer.destroy();
        if (m_planarMaskVertexBuffer.isCreated()) m_planarMaskVertexBuffer.destroy();
        if (m_vertexBuffer.isCreated()) m_vertexBuffer.destroy();
        if (m_vertexArray.isCreated()) m_vertexArray.destroy();
        doneCurrent();
    }

void PointCloudCanvas::setCloud(QVector<pointcloud::Point3D> points) {
        if (QThread::currentThread() != thread()) {
            qWarning() << "PointCloudCanvas::setCloud queued to GUI thread, points="
                       << points.size();
            QMetaObject::invokeMethod(
                this,
                [this, points = std::move(points)]() mutable {
                    setCloud(std::move(points));
                },
                Qt::QueuedConnection);
            return;
        }
          m_points = std::move(points);
          m_hasDisplayMapping = false;
          m_displayToFullIndices.clear();
          m_fullToDisplayIndices.clear();
          m_fullPointCount = m_points.size();
          m_pointStates.fill(NormalPoint, m_points.size());
          clearPointMarkerOverlay();
          clearPlanarMaskOverlays();
          // Point-index overlays refer to the previous frame and are invalid
          // after replacement; source-bound planar masks are cleared as well.
          m_selectedIndices.clear();
          m_selectedEdgeIndices.clear();
          m_edgeIndices.clear();
        ++m_renderRevision;
        validateRenderSnapshot();
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_selectedIndices.clear();
        m_coordinatePointIndex = -1;
        m_workpieceCoordinate = {};
        clearTextureGrayOverlay();
        m_contourVertices.clear();
        m_contourRanges.clear();
        m_contourUploadPending = true;
        updateBounds();
        m_uploadError.clear();
        m_uploadPending = true;
        qInfo() << "PointCloudCanvas cache published, points=" << m_points.size();
        update();
    }

void PointCloudCanvas::setDisplayCloud(QVector<pointcloud::Point3D> points,
                                       QVector<int> displayToFullIndices,
                                       qsizetype fullPointCount) {
        if (QThread::currentThread() != thread()) {
            QMetaObject::invokeMethod(this,
                [this, points = std::move(points), mapping = std::move(displayToFullIndices), fullPointCount]() mutable {
                    setDisplayCloud(std::move(points), std::move(mapping), fullPointCount);
                }, Qt::QueuedConnection);
            return;
        }
        if (fullPointCount < 0 || points.size() != displayToFullIndices.size()
            || points.size() > fullPointCount || points.size() > std::numeric_limits<int>::max()) {
            qWarning() << "PointCloudCanvas rejected display cloud mapping size" << points.size()
                       << displayToFullIndices.size() << fullPointCount;
            return;
        }
        QHash<int,int> fullToDisplay;
        fullToDisplay.reserve(displayToFullIndices.size());
        for (int index : displayToFullIndices) {
            if (index < 0 || qsizetype(index) >= fullPointCount || fullToDisplay.contains(index)) {
                qWarning() << "PointCloudCanvas rejected display cloud mapping index" << index;
                return;
            }
            fullToDisplay.insert(index,int(fullToDisplay.size()));
        }
        for (const auto &point : points) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
                || !std::isfinite(point.nx) || !std::isfinite(point.ny) || !std::isfinite(point.nz)) {
                qWarning() << "PointCloudCanvas rejected non-finite display point";
                return;
            }
        }
        m_points = std::move(points);
        m_hasDisplayMapping = true;
        m_displayToFullIndices = std::move(displayToFullIndices);
        m_fullToDisplayIndices = std::move(fullToDisplay);
        m_fullPointCount = fullPointCount;
        m_pointStates.fill(NormalPoint, m_points.size());
        clearPointMarkerOverlay();
        clearPlanarMaskOverlays();
        m_selectedIndices.clear();
        m_selectedEdgeIndices.clear();
        m_edgeIndices.clear();
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_coordinatePointIndex = -1;
        m_workpieceCoordinate = {};
        clearTextureGrayOverlay();
        m_contourVertices.clear();
        m_contourRanges.clear();
        m_contourUploadPending = true;
        ++m_renderRevision;
        updateBounds();
        m_uploadError.clear();
        m_uploadPending = true;
        m_stateUploadPending = true;
        qInfo() << "PointCloudCanvas display cloud published, points=" << m_points.size()
                << "full points=" << m_fullPointCount;
        update();
    }

void PointCloudCanvas::setCloudStreaming(QVector<pointcloud::Point3D> points) {
        if (QThread::currentThread() != thread()) {
            qWarning() << "PointCloudCanvas::setCloudStreaming queued to GUI thread, points="
                       << points.size();
            QMetaObject::invokeMethod(
                this,
                [this, points = std::move(points)]() mutable {
                    setCloudStreaming(std::move(points));
                },
                Qt::QueuedConnection);
            return;
        }
        m_points = std::move(points);
        m_hasDisplayMapping = false;
        m_displayToFullIndices.clear();
        m_fullToDisplayIndices.clear();
        m_fullPointCount = m_points.size();
        m_pointStates.fill(NormalPoint, m_points.size());
        clearPointMarkerOverlay();
        clearPlanarMaskOverlays();
        ++m_renderRevision;
        validateRenderSnapshot();
        // Point-index overlays belong to the preceding frame. Keep only
        // world-coordinate overlays, which remain meaningful across frames.
        m_selectedIndices.clear();
        m_selectedEdgeIndices.clear();
        m_edgeIndices.clear();
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_coordinatePointIndex = -1;
        clearTextureGrayOverlay();
        updateBounds();
        m_uploadError.clear();
        m_uploadPending = true;
        m_stateUploadPending = true;
        qInfo() << "PointCloudCanvas streaming cloud published, points=" << m_points.size();
        update();
    }

void PointCloudCanvas::setSelectionMode(bool enabled) {
        if (QThread::currentThread() != thread()) return;
        if(m_selectionMode==enabled)return;
        m_selectionMode = enabled;
        setToolTip(enabled ? tr("左键选择点，右键平移，Esc 取消，Backspace 撤销")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

void PointCloudCanvas::setEdgeSelectionMode(bool enabled, const QVector<int> &edgeIndices) {
        if (QThread::currentThread() != thread()) return;
        m_edgeSelectionMode = enabled;
        m_edgeIndices.clear();
        for (int index : edgeIndices) {
            const int display = displayIndexForFullIndex(index);
            if (display >= 0) m_edgeIndices.push_back(display);
        }
        setToolTip(enabled ? tr("左键选择边缘真实点，Esc 取消")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

void PointCloudCanvas::setSelectedEdgeIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedEdgeIndices.clear();
        for (int index : indices) {
            const int display = displayIndexForFullIndex(index);
            if (display >= 0) m_selectedEdgeIndices.push_back(display);
        }
        update();
    }

QVector<int> PointCloudCanvas::pickRectangleForSelection(const QRectF &selection) {
        return pickRectangle(selection);
    }

void PointCloudCanvas::setSelectedIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedIndices.clear();
        for (int index : indices) {
            const int display = displayIndexForFullIndex(index);
            if (display >= 0) m_selectedIndices.push_back(display);
        }
        update();
    }

void PointCloudCanvas::setPlaneResult(const QVector<int> &planeIndices, const QVector<int> &edgeIndices,
                        const QVector<pcv::render::Contour> &contours) {
        if (QThread::currentThread() != thread()) return;
        QString renderError;
        if (!pcv::render::validateContours(contours, &renderError)) {
            qWarning() << "PointCloudCanvas rejected contour snapshot:" << renderError;
            return;
        }
        QVector<int> mappedPlane, mappedEdge;
        for (int index : planeIndices) {
            const int display = displayIndexForFullIndex(index);
            if (display >= 0) mappedPlane.push_back(display);
        }
        for (int index : edgeIndices) {
            const int display = displayIndexForFullIndex(index);
            if (display >= 0) mappedEdge.push_back(display);
        }
        const bool planeChanged = m_planeResultIndices != mappedPlane;
        m_planeResultIndices = std::move(mappedPlane);
        m_edgeResultIndices = std::move(mappedEdge);
        m_pointStates.fill(NormalPoint, m_points.size());
        for (int index : m_planeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = PlanePoint;
        for (int index : m_edgeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = EdgePoint;
        m_contourVertices.clear();
        m_contourRanges.clear();
        for (const pcv::render::Contour &contour : contours) {
            if (contour.points.size() < 2) continue;
            const int start = m_contourVertices.size();
            for (const QVector3D &point : contour.points)
                m_contourVertices.push_back({point.x(), point.y(), point.z()});
            m_contourRanges.push_back({start, int(contour.points.size())});
        }
        ++m_renderRevision;
        m_stateUploadPending = true;
        m_contourUploadPending = true;
        if (planeChanged) clearTextureGrayOverlay();
        validateRenderSnapshot();
        update();
    }

void PointCloudCanvas::setExtractedPlane(const QVector<int> &planeIndices) {
        setPlaneResult(planeIndices, {}, {});
    }

bool PointCloudCanvas::setPlanarMaskOverlays(const QVector<pcv::render::PlanarMaskOverlay> &overlays) {
    if (QThread::currentThread() != thread()) return false;
    QString error;
    qsizetype vertexCount = 0, boundaryCount = 0, indexCount = 0;
    for (const auto &overlay : overlays) {
        if (!pcv::render::validatePlanarMaskOverlay(overlay, &error)) {
            qWarning() << "PointCloudCanvas rejected planar mask overlay:" << error;
            return false;
        }
        const qsizetype maxBufferVertices = std::numeric_limits<int>::max() / qsizetype(sizeof(QVector3D));
        const qsizetype maxBufferIndices = std::numeric_limits<int>::max() / qsizetype(sizeof(quint32));
        if (overlay.vertices.size() > qsizetype(std::numeric_limits<quint32>::max()) - vertexCount
            || overlay.vertices.size() > maxBufferVertices - vertexCount
            || overlay.boundaryVertices.size() > maxBufferVertices - vertexCount - overlay.vertices.size() - boundaryCount
            || overlay.triangleIndices.size() > maxBufferIndices - indexCount) {
            qWarning() << "PointCloudCanvas rejected oversized planar mask overlay";
            return false;
        }
        vertexCount += overlay.vertices.size();
        boundaryCount += overlay.boundaryVertices.size();
        indexCount += overlay.triangleIndices.size();
    }
    QVector<QVector3D> vertices, boundaryVertices;
    QVector<quint32> indices;
    QVector<QPair<int,int>> ranges, boundaryRanges;
    QVector<QVector4D> colors, boundaryColors;
    QVector<float> opacities;
    try {
    for (const auto &overlay : overlays) {
        const quint32 vertexOffset = quint32(vertices.size());
        const int indexStart = indices.size();
        vertices.append(overlay.vertices);
        for (quint32 index : overlay.triangleIndices) indices.append(vertexOffset + index);
        ranges.append({indexStart, overlay.triangleIndices.size()});
        colors.append(overlay.color);
        opacities.append(overlay.opacity);
        const int boundaryOffset = boundaryVertices.size();
        boundaryVertices.append(overlay.boundaryVertices);
        for (const auto &range : overlay.boundaryRanges)
            boundaryRanges.append({boundaryOffset + range.first, range.second});
        for (int i = 0; i < overlay.boundaryRanges.size(); ++i) boundaryColors.append(overlay.color);
    }
    } catch (const std::bad_alloc &) {
        qWarning() << "PointCloudCanvas could not allocate planar mask overlay buffers";
        return false;
    }
    m_planarMaskVertices = std::move(vertices);
    m_planarMaskIndices = std::move(indices);
    m_planarMaskRanges = std::move(ranges);
    m_planarMaskColors = std::move(colors);
    m_planarMaskOpacities = std::move(opacities);
    m_planarMaskBoundaryVertices = std::move(boundaryVertices);
    m_planarMaskBoundaryRanges = std::move(boundaryRanges);
    m_planarMaskBoundaryColors = std::move(boundaryColors);
    m_planarMaskUploadPending = true;
    update();
    return true;
}

void PointCloudCanvas::clearPlanarMaskOverlays() {
    if (QThread::currentThread() != thread()) return;
    m_planarMaskVertices.clear(); m_planarMaskIndices.clear(); m_planarMaskRanges.clear();
    m_planarMaskColors.clear(); m_planarMaskOpacities.clear(); m_planarMaskBoundaryVertices.clear();
    m_planarMaskBoundaryRanges.clear(); m_planarMaskBoundaryColors.clear();
    m_planarMaskUploadPending = true;
    update();
}

void PointCloudCanvas::clearPlaneResult() {
        if (QThread::currentThread() != thread()) return;
        ++m_renderRevision;
        m_pointStates.fill(NormalPoint, m_points.size());
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_contourVertices.clear();
        m_contourRanges.clear();
        m_stateUploadPending = true;
        m_contourUploadPending = true;
        clearTextureGrayOverlay();
        update();
    }

void PointCloudCanvas::setTextureGrayOverlay(const QVector<quint8> &grayscale,
                                             const QVector<quint8> &valid) {
        if (QThread::currentThread() != thread()) return;
        if (grayscale.size() != m_fullPointCount || valid.size() != m_fullPointCount) {
            qWarning() << "PointCloudCanvas rejected texture overlay size";
            return;
        }
        pcv::render::PointColorOverlay overlay;
        overlay.valid.resize(m_points.size());
        overlay.colors.resize(m_points.size());
        for (int i = 0; i < m_points.size(); ++i) {
            const int full = fullIndexForDisplayIndex(i);
            const float value = float(grayscale.at(full)) / 255.0f;
            overlay.valid[i] = valid.at(full);
            overlay.colors[i] = QVector4D(value, value, value,
                                          valid.at(full) ? 1.0f : 0.0f);
        }
        QString error;
        if (!pcv::render::validatePointColorOverlay(overlay, m_points.size(), &error)) {
            qWarning() << "PointCloudCanvas rejected texture overlay:" << error;
            return;
        }
        m_textureColors = std::move(overlay.colors);
        m_textureValid = std::move(overlay.valid);
        m_textureUploadPending = true;
        update();
    }

void PointCloudCanvas::clearTextureGrayOverlay() {
        if (QThread::currentThread() != thread()) return;
        m_textureColors.fill(QVector4D(0.0f, 0.0f, 0.0f, 0.0f), m_points.size());
        m_textureValid.fill(0, m_points.size());
        m_textureUploadPending = true;
        update();
    }

void PointCloudCanvas::setAppearance(const QColor &background, const QColor &selection) {
    if (QThread::currentThread() != thread() || !background.isValid() || !selection.isValid()) return;
    m_backgroundColor = background;
    m_selectionColor = selection;
    update();
}

void PointCloudCanvas::setEmptyMessage(const QString &message) {
    if (QThread::currentThread() != thread()) return;
    m_emptyMessage = message;
    update();
}

bool PointCloudCanvas::setPointColorOverlay(const pcv::render::PointColorOverlay &overlay) {
    if (QThread::currentThread() != thread()) return false;
    QString error;
    if (!pcv::render::validatePointColorOverlay(overlay, m_fullPointCount, &error)) {
        qWarning() << "PointCloudCanvas rejected point color overlay:" << error;
        return false;
    }
    m_textureColors.resize(m_points.size());
    m_textureValid.resize(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        const int full = fullIndexForDisplayIndex(i);
        m_textureColors[i] = overlay.colors.at(full);
        m_textureColors[i].setW(overlay.valid.at(full) ? overlay.colors.at(full).w() : 0.0f);
        m_textureValid[i] = overlay.valid.at(full);
    }
    m_textureUploadPending = true;
    update();
    return true;
}

bool PointCloudCanvas::setPointMarkerOverlay(const pcv::render::PointColorOverlay &overlay) {
    if (QThread::currentThread() != thread()) return false;
    QString error;
    if (!pcv::render::validatePointColorOverlay(overlay, m_fullPointCount, &error)) {
        qWarning() << "PointCloudCanvas rejected point marker overlay:" << error;
        return false;
    }
    m_markerColors.resize(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        const int full = fullIndexForDisplayIndex(i);
        m_markerColors[i] = overlay.colors.at(full);
        m_markerColors[i].setW(overlay.valid.at(full) ? 1.0f : 0.0f);
    }
    m_markerUploadPending = true;
    update();
    return true;
}

void PointCloudCanvas::clearPointMarkerOverlay() {
    if (QThread::currentThread() != thread()) return;
    m_markerColors.fill(QVector4D(0, 0, 0, 0), m_points.size());
    m_markerUploadPending = true;
    update();
}

void PointCloudCanvas::setWorkpieceCoordinateSystem(const pcv::render::CoordinateFrame &frame) {
        if (QThread::currentThread() != thread()) return;
        QString renderError;
        if (!pcv::render::validateCoordinateFrame(frame, &renderError)) {
            qWarning() << "PointCloudCanvas rejected coordinate frame:" << renderError;
            return;
        }
        m_workpieceCoordinate = frame;
        update();
    }

void PointCloudCanvas::clearWorkpieceCoordinateSystem() {
        if (QThread::currentThread() != thread()) return;
        m_workpieceCoordinate = {};
        update();
    }

void PointCloudCanvas::setPointSize(int size) {
        if (QThread::currentThread() != thread()) return;
        m_pointSize = qBound(1, size, 8);
        update();
    }

void PointCloudCanvas::setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax) {
        if (QThread::currentThread() != thread()) return;
        if (colorMode != 3) m_textureFallbackColorMode = qBound(0, colorMode, 2);
        m_colorMode = colorMode;
        m_overlay = float(qBound(0.0, overlay, 1.0));
        m_mapMin = float(mapMin);
        m_mapMax = float(mapMax);
        update();
}

void PointCloudCanvas::setOverlayOptions(const pcv::render::RenderOverlayOptions &options) {
        if (QThread::currentThread() != thread()) return;
        m_overlayOptions = options;
        update();
}

void PointCloudCanvas::resetView() {
        if (QThread::currentThread() != thread()) return;
        m_yaw = 0.0f;
        m_pitch = -72.0f;
        m_zoom = 1.0f;
        m_pan = QPointF();
        fitView();
    }

void PointCloudCanvas::fitView() {
    if (QThread::currentThread() != thread()) return;
    m_zoom = 1.0f;
    m_pan = QPointF();
    if (!m_points.isEmpty()) {
        float x = 0.0f, y = 0.0f;
        const float aspect = float(qMax(1, width())) / float(qMax(1, height()));
        for (int i = 0; i < 8; ++i) {
            const QVector3D corner(i & 1 ? m_maxX : m_minX,
                                   i & 2 ? m_maxY : m_minY,
                                   i & 4 ? m_maxZ : m_minZ);
            const QVector3D p = rotationTransform().mapVector((corner - m_center) * (2.0f / m_span));
            x = qMax(x, std::abs(p.x())); y = qMax(y, std::abs(p.y()));
        }
        const float xLimit = aspect >= 1.0f ? aspect : 1.0f;
        const float yLimit = aspect >= 1.0f ? 1.0f : 1.0f / aspect;
        m_zoom = qBound(0.08f, 0.82f / qMax(qMax(x / xLimit, y / yLimit), 0.0001f), 30.0f);
    }
    update();
}

void PointCloudCanvas::setStandardView(int view) {
    if (QThread::currentThread() != thread()) return;
    if (view == 0) { m_yaw = 0.0f; m_pitch = -72.0f; }
    else if (view == 2) { m_yaw = 0.0f; m_pitch = 0.0f; }
    else if (view == 3) { m_yaw = 0.0f; m_pitch = 180.0f; }
    else if (view == 4) { m_yaw = 90.0f; m_pitch = -90.0f; }
    else if (view == 5) { m_yaw = -90.0f; m_pitch = -90.0f; }
    else { m_yaw = 38.0f; m_pitch = -28.0f; }
    fitView();
}

void PointCloudCanvas::initializeGL()  {
        initializeOpenGLFunctions();
        // A failed/invalid context may legally return nullptr from glGetString.
        // Passing that pointer directly to QString::fromLatin1() caused the
        // Windows 0xFFFFFFFFFFFFFFFF read-access crash during QWidget::show().
        const auto safeGlString = [this](GLenum name) {
            const GLubyte *value = glGetString(name);
            return value ? QString::fromLatin1(reinterpret_cast<const char *>(value))
                         : QStringLiteral("<unavailable>");
        };
        const QString vendor = safeGlString(GL_VENDOR);
        const QString renderer = safeGlString(GL_RENDERER);
        const QString version = safeGlString(GL_VERSION);
        qInfo().noquote() << "OpenGL vendor:" << vendor;
        qInfo().noquote() << "OpenGL renderer:" << renderer;
        qInfo().noquote() << "OpenGL version:" << version;
        if (vendor == QStringLiteral("<unavailable>")
            || renderer == QStringLiteral("<unavailable>")) {
            m_initializationError = tr("无法创建有效的 OpenGL 上下文。请检查显卡驱动或 Qt OpenGL 部署。\n"
                                       "程序将保留界面，但暂时不绘制点云。");
            qWarning().noquote() << m_initializationError;
            return;
        }
        const QString rendererLower = renderer.toLower();
        if (rendererLower.contains(QStringLiteral("llvmpipe"))
            || rendererLower.contains(QStringLiteral("softpipe"))
            || rendererLower.contains(QStringLiteral("software"))
            || rendererLower.contains(QStringLiteral("gdi generic"))) {
            qWarning().noquote() << "OpenGL software renderer detected:" << renderer;
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Each vertex is emitted as a direct raster point.  No circle sprite,
        // antialiasing or per-point QPainter geometry is generated.
        glEnable(GL_PROGRAM_POINT_SIZE);

        static const char *vertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            layout(location = 1) in vec3 vertexNormal;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            uniform float pointSize;
            uniform float devicePixelRatio;
            uniform float minHeight;
            uniform float maxHeight;
            uniform int colorMode;
            uniform float overlay;
            uniform int renderPass;
            layout(location = 2) in uint pointState;
            layout(location = 3) in vec4 textureColor;
            layout(location = 4) in vec4 markerColor;
            out float heightValue;
            out vec3 normalValue;
            out vec4 textureValue;
            out vec4 markerValue;
            flat out int stateValue;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
                markerValue = markerColor;
                gl_PointSize = renderPass == 0
                    ? (markerColor.a > 0.5 ? max(pointSize * devicePixelRatio * 3.0, devicePixelRatio * 7.0) : pointSize)
                    : max(pointSize + 6.0, 8.0);
                normalValue = vertexNormal;
                textureValue = textureColor;
                stateValue = int(pointState);
                heightValue = clamp((vertexPosition.z - minHeight) /
                                    max(maxHeight - minHeight, 0.000001), 0.0, 1.0);
            }
        )";
        static const char *fragmentShader = R"(
            #version 330 core
            in float heightValue;
            in vec3 normalValue;
            uniform int colorMode;
            uniform int textureFallbackColorMode;
            uniform float overlay;
            uniform int renderPass;
            uniform vec3 selectionColor;
            out vec4 fragmentColor;
            flat in int stateValue;
            in vec4 textureValue;
            in vec4 markerValue;
            void main() {
                if (renderPass == 1 && distance(gl_PointCoord, vec2(0.5)) > 0.48)
                    discard;
                vec3 color;
                if (heightValue < 0.25)
                    color = mix(vec3(0.05, 0.05, 0.95), vec3(0.00, 0.85, 0.95), heightValue * 4.0);
                else if (heightValue < 0.50)
                    color = mix(vec3(0.00, 0.85, 0.95), vec3(0.10, 0.95, 0.15), (heightValue - 0.25) * 4.0);
                else if (heightValue < 0.75)
                    color = mix(vec3(0.10, 0.95, 0.15), vec3(1.00, 0.90, 0.00), (heightValue - 0.50) * 4.0);
                else
                    color = mix(vec3(1.00, 0.90, 0.00), vec3(1.00, 0.05, 0.00), (heightValue - 0.75) * 4.0);
                int activeColorMode = colorMode == 3 ? textureFallbackColorMode : colorMode;
                if (activeColorMode == 1) {
                    float gray = dot(color, vec3(0.299, 0.587, 0.114));
                    color = vec3(gray);
                } else if (activeColorMode == 2) {
                    color = normalize(abs(normalValue) + vec3(0.001));
                }
                bool hasTextureColor = colorMode == 3 && textureValue.a > 0.5;
                if (hasTextureColor)
                    color = textureValue.rgb;
                vec3 normal = length(normalValue) > 0.0001
                    ? normalize(normalValue) : vec3(0.0, 0.0, 1.0);
                float light = hasTextureColor
                    ? 1.0
                    : 0.72 + 0.28 * max(dot(normal, normalize(vec3(0.35, 0.45, 0.82))), 0.0);
                if (renderPass == 1) color = selectionColor;
                else if (markerValue.a > 0.5) {
                    float radius = distance(gl_PointCoord, vec2(0.5));
                    if (radius > 0.47) color = vec3(0.025, 0.035, 0.05);
                    else if (radius > 0.34) color = vec3(0.96, 0.98, 1.0);
                    else color = markerValue.rgb;
                }
                else if (!hasTextureColor) {
                    if (stateValue == 1) color = vec3(0.58);
                    else if (stateValue == 2) color = vec3(1.0, 0.86, 0.18);
                }
                fragmentColor = vec4(color * light, max(0.02, overlay));
            }
        )";

        static const char *pickingVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            uniform float pointSize;
            flat out uint pointId;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
                gl_PointSize = pointSize;
                pointId = uint(gl_VertexID) + 1u;
            }
        )";
        static const char *pickingFragmentShader = R"(
            #version 330 core
            flat in uint pointId;
            out vec4 fragmentColor;
            void main() {
                fragmentColor = vec4(
                    float(pointId & 255u),
                    float((pointId >> 8u) & 255u),
                    float((pointId >> 16u) & 255u),
                    float((pointId >> 24u) & 255u)) / 255.0;
            }
        )";
        static const char *contourVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
            }
        )";
        static const char *contourFragmentShader = R"(
            #version 330 core
            out vec4 fragmentColor;
            void main() { fragmentColor = vec4(1.0, 0.86, 0.18, 1.0); }
        )";
        static const char *planarMaskVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
            }
        )";
        static const char *planarMaskFragmentShader = R"(
            #version 330 core
            uniform vec4 maskColor;
            out vec4 fragmentColor;
            void main() { fragmentColor = maskColor; }
        )";

        if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
            || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)
            || !m_program.link()
            || !m_pickingProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, pickingVertexShader)
            || !m_pickingProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, pickingFragmentShader)
            || !m_pickingProgram.link()
            || !m_contourProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, contourVertexShader)
            || !m_contourProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, contourFragmentShader)
            || !m_contourProgram.link()
            || !m_planarMaskProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, planarMaskVertexShader)
            || !m_planarMaskProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, planarMaskFragmentShader)
            || !m_planarMaskProgram.link()) {
            m_initializationError = tr("OpenGL 着色器初始化失败\n主渲染：%1\n拾取：%2\n轮廓：%3\n平面掩码：%4")
                                        .arg(m_program.log(), m_pickingProgram.log(),
                                             m_contourProgram.log(), m_planarMaskProgram.log());
            qWarning().noquote() << m_initializationError;
            return;
        }

        if (!m_vertexArray.create() || !m_vertexBuffer.create() || !m_stateBuffer.create()
            || !m_textureBuffer.create() || !m_markerBuffer.create()
            || !m_contourVertexArray.create() || !m_contourBuffer.create()
            || !m_planarMaskVertexArray.create() || !m_planarMaskVertexBuffer.create()
            || !m_planarMaskIndexBuffer.create()) {
            m_initializationError = tr("无法创建 OpenGL 顶点缓冲区");
            return;
        }
        m_uploadPending = true;
    }

void PointCloudCanvas::paintGL()  {
        glClearColor(float(m_backgroundColor.redF()), float(m_backgroundColor.greenF()),
                     float(m_backgroundColor.blueF()), 1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_initializationError.isEmpty() && context() && context()->isValid()
            && !m_points.isEmpty()) {
                uploadCloudIfNeeded();
            if (m_uploadError.isEmpty()) {
                uploadStatesIfNeeded();
                uploadTextureIfNeeded();
                uploadMarkersIfNeeded();
                uploadPlanarMasksIfNeeded();
                uploadContoursIfNeeded();
                const QMatrix4x4 transform = viewTransform();
                m_program.bind();
                m_program.setUniformValue("transform", transform);
                m_program.setUniformValue("cloudCenter", m_center);
                m_program.setUniformValue("cloudSpan", m_span);
                m_program.setUniformValue("viewPan", QVector2D(m_pan));
                m_program.setUniformValue("pointSize", float(m_pointSize));
                m_program.setUniformValue("devicePixelRatio", float(devicePixelRatioF()));
                const float lower = m_mapMin;
                const float upper = m_mapMax;
                m_program.setUniformValue("minHeight", lower);
                m_program.setUniformValue("maxHeight", upper);
                m_program.setUniformValue("colorMode", m_colorMode);
                m_program.setUniformValue("textureFallbackColorMode", m_textureFallbackColorMode);
                m_program.setUniformValue("overlay", m_overlay);
                m_program.setUniformValue("renderPass", 0);
                m_program.setUniformValue("selectionColor", QVector3D(float(m_selectionColor.redF()),
                    float(m_selectionColor.greenF()), float(m_selectionColor.blueF())));
                m_vertexArray.bind();
                glDrawArrays(GL_POINTS, 0, int(m_points.size()));
                m_lastPaintedRevision = m_renderRevision;
                if (!m_selectedIndices.isEmpty()) {
                    glDepthFunc(GL_LEQUAL);
                    m_program.setUniformValue("renderPass", 1);
                    for (int index : m_selectedIndices) {
                        if (index >= 0 && index < m_points.size())
                            glDrawArrays(GL_POINTS, index, 1);
                    }
                    glDepthFunc(GL_LESS);
                }
                if (!m_selectedEdgeIndices.isEmpty()) {
                    glDepthFunc(GL_LEQUAL);
                    m_program.setUniformValue("renderPass", 1);
                    for (int index : m_selectedEdgeIndices) {
                        if (index >= 0 && index < m_points.size())
                            glDrawArrays(GL_POINTS, index, 1);
                    }
                    glDepthFunc(GL_LESS);
                }
                m_vertexArray.release();
                m_program.release();
                if (!m_contourRanges.isEmpty()) {
                    m_contourProgram.bind();
                    m_contourProgram.setUniformValue("transform", transform);
                    m_contourProgram.setUniformValue("cloudCenter", m_center);
                    m_contourProgram.setUniformValue("cloudSpan", m_span);
                    m_contourProgram.setUniformValue("viewPan", QVector2D(m_pan));
                    m_contourVertexArray.bind();
                    glDepthFunc(GL_LEQUAL);
                    glLineWidth(2.0f);
                    for (const auto &range : m_contourRanges)
                        glDrawArrays(GL_LINE_STRIP, range.first, range.second);
                    glLineWidth(1.0f);
                    glDepthFunc(GL_LESS);
                    m_contourVertexArray.release();
                    m_contourProgram.release();
                }
                if (!m_planarMaskRanges.isEmpty() || !m_planarMaskBoundaryRanges.isEmpty()) {
                    m_planarMaskProgram.bind();
                    m_planarMaskProgram.setUniformValue("transform", transform);
                    m_planarMaskProgram.setUniformValue("cloudCenter", m_center);
                    m_planarMaskProgram.setUniformValue("cloudSpan", m_span);
                    m_planarMaskProgram.setUniformValue("viewPan", QVector2D(m_pan));
                    m_planarMaskVertexArray.bind();
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(-1.0f, -1.0f);
                    for (int i = 0; i < m_planarMaskRanges.size(); ++i) {
                        const auto range = m_planarMaskRanges.at(i);
                        const auto color = m_planarMaskColors.at(i);
                        m_planarMaskProgram.setUniformValue("maskColor", QVector4D(color.x(), color.y(), color.z(), m_planarMaskOpacities.at(i)));
                        glDrawElements(GL_TRIANGLES, range.second, GL_UNSIGNED_INT,
                            reinterpret_cast<const void *>(quintptr(range.first) * sizeof(quint32)));
                    }
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glLineWidth(float(qMax(1.0, devicePixelRatioF() * 1.5)));
                    for (int i = 0; i < m_planarMaskBoundaryRanges.size(); ++i) {
                        const auto range = m_planarMaskBoundaryRanges.at(i);
                        const auto color = m_planarMaskBoundaryColors.at(i);
                        m_planarMaskProgram.setUniformValue("maskColor", QVector4D(color.x(), color.y(), color.z(), 1.0f));
                        glDrawArrays(GL_LINE_STRIP, m_planarMaskVertices.size() + range.first, range.second);
                    }
                    glLineWidth(1.0f);
                    glDepthMask(GL_TRUE);
                    glDepthFunc(GL_LESS);
                    m_planarMaskVertexArray.release();
                    m_planarMaskProgram.release();
                }
            }
        }

        if (m_overlayWidget) m_overlayWidget->update();
    }

void PointCloudCanvas::mousePressEvent(QMouseEvent *event)  {
        m_lastMousePosition = event->position();
        m_selectionOrigin = event->position();
        m_pressedButton = event->button();
        m_mouseMoved = false;
    }

void PointCloudCanvas::mouseMoveEvent(QMouseEvent *event)  {
        const QPointF delta = event->position() - m_lastMousePosition;
        if (std::abs(delta.x()) + std::abs(delta.y()) > 1.0) m_mouseMoved = true;
        m_lastMousePosition = event->position();
        if (m_pressedButton == Qt::LeftButton && !m_selectionMode && !m_edgeSelectionMode) {
            m_yaw += float(delta.x()) * 0.45f;
            // Do not clamp pitch at +/-89 degrees: a 2.5D plane can be
            // inspected from every orientation, including upside down.
            m_pitch = std::fmod(m_pitch + float(delta.y()) * 0.45f, 360.0f);
            if (m_pitch < 0.0f) m_pitch += 360.0f;
        } else if (m_pressedButton == Qt::RightButton) {
            m_pan += QPointF(2.0 * delta.x() / qMax(1, width()),
                             -2.0 * delta.y() / qMax(1, height()));
        }
        update();
    }

void PointCloudCanvas::mouseReleaseEvent(QMouseEvent *event)  {
        if (!m_selectionMode && !m_edgeSelectionMode && !m_mouseMoved
            && event->button() == Qt::LeftButton && m_overlayOptions.showOrientation
            && QRect(width() - 80, 10, 68, 68).contains(event->position().toPoint())) {
            const QPointF delta = event->position() - QPointF(width() - 46, 44);
            if (std::hypot(delta.x(), delta.y()) < 14) setStandardView(0);
            else if (std::abs(delta.x()) > std::abs(delta.y())) setStandardView(delta.x() < 0 ? 4 : 5);
            else setStandardView(delta.y() < 0 ? 2 : 3);
            m_pressedButton = Qt::NoButton;
            return;
        }
        if (m_edgeSelectionMode && event->button() == Qt::LeftButton && m_mouseMoved
            && edgeRectanglePicked) {
            edgeRectanglePicked(QRectF(m_selectionOrigin, event->position()).normalized());
        } else if (event->button() == Qt::LeftButton && !m_mouseMoved) {
            if (pointPickingStarted) pointPickingStarted();
            const int index = pickPoint(event->position());
            if (index >= 0) {
                m_coordinatePointIndex = displayIndexForFullIndex(index);
                update();
            }
            if (pointPicked) pointPicked(index);
        }
        m_pressedButton = Qt::NoButton;
    }

void PointCloudCanvas::resizeGL(int width, int height)  {
        Q_UNUSED(width)
        Q_UNUSED(height)
        m_pickingFbo.reset();
        if (m_overlayWidget) { m_overlayWidget->setGeometry(rect()); m_overlayWidget->raise(); }
    }

void PointCloudCanvas::wheelEvent(QWheelEvent *event)  {
        m_zoom = qBound(0.08f,
                        m_zoom * (event->angleDelta().y() > 0 ? 1.15f : 0.87f),
                        30.0f);
        update();
    }

void PointCloudCanvas::validateRenderSnapshot() {
        pcv::render::RenderSnapshot snapshot;
        snapshot.points = m_points;
        snapshot.states.resize(m_pointStates.size());
        for (int i = 0; i < m_pointStates.size(); ++i) {
            snapshot.states[i] = m_pointStates[i] == PlanePoint
                ? pcv::render::PointState::Plane
                : (m_pointStates[i] == EdgePoint
                    ? pcv::render::PointState::Edge
                    : pcv::render::PointState::Normal);
        }
        snapshot.revision = m_renderRevision;
        QString error;
        if (!pcv::render::validateSnapshot(snapshot, &error)) {
            m_uploadError = error;
            qWarning() << "PointCloudCanvas render snapshot rejected:" << error;
        }
    }

void PointCloudCanvas::updateBounds() {
        if (m_points.isEmpty()) {
            m_center = QVector3D();
            m_span = 1.0f;
            m_minX = 0.0f;
            m_maxX = 0.0f;
            m_minY = 0.0f;
            m_maxY = 0.0f;
            m_spanX = 0.0f;
            m_spanY = 0.0f;
            m_spanZ = 0.0f;
            m_minZ = 0.0f;
            m_maxZ = 1.0f;
            return;
        }

        m_minX = m_points.first().x;
        m_maxX = m_minX;
        m_minY = m_points.first().y;
        m_maxY = m_minY;
        m_minZ = m_points.first().z;
        m_maxZ = m_minZ;
        for (const pointcloud::Point3D &point : m_points) {
            m_minX = std::min(m_minX, point.x);
            m_maxX = std::max(m_maxX, point.x);
            m_minY = std::min(m_minY, point.y);
            m_maxY = std::max(m_maxY, point.y);
            m_minZ = std::min(m_minZ, point.z);
            m_maxZ = std::max(m_maxZ, point.z);
        }
        m_center = QVector3D((m_minX + m_maxX) * 0.5f,
                             (m_minY + m_maxY) * 0.5f,
                             (m_minZ + m_maxZ) * 0.5f);
        m_spanX = m_maxX - m_minX;
        m_spanY = m_maxY - m_minY;
        m_spanZ = m_maxZ - m_minZ;
        m_span = std::max({m_spanX, m_spanY, m_spanZ, 0.000001f});
        m_mapMin = m_minZ;
        m_mapMax = m_maxZ > m_minZ ? m_maxZ : m_minZ + 1.0f;
    }

void PointCloudCanvas::uploadCloudIfNeeded() {
        if (!m_uploadPending) return;
        if (m_points.size() > std::numeric_limits<int>::max() / qsizetype(sizeof(pointcloud::Point3D))) {
            m_uploadError = tr("点云过大，超过单个 OpenGL 缓冲区限制");
            qCritical() << "OpenGL upload rejected, points=" << m_points.size()
                        << "bytes="
                        << (m_points.size() * qsizetype(sizeof(pointcloud::Point3D)));
            m_uploadPending = false;
            return;
        }

        const qsizetype uploadBytes =
            m_points.size() * qsizetype(sizeof(pointcloud::Point3D));
        qInfo() << "OpenGL cloud upload started, points=" << m_points.size()
                << "bytes=" << uploadBytes;
        m_vertexArray.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.setUsagePattern(QOpenGLBuffer::StreamDraw);
        while (glGetError() != GL_NO_ERROR) {}
        const qsizetype requiredBytes = m_points.size() * qsizetype(sizeof(pointcloud::Point3D));
        if (requiredBytes > m_vertexBufferCapacityBytes) {
            const qsizetype grown = qMax(requiredBytes, qMax<qsizetype>(4096, m_vertexBufferCapacityBytes * 2));
            m_vertexBuffer.allocate(static_cast<int>(grown));
            m_vertexBufferCapacityBytes = grown;
        }
        if (requiredBytes > 0)
            m_vertexBuffer.write(0, m_points.constData(), int(requiredBytes));
        const GLenum uploadError = glGetError();
        if (uploadError != GL_NO_ERROR) {
            m_uploadError = uploadError == GL_OUT_OF_MEMORY
                ? tr("显存不足，无法全量上传当前点云")
                : tr("点云上传至显卡失败（OpenGL 错误 %1）").arg(uploadError);
            qCritical().noquote() << m_uploadError
                                  << "points=" << m_points.size()
                                  << "bytes=" << uploadBytes;
            m_uploadPending = false;
        } else {
            m_program.bind();
            m_program.enableAttributeArray(0);
            m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(pointcloud::Point3D));
            m_program.enableAttributeArray(1);
            m_program.setAttributeBuffer(1, GL_FLOAT, int(3 * sizeof(float)), 3,
                                         sizeof(pointcloud::Point3D));
            m_stateBuffer.bind();
            m_stateBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            const qsizetype stateBytes = m_pointStates.size();
            if (stateBytes > m_stateBufferCapacityBytes) {
                const qsizetype grown = qMax(stateBytes, qMax<qsizetype>(1024, m_stateBufferCapacityBytes * 2));
                m_stateBuffer.allocate(static_cast<int>(grown));
                m_stateBufferCapacityBytes = grown;
            }
            if (stateBytes > 0)
                m_stateBuffer.write(0, m_pointStates.constData(), int(stateBytes));
            glEnableVertexAttribArray(2);
            glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(quint8), nullptr);
            m_stateBuffer.release();
            m_textureBuffer.bind();
            m_textureBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            const qsizetype textureBytes = m_textureColors.size()
                * qsizetype(sizeof(QVector4D));
            if (textureBytes > m_textureBufferCapacityBytes) {
                const qsizetype grown = qMax(textureBytes,
                    qMax<qsizetype>(1024, m_textureBufferCapacityBytes * 2));
                m_textureBuffer.allocate(static_cast<int>(grown));
                m_textureBufferCapacityBytes = grown;
            }
            if (textureBytes > 0)
                m_textureBuffer.write(0, m_textureColors.constData(), int(textureBytes));
            m_program.bind();
            m_program.enableAttributeArray(3);
            m_program.setAttributeBuffer(3, GL_FLOAT, 0, 4, sizeof(QVector4D));
            m_markerBuffer.bind();
            m_markerBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            const qsizetype markerBytes = m_markerColors.size() * qsizetype(sizeof(QVector4D));
            if (markerBytes > m_markerBufferCapacityBytes) {
                const qsizetype grown = qMax(markerBytes,
                    qMax<qsizetype>(1024, m_markerBufferCapacityBytes * 2));
                m_markerBuffer.allocate(static_cast<int>(grown));
                m_markerBufferCapacityBytes = grown;
            }
            if (markerBytes > 0) m_markerBuffer.write(0, m_markerColors.constData(), int(markerBytes));
            m_program.enableAttributeArray(4);
            m_program.setAttributeBuffer(4, GL_FLOAT, 0, 4, sizeof(QVector4D));
            m_markerBuffer.release();
            m_program.release();
            m_textureBuffer.release();
            m_program.release();
            m_uploadPending = false;
            m_stateUploadPending = false;
            m_textureUploadPending = false;
            m_markerUploadPending = false;
            qInfo() << "OpenGL cloud upload finished, points=" << m_points.size()
                    << "bytes=" << uploadBytes;
        }
        m_vertexBuffer.release();
        m_vertexArray.release();
    }

void PointCloudCanvas::uploadStatesIfNeeded() {
        if (!m_stateUploadPending || !m_stateBuffer.isCreated()) return;
        m_vertexArray.bind();
        m_stateBuffer.bind();
        const qsizetype stateBytes = m_pointStates.size();
        if (stateBytes > m_stateBufferCapacityBytes) {
            const qsizetype grown = qMax(stateBytes, qMax<qsizetype>(1024, m_stateBufferCapacityBytes * 2));
            m_stateBuffer.allocate(static_cast<int>(grown));
            m_stateBufferCapacityBytes = grown;
        }
        if (stateBytes > 0)
            m_stateBuffer.write(0, m_pointStates.constData(), int(stateBytes));
        m_stateBuffer.release();
        m_vertexArray.release();
        m_stateUploadPending = false;
    }

void PointCloudCanvas::uploadTextureIfNeeded() {
        if (!m_textureUploadPending || !m_textureBuffer.isCreated()) return;
        m_vertexArray.bind();
        m_textureBuffer.bind();
        m_textureBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        const qsizetype textureBytes = m_textureColors.size()
            * qsizetype(sizeof(QVector4D));
        if (textureBytes > m_textureBufferCapacityBytes) {
            const qsizetype grown = qMax(textureBytes,
                qMax<qsizetype>(1024, m_textureBufferCapacityBytes * 2));
            m_textureBuffer.allocate(static_cast<int>(grown));
            m_textureBufferCapacityBytes = grown;
        }
        if (textureBytes > 0)
            m_textureBuffer.write(0, m_textureColors.constData(), int(textureBytes));
        m_program.bind();
        m_program.enableAttributeArray(3);
        m_program.setAttributeBuffer(3, GL_FLOAT, 0, 4, sizeof(QVector4D));
        m_program.release();
        m_textureBuffer.release();
        m_vertexArray.release();
        m_textureUploadPending = false;
    }

void PointCloudCanvas::uploadContoursIfNeeded() {
        if (!m_contourUploadPending || !m_contourBuffer.isCreated()) return;
        m_contourVertexArray.bind();
        m_contourBuffer.bind();
        m_contourBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        m_contourBuffer.allocate(m_contourVertices.constData(),
            int(m_contourVertices.size() * qsizetype(sizeof(pointcloud::Point3D))));
        m_contourProgram.bind();
        m_contourProgram.enableAttributeArray(0);
        m_contourProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3,
                                             sizeof(pointcloud::Point3D));
        m_contourProgram.release();
        m_contourBuffer.release();
        m_contourVertexArray.release();
        m_contourUploadPending = false;
    }

int PointCloudCanvas::pickPoint(const QPointF &position) {
        if (QThread::currentThread() != thread() || !m_initializationError.isEmpty()
            || m_points.isEmpty() || !context() || !isValid()) return -1;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return -1;
        uploadCloudIfNeeded();
        if (!m_uploadError.isEmpty()) { doneCurrent(); return -1; }
        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(width() * dpr)),
                              qMax(1, qRound(height() * dpr)));
        if (!m_pickingFbo || m_pickingFbo->size() != pixelSize) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::Depth);
            format.setInternalTextureFormat(GL_RGBA8);
            m_pickingFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
        }
        if (!m_pickingFbo || !m_pickingFbo->isValid()) { doneCurrent(); return -1; }
        m_pickingFbo->bind();
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND);
        glDisable(GL_DITHER);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_pickingProgram.bind();
        m_pickingProgram.setUniformValue("transform", viewTransform());
        m_pickingProgram.setUniformValue("cloudCenter", m_center);
        m_pickingProgram.setUniformValue("cloudSpan", m_span);
        m_pickingProgram.setUniformValue("viewPan", QVector2D(m_pan));
        m_pickingProgram.setUniformValue("pointSize", float(m_pointSize));
        m_vertexArray.bind();
        glDrawArrays(GL_POINTS, 0, int(m_points.size()));
        m_vertexArray.release();
        m_pickingProgram.release();

        const int centerX = qBound(0, int(std::floor(position.x() * dpr)),
                                   pixelSize.width() - 1);
        const int centerY = qBound(0, pixelSize.height() - 1
                                   - int(std::floor(position.y() * dpr)),
                                   pixelSize.height() - 1);
        quint8 pixels[3 * 3 * 4] = {};
        float depths[3 * 3] = {};
        const int x = qBound(0, centerX - 1, qMax(0, pixelSize.width() - 3));
        const int y = qBound(0, centerY - 1, qMax(0, pixelSize.height() - 3));
        const int readWidth = qMin(3, pixelSize.width());
        const int readHeight = qMin(3, pixelSize.height());
        glReadPixels(x, y, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glReadPixels(x, y, readWidth, readHeight, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glClearColor(float(m_backgroundColor.redF()), float(m_backgroundColor.greenF()),
                     float(m_backgroundColor.blueF()), 1.0f);
        glEnable(GL_DITHER);
        glEnable(GL_BLEND);
        doneCurrent();
        const int preferredX = centerX - x, preferredY = centerY - y;
        auto decode = [&](int px, int py) -> quint32 {
            if (px < 0 || py < 0 || px >= readWidth || py >= readHeight) return 0;
            const quint8 *rgba = pixels + (py * readWidth + px) * 4;
            return quint32(rgba[0]) | (quint32(rgba[1]) << 8)
                | (quint32(rgba[2]) << 16) | (quint32(rgba[3]) << 24);
        };
        quint32 id = decode(preferredX, preferredY);
        if (id == 0) {
            int bestDistanceSquared = std::numeric_limits<int>::max();
            float bestDepth = 1.0f;
            for (int py = 0; py < readHeight; ++py) {
                for (int px = 0; px < readWidth; ++px) {
                    const quint32 candidate = decode(px, py);
                    if (candidate == 0) continue;
                    const int dx = px - preferredX;
                    const int dy = py - preferredY;
                    const int distanceSquared = dx * dx + dy * dy;
                    const float depth = depths[py * readWidth + px];
                    if (distanceSquared < bestDistanceSquared
                        || (distanceSquared == bestDistanceSquared && depth < bestDepth)) {
                        id = candidate;
                        bestDistanceSquared = distanceSquared;
                        bestDepth = depth;
                    }
                }
            }
        }
        return id > 0 && id <= quint32(m_points.size())
            ? fullIndexForDisplayIndex(int(id - 1)) : -1;
    }

QVector<int> PointCloudCanvas::pickRectangle(const QRectF &selection) {
        QVector<int> result;
        if (QThread::currentThread() != thread() || !m_initializationError.isEmpty()
            || m_points.isEmpty() || !context() || !isValid()) return result;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return result;
        uploadCloudIfNeeded();
        if (!m_uploadError.isEmpty()) { doneCurrent(); return result; }
        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
        if (!m_pickingFbo || m_pickingFbo->size() != pixelSize) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::Depth);
            format.setInternalTextureFormat(GL_RGBA8);
            m_pickingFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
        }
        if (!m_pickingFbo || !m_pickingFbo->isValid()) { doneCurrent(); return result; }
        const QRectF clipped = selection.intersected(rect());
        const int x0 = qBound(0, qFloor(clipped.left() * dpr), pixelSize.width() - 1);
        const int x1 = qBound(0, qCeil(clipped.right() * dpr), pixelSize.width() - 1);
        const int top = qBound(0, qFloor(clipped.top() * dpr), pixelSize.height() - 1);
        const int bottom = qBound(0, qCeil(clipped.bottom() * dpr), pixelSize.height() - 1);
        const int readWidth = qMax(1, x1 - x0 + 1);
        const int readHeight = qMax(1, bottom - top + 1);
        const int y0 = pixelSize.height() - 1 - bottom;
        m_pickingFbo->bind();
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND); glDisable(GL_DITHER); glEnable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_pickingProgram.bind();
        m_pickingProgram.setUniformValue("transform", viewTransform());
        m_pickingProgram.setUniformValue("cloudCenter", m_center);
        m_pickingProgram.setUniformValue("cloudSpan", m_span);
        m_pickingProgram.setUniformValue("viewPan", QVector2D(m_pan));
        m_pickingProgram.setUniformValue("pointSize", float(m_pointSize));
        m_vertexArray.bind(); glDrawArrays(GL_POINTS, 0, int(m_points.size()));
        m_vertexArray.release(); m_pickingProgram.release();
        QByteArray pixels(readWidth * readHeight * 4, Qt::Uninitialized);
        glReadPixels(x0, y0, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glClearColor(float(m_backgroundColor.redF()), float(m_backgroundColor.greenF()),
                     float(m_backgroundColor.blueF()), 1.0f);
        glEnable(GL_DITHER); glEnable(GL_BLEND); doneCurrent();
        QSet<int> unique;
        for (int i = 0; i < readWidth * readHeight; ++i) {
            const uchar *rgba = reinterpret_cast<const uchar *>(pixels.constData() + i * 4);
            const quint32 id = quint32(rgba[0]) | (quint32(rgba[1]) << 8)
                | (quint32(rgba[2]) << 16) | (quint32(rgba[3]) << 24);
            if (id > 0 && id <= quint32(m_points.size())) unique.insert(int(id - 1));
        }
        for (int display : unique)
            result.push_back(fullIndexForDisplayIndex(display));
        std::sort(result.begin(), result.end());
        return result;
    }

int PointCloudCanvas::displayIndexForFullIndex(int fullIndex) const {
    if (!m_hasDisplayMapping)
        return fullIndex >= 0 && fullIndex < m_points.size() ? fullIndex : -1;
    return m_fullToDisplayIndices.value(fullIndex, -1);
}

void PointCloudCanvas::uploadPlanarMasksIfNeeded() {
    if (!m_planarMaskUploadPending || !m_planarMaskVertexBuffer.isCreated()) return;
    m_planarMaskVertexArray.bind();
    m_planarMaskVertexBuffer.bind();
    m_planarMaskVertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    const qsizetype vertexBytes = m_planarMaskVertices.size() * qsizetype(sizeof(QVector3D));
    const qsizetype boundaryBytes = m_planarMaskBoundaryVertices.size() * qsizetype(sizeof(QVector3D));
    m_planarMaskVertexBuffer.allocate(int(vertexBytes + boundaryBytes));
    if (vertexBytes) m_planarMaskVertexBuffer.write(0, m_planarMaskVertices.constData(), int(vertexBytes));
    if (boundaryBytes) m_planarMaskVertexBuffer.write(int(vertexBytes), m_planarMaskBoundaryVertices.constData(), int(boundaryBytes));
    m_planarMaskProgram.bind();
    m_planarMaskProgram.enableAttributeArray(0);
    m_planarMaskProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    m_planarMaskIndexBuffer.bind();
    m_planarMaskIndexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_planarMaskIndexBuffer.allocate(m_planarMaskIndices.constData(), int(m_planarMaskIndices.size() * qsizetype(sizeof(quint32))));
    m_planarMaskProgram.release();
    m_planarMaskVertexBuffer.release();
    // Keep the element buffer attached to this VAO for core-profile draws.
    m_planarMaskVertexArray.release();
    m_planarMaskUploadPending = false;
}

void PointCloudCanvas::uploadMarkersIfNeeded() {
    if (!m_markerUploadPending || !m_markerBuffer.isCreated()) return;
    m_vertexArray.bind();
    m_markerBuffer.bind();
    m_markerBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    const qsizetype markerBytes = m_markerColors.size() * qsizetype(sizeof(QVector4D));
    if (markerBytes > m_markerBufferCapacityBytes) {
        const qsizetype grown = qMax(markerBytes,
            qMax<qsizetype>(1024, m_markerBufferCapacityBytes * 2));
        m_markerBuffer.allocate(static_cast<int>(grown));
        m_markerBufferCapacityBytes = grown;
    }
    if (markerBytes > 0) m_markerBuffer.write(0, m_markerColors.constData(), int(markerBytes));
    m_program.bind();
    m_program.enableAttributeArray(4);
    m_program.setAttributeBuffer(4, GL_FLOAT, 0, 4, sizeof(QVector4D));
    m_program.release();
    m_markerBuffer.release();
    m_vertexArray.release();
    m_markerUploadPending = false;
}

int PointCloudCanvas::fullIndexForDisplayIndex(int displayIndex) const {
    if (displayIndex < 0 || displayIndex >= m_points.size()) return -1;
    return m_hasDisplayMapping ? m_displayToFullIndices.at(displayIndex) : displayIndex;
}

QMatrix4x4 PointCloudCanvas::rotationTransform() const {
        QMatrix4x4 rotation;
        rotation.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
        rotation.rotate(m_yaw, 0.0f, 0.0f, 1.0f);
        return rotation;
    }

QMatrix4x4 PointCloudCanvas::viewTransform() const {
        const float aspect = float(qMax(1, width())) / float(qMax(1, height()));
        QMatrix4x4 projection;
        if (aspect >= 1.0f)
            projection.ortho(-aspect / m_zoom, aspect / m_zoom,
                             -1.0f / m_zoom, 1.0f / m_zoom, -4.0f, 4.0f);
        else
            projection.ortho(-1.0f / m_zoom, 1.0f / m_zoom,
                             -1.0f / (aspect * m_zoom), 1.0f / (aspect * m_zoom),
                             -4.0f, 4.0f);
        return projection * rotationTransform();
    }

void PointCloudCanvas::drawOverlay(QPaintDevice *device) {
        QPainter painter;
        if (!painter.begin(device)) return;
        painter.setRenderHint(QPainter::Antialiasing);
        const auto project = [this](const QVector3D &world) {
            const QVector3D normalized = (world - m_center) * (2.0f / m_span);
            QVector4D clip = viewTransform() * QVector4D(normalized, 1.0f);
            clip.setX(clip.x() + float(m_pan.x()) * clip.w());
            clip.setY(clip.y() + float(m_pan.y()) * clip.w());
            if (std::abs(clip.w()) > 1.0e-9f) clip /= clip.w();
            return QPointF((clip.x() * 0.5f + 0.5f) * width(),
                           (0.5f - clip.y() * 0.5f) * height());
        };
        QVector<QRectF> axisLabelRects;
        const auto drawAxisLabel = [&](const QPointF &anchor, const QString &text,
                                       const QPointF &offset, const QColor &color) {
            const QFontMetricsF metrics(painter.font());
            QRectF labelRect(QPointF(), metrics.size(Qt::TextSingleLine, text));
            labelRect.moveTopLeft(anchor + offset);
            if (!rect().contains(labelRect.toAlignedRect())) return;
            for (const QRectF &placed : axisLabelRects) {
                if (placed.intersects(labelRect)) return;
            }
            axisLabelRects.push_back(labelRect);
            painter.fillRect(labelRect.adjusted(-3.0, -2.0, 3.0, 2.0), QColor(8, 13, 20, 210));
            painter.setPen(color);
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        };
        if (!m_points.isEmpty() && m_span > 1.0e-9f) {
            const pcv::render::internal::OverlayBounds bounds = {
                m_minX, m_maxX, m_minY, m_maxY, m_minZ, m_maxZ};
            const pcv::render::internal::OverlayGeometry geometry =
                pcv::render::internal::makeOverlayGeometry(bounds);
            const bool drawBoundingAxes = m_overlayOptions.showBoundingBox
                && m_overlayOptions.showCoordinateAxes;
            const bool drawXAxis = drawBoundingAxes && geometry.axes[0].valid;
            if (m_overlayOptions.showFloorGrid) {
                painter.setPen(QPen(QColor(110, 130, 155, 105), 1.0));
                if (m_overlayOptions.floorZUp) {
                    for (int i = 0; i <= pcv::render::internal::kOverlayGridDivisions; ++i) {
                        const float t = float(i) / pcv::render::internal::kOverlayGridDivisions;
                        painter.drawLine(project({bounds.minX + (bounds.maxX - bounds.minX) * t,
                                                  bounds.minY, bounds.minZ}),
                                         project({bounds.minX + (bounds.maxX - bounds.minX) * t,
                                                  bounds.maxY, bounds.minZ}));
                        // This lower horizontal line is the selected X-axis edge.
                        // Leave it to the color pass so it is never a grey double line.
                        if (!(drawXAxis && i == 0)) {
                            painter.drawLine(project({bounds.minX,
                                                      bounds.minY + (bounds.maxY - bounds.minY) * t,
                                                      bounds.minZ}),
                                             project({bounds.maxX,
                                                      bounds.minY + (bounds.maxY - bounds.minY) * t,
                                                      bounds.minZ}));
                        }
                    }
                } else {
                    for (int i = 0; i <= pcv::render::internal::kOverlayGridDivisions; ++i) {
                        const float t = float(i) / pcv::render::internal::kOverlayGridDivisions;
                        painter.drawLine(project({bounds.minX + (bounds.maxX - bounds.minX) * t,
                                                  bounds.minY, bounds.minZ}),
                                         project({bounds.minX + (bounds.maxX - bounds.minX) * t,
                                                  bounds.minY, bounds.maxZ}));
                        if (!(drawXAxis && i == 0)) {
                            painter.drawLine(project({bounds.minX, bounds.minY,
                                                      bounds.minZ + (bounds.maxZ - bounds.minZ) * t}),
                                             project({bounds.maxX, bounds.minY,
                                                      bounds.minZ + (bounds.maxZ - bounds.minZ) * t}));
                        }
                    }
                }
            }
            if (m_overlayOptions.showBoundingBox) {
                painter.setPen(QPen(QColor(175, 190, 210, 190), 1.2));
                for (const pcv::render::internal::OverlaySegment &edge : geometry.boxEdges) {
                    if (!edge.valid) continue;
                    if (drawBoundingAxes && pcv::render::internal::isAxisSegment(edge, geometry))
                        continue;
                    painter.drawLine(project(edge.start), project(edge.end));
                }

                if (drawBoundingAxes) {
                    static const std::array<QColor, 3> axisColors = {
                        QColor(244, 92, 92), QColor(78, 218, 132), QColor(76, 156, 255)};
                    static const std::array<QString, 3> axisNames = {
                        QStringLiteral("X(mm)"), QStringLiteral("Y(mm)"), QStringLiteral("Z(mm)")};
                    const std::array<std::pair<float, float>, 3> axisRanges = {{
                        {bounds.minX, bounds.maxX}, {bounds.minY, bounds.maxY},
                        {bounds.minZ, bounds.maxZ}}};
                    QFont axisFont = painter.font();
                    axisFont.setPointSize(12);
                    axisFont.setBold(true);
                    painter.setFont(axisFont);
                    for (int axisIndex = 0; axisIndex < int(geometry.axes.size()); ++axisIndex) {
                        const pcv::render::internal::OverlaySegment &axis = geometry.axes[axisIndex];
                        if (!axis.valid) continue;
                        const QPointF start = project(axis.start);
                        const QPointF end = project(axis.end);
                        painter.setPen(QPen(axisColors[axisIndex], 2.5));
                        painter.drawLine(start, end);
                        if (!m_overlayOptions.showAxisLabels) continue;

                        const QPointF axisVector = end - start;
                        const qreal projectedLength = std::hypot(axisVector.x(), axisVector.y());
                        if (projectedLength < 8.0) continue;
                        const QPointF normal(-axisVector.y() / projectedLength,
                                             axisVector.x() / projectedLength);
                        drawAxisLabel(end, axisNames[axisIndex], normal * -17.0 + QPointF(5.0, -4.0),
                                      axisColors[axisIndex]);
                        const QVector<pcv::render::internal::OverlayTick> ticks =
                            pcv::render::internal::makeAxisLabelTicks(
                                axisRanges[axisIndex].first, axisRanges[axisIndex].second);
                        painter.setPen(QPen(axisColors[axisIndex], 1.0));
                        for (const pcv::render::internal::OverlayTick &tick : ticks) {
                            const QVector3D world = axis.start + (axis.end - axis.start) * tick.normalized;
                            const QPointF position = project(world);
                            painter.drawLine(position - normal * 3.0, position + normal * 3.0);
                            drawAxisLabel(position, QString::number(tick.roundedValue),
                                          normal * 7.0 + QPointF(4.0, -4.0), axisColors[axisIndex]);
                        }
                    }
                }
            }
            if (m_overlayOptions.showNormals) {
                const qsizetype stride = qMax<qsizetype>(1, m_points.size() / 2000);
                const float normalLength = qMax(0.001f, m_span * 0.018f);
                painter.setPen(QPen(QColor(255, 205, 105, 190), 1.0));
                for (qsizetype i = 0; i < m_points.size(); i += stride) {
                    const pointcloud::Point3D &point = m_points[i];
                    const float squared = point.nx * point.nx + point.ny * point.ny
                        + point.nz * point.nz;
                    if (!std::isfinite(squared) || squared < 1.0e-8f) continue;
                    const float inverseLength = 1.0f / std::sqrt(squared);
                    const QVector3D start(point.x, point.y, point.z);
                    const QVector3D end = start + QVector3D(point.nx, point.ny, point.nz)
                        * (inverseLength * normalLength);
                    painter.drawLine(project(start), project(end));
                }
            }
        }
        if (m_edgeSelectionMode && m_mouseMoved && m_pressedButton == Qt::LeftButton) {
            painter.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
            painter.setBrush(QColor(90, 210, 255, 35));
            painter.drawRect(QRectF(m_selectionOrigin, m_lastMousePosition).normalized());
        }
        painter.setPen(m_backgroundColor.lightnessF() > 0.5
                           ? QColor(102, 120, 141) : QColor(231, 237, 246));
        QFont titleFont = painter.font();
        titleFont.setPointSize(11);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setFont(QFont());
        painter.drawText(12, 22, tr("%1 点").arg(QLocale().toString(m_fullPointCount)));

        if (m_overlayOptions.showColorBar && m_colorMode == 0 && !m_points.isEmpty()) {
            const int barHeight = qBound(80, int(height() * 0.45), 360);
            QLinearGradient gradient(0, 0, 0, barHeight);
            gradient.setColorAt(0.0, QColor::fromRgbF(1.0, 0.05, 0.0));
            gradient.setColorAt(0.25, QColor::fromRgbF(1.0, 0.90, 0.0));
            gradient.setColorAt(0.5, QColor::fromRgbF(0.10, 0.95, 0.15));
            gradient.setColorAt(0.75, QColor::fromRgbF(0.0, 0.85, 0.95));
            gradient.setColorAt(1.0, QColor::fromRgbF(0.05, 0.05, 0.95));
            painter.save(); painter.translate(width() - 26, (height() - barHeight) / 2);
            painter.fillRect(QRect(0, 0, 14, barHeight), gradient); painter.restore();
        }
        if (m_overlayOptions.showCoordinateAxes) {
            const QPointF origin(42, height() - 38);
            const QVector3D vectors[]{QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
            const QColor colors[]{QColor(255, 65, 65), QColor(65, 235, 95), QColor(65, 145, 255)};
            const QString names[]{"X", "Y", "Z"};
            for (int i = 0; i < 3; ++i) {
                const QVector3D axis = rotationTransform().mapVector(vectors[i]);
                const QPointF end = origin + QPointF(axis.x() * 28, -axis.y() * 28);
                painter.setPen(QPen(colors[i], 2)); painter.drawLine(origin, end);
                if (m_overlayOptions.showAxisLabels) painter.drawText(end + QPointF(4, -3), names[i]);
            }
        }
        if (m_overlayOptions.showOrientation) {
            const QRectF compass(width() - 80, 10, 68, 68);
            painter.setPen(QPen(QColor(157, 168, 188), 1)); painter.setBrush(QColor(233, 238, 248));
            painter.drawEllipse(compass); painter.setPen(QColor(55, 69, 90));
            painter.drawText(compass.adjusted(0, 2, 0, -44), Qt::AlignCenter, "T");
            painter.drawText(compass.adjusted(0, 44, 0, -2), Qt::AlignCenter, "B");
            painter.drawText(compass.adjusted(3, 0, -44, 0), Qt::AlignCenter, "L");
            painter.drawText(compass.adjusted(44, 0, -3, 0), Qt::AlignCenter, "R");
            painter.setBrush(Qt::NoBrush); painter.drawEllipse(compass.center(), 10, 10);
            painter.setBrush(QColor(55, 69, 90)); painter.drawEllipse(compass.center(), 3, 3);
        }

        if (m_overlayOptions.showStatistics) {
            painter.setFont(QFont());
            painter.setPen(QColor(150, 164, 184));
            const QString pointText = m_points.isEmpty()
                ? tr("未加载数据")
                : (m_hasDisplayMapping
                    ? tr("当前帧展示  %1 / 完整点数 %2")
                        .arg(QLocale().toString(m_points.size())).arg(QLocale().toString(m_fullPointCount))
                    : tr("工作点数  %1  |  全量绘制").arg(QLocale().toString(m_points.size())));
            painter.drawText(18, 49, pointText);
        }

        if (!m_points.isEmpty() && m_overlayOptions.showStatistics) {
            const QString centerText = tr("中心 (mm)  X %1  Y %2  Z %3")
                .arg(QString::number(m_center.x(), 'f', 2))
                .arg(QString::number(m_center.y(), 'f', 2))
                .arg(QString::number(m_center.z(), 'f', 2));
            const QString extentText = tr("范围 (mm)  %1 × %2 × %3")
                .arg(QString::number(m_spanX, 'f', 2))
                .arg(QString::number(m_spanY, 'f', 2))
                .arg(QString::number(m_spanZ, 'f', 2));
            painter.setPen(QColor(196, 207, 222));
            const int right = width() - 18;
            int top = 27;
            if (m_coordinatePointIndex >= 0 && m_coordinatePointIndex < m_points.size()) {
                const pointcloud::Point3D &point = m_points[m_coordinatePointIndex];
                const QString positionText = tr("点 #%1  X %2  Y %3  Z %4 mm")
                    .arg(fullIndexForDisplayIndex(m_coordinatePointIndex))
                    .arg(QString::number(point.x, 'f', 3))
                    .arg(QString::number(point.y, 'f', 3))
                    .arg(QString::number(point.z, 'f', 3));
                const float normalLength = std::sqrt(point.nx * point.nx
                    + point.ny * point.ny + point.nz * point.nz);
                QString attitudeText;
                if (normalLength > 1.0e-6f && std::isfinite(normalLength)) {
                    const float nx = point.nx / normalLength;
                    const float ny = point.ny / normalLength;
                    const float nz = point.nz / normalLength;
                    const float a = qRadiansToDegrees(std::atan2(ny, nz));
                    const float b = qRadiansToDegrees(std::atan2(-nx, std::hypot(ny, nz)));
                    attitudeText = tr("法向姿态  A %1°  B %2°  C 0.00°")
                        .arg(QString::number(a, 'f', 2))
                        .arg(QString::number(b, 'f', 2));
                } else {
                    attitudeText = tr("法向姿态  A --  B --  C --");
                }
                painter.setPen(QColor(239, 244, 250));
                painter.drawText(right - painter.fontMetrics().horizontalAdvance(positionText),
                                 top, positionText);
                painter.setPen(QColor(107, 205, 255));
                painter.drawText(right - painter.fontMetrics().horizontalAdvance(attitudeText),
                                 top + 22, attitudeText);
                top += 44;
            }
            painter.setPen(QColor(196, 207, 222));
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(centerText),
                             top, centerText);
            painter.setPen(QColor(145, 160, 180));
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(extentText),
                             top + 22, extentText);
        }

        if (m_workpieceCoordinate.valid && m_span > 1.0e-9f
            && m_overlayOptions.showCoordinateAxes) {
            const float axisLength = qMax(1.0f, m_span * 0.14f);
            const QPointF origin = project(m_workpieceCoordinate.originInRobotBase);
            const auto drawFrameAxis = [&](const QVector3D &axis, const QColor &color,
                                           const QString &label) {
                const QPointF end = project(m_workpieceCoordinate.originInRobotBase
                                            + axis * axisLength);
                painter.setPen(QPen(color, 2.5));
                painter.drawLine(origin, end);
                painter.setBrush(color);
                painter.drawEllipse(end, 3.0, 3.0);
                if (m_overlayOptions.showAxisLabels)
                    painter.drawText(end + QPointF(5.0, -4.0), label);
            };
            painter.setBrush(QColor(245, 245, 245));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(origin, 4.5, 4.5);
            drawFrameAxis(m_workpieceCoordinate.axisXInRobotBase,
                          QColor(244, 92, 92), tr("工件 X"));
            drawFrameAxis(m_workpieceCoordinate.axisYInRobotBase,
                          QColor(78, 218, 132), tr("工件 Y"));
            drawFrameAxis(m_workpieceCoordinate.axisZInRobotBase,
                          QColor(76, 156, 255), tr("工件 Z"));
        }

        const QString displayError = m_initializationError.isEmpty()
            ? m_uploadError : m_initializationError;
        if (!displayError.isEmpty()) {
            painter.setPen(QColor(255, 125, 115));
            painter.drawText(rect().adjusted(40, 40, -40, -40),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             tr("点云显示失败\n%1").arg(displayError));
            return;
        }
        if (m_points.isEmpty() && !m_emptyMessage.isEmpty()) {
            painter.setPen(QColor(170, 182, 199));
            painter.drawText(rect(), Qt::AlignCenter, m_emptyMessage);
        }
        painter.end();
    }
