#pragma once

#include <pcv/core/point_types.h>
#include <pcv/render/pointcloud_canvas_contract.h>

#include <QCoreApplication>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSet>
#include <QThread>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>
#include <functional>
#include <limits>
#include <memory>

class PointCloudCanvas final : public QOpenGLWidget,
                               protected QOpenGLFunctions_3_3_Core {
public:
    enum PointState : quint8 {
        NormalPoint = 0,
        PlanePoint = 1,
        EdgePoint = 2
    };
    // Optional CPU timing hook. Revisions identify cloud replacement, not overlays.
    std::function<void(quint64,const QJsonObject &)> displayTiming;
    quint64 displayCloudRevision() const { return m_displayCloudRevision; }
    std::function<void(int)> pointPicked;
    // Optional GUI-thread timing hook, immediately before the picking pass.
    std::function<void()> pointPickingStarted;
    std::function<void(const QRectF &)> edgeRectanglePicked;

    explicit PointCloudCanvas(QWidget *parent = nullptr);

    ~PointCloudCanvas() override;

    void setCloud(QVector<pointcloud::Point3D> points);

    // Display-only cloud. Each display point maps to one point in the full
    // source cloud; picking and all overlays continue to use full indices.
    void setDisplayCloud(QVector<pointcloud::Point3D> points,
                         QVector<int> displayToFullIndices,
                         qsizetype fullPointCount);
    // Check from frameSwapped to exclude a swap for the preceding cloud.
    bool hasRenderedCurrentCloud() const {
        return !m_points.isEmpty() && m_lastPaintedRevision == m_renderRevision;
    }

    // Lightweight streaming variant for live camera frames: replaces the
    // cloud and refreshes bounds/depth colormap only. Keeps selection,
    // coordinate frame and contour overlays untouched (the application decides
    // when to clear them) and never resets the view.
    void setCloudStreaming(QVector<pointcloud::Point3D> points);

    void setSelectionMode(bool enabled);

    void setEdgeSelectionMode(bool enabled, const QVector<int> &edgeIndices);

    void setSelectedEdgeIndices(const QVector<int> &indices);

    QVector<int> pickRectangleForSelection(const QRectF &selection);
    pcv::render::CloudProjectionSnapshotV1 projectionSnapshot() const;

    void setSelectedIndices(const QVector<int> &indices);
    // Independent screen-space crosses; indices use the full source mapping.
    void setControlPointIndices(const QVector<int> &indices);

    void setPlaneResult(const QVector<int> &planeIndices, const QVector<int> &edgeIndices,
                        const QVector<pcv::render::Contour> &contours);
    // Mask overlays belong to the currently published point cloud and are
    // cleared automatically when that cloud or its display subset is replaced.
    bool setPlanarMaskOverlays(const QVector<pcv::render::PlanarMaskOverlay> &overlays);
    void clearPlanarMaskOverlays();

    void setExtractedPlane(const QVector<int> &planeIndices);

    void clearPlaneResult();

    void setTextureGrayOverlay(const QVector<quint8> &grayscale,
                               const QVector<quint8> &valid);

    void clearTextureGrayOverlay();

    // Instance-local presentation. Existing callers retain their defaults.
    void setAppearance(const QColor &background, const QColor &selection);
    // Overrides base point shading only. Disabled by default; no point mutation.
    void setUniformPointColor(const QColor &color);
    void clearUniformPointColor();
    void setEmptyMessage(const QString &message);
    // Colors/valid are indexed by the full source cloud, like gray overlays.
    bool setPointColorOverlay(const pcv::render::PointColorOverlay &overlay);
    // Display-sized colors, for callers that already own a deterministic
    // display subset. Does not change full-source selection/index semantics.
    bool setDisplayPointColorOverlay(const pcv::render::PointColorOverlay &overlay);
    // Marker colors are rendered independently of the active color mode and
    // use the same full-source index contract as PointColorOverlay.
    bool setPointMarkerOverlay(const pcv::render::PointColorOverlay &overlay);
    void clearPointMarkerOverlay();

    void setWorkpieceCoordinateSystem(const pcv::render::CoordinateFrame &frame);

    void clearWorkpieceCoordinateSystem();

    void setPointSize(int size);

    void setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax);

    void setOverlayOptions(const pcv::render::RenderOverlayOptions &options);
    pcv::render::RenderOverlayOptions overlayOptions() const { return m_overlayOptions; }

    void resetView();
    void fitView();
    void setStandardView(int view);

protected:
    void initializeGL() override;

    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void resizeGL(int width, int height) override;

    void wheelEvent(QWheelEvent *event) override;

private:
    friend struct PointCloudCanvasTestAccess;
    void validateRenderSnapshot();

    void updateBounds();

    void uploadCloudIfNeeded();

    void uploadStatesIfNeeded();

    void uploadTextureIfNeeded();

    void uploadMarkersIfNeeded();
    void uploadPlanarMasksIfNeeded();

    void uploadContoursIfNeeded();

    int pickPoint(const QPointF &position);

    QVector<int> pickRectangle(const QRectF &selection);

    QMatrix4x4 rotationTransform() const;

    QMatrix4x4 viewTransform() const;

    void drawOverlay(QPaintDevice *device);
    QWidget *m_overlayWidget = nullptr;

    quint64 m_displayCloudRevision = 0;
    QVector<pointcloud::Point3D> m_points;
    bool m_hasDisplayMapping = false;
    bool m_displayMappingSorted = false;
    QVector<int> m_displayToFullIndices;
    QHash<int, int> m_fullToDisplayIndices;
    qsizetype m_fullPointCount = 0;
    int m_coordinatePointIndex = -1;
    pcv::render::CoordinateFrame m_workpieceCoordinate;
    QVector<quint8> m_pointStates;
    QVector<int> m_selectedIndices;
    QVector<int> m_controlPointIndices;
    QVector<int> m_edgeIndices;
    QVector<int> m_selectedEdgeIndices;
    QVector<int> m_planeResultIndices;
    QVector<int> m_edgeResultIndices;
    QVector<pointcloud::Point3D> m_contourVertices;
    QVector<QPair<int, int>> m_contourRanges;
    QVector<QVector3D> m_planarMaskVertices;
    QVector<quint32> m_planarMaskIndices;
    QVector<QPair<int, int>> m_planarMaskRanges;
    QVector<QVector4D> m_planarMaskColors;
    QVector<float> m_planarMaskOpacities;
    QVector<QVector3D> m_planarMaskBoundaryVertices;
    QVector<QPair<int, int>> m_planarMaskBoundaryRanges;
    QVector<QVector4D> m_planarMaskBoundaryColors;
    QOpenGLShaderProgram m_program;
    QOpenGLShaderProgram m_pickingProgram;
    QOpenGLShaderProgram m_contourProgram;
    QOpenGLShaderProgram m_planarMaskProgram;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLVertexArrayObject m_contourVertexArray;
    QOpenGLVertexArrayObject m_planarMaskVertexArray;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_stateBuffer;
    QOpenGLBuffer m_textureBuffer;
    QOpenGLBuffer m_markerBuffer;
    QOpenGLBuffer m_planarMaskVertexBuffer;
    QOpenGLBuffer m_planarMaskIndexBuffer{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer m_contourBuffer;
    std::unique_ptr<QOpenGLFramebufferObject> m_pickingFbo;
    QVector3D m_center;
    QPointF m_lastMousePosition;
    QPointF m_selectionOrigin;
    QPointF m_pan;
    Qt::MouseButton m_pressedButton = Qt::NoButton;
    QString m_initializationError;
    QString m_uploadError;
    float m_span = 1.0f;
    float m_spanX = 0.0f;
    float m_spanY = 0.0f;
    float m_spanZ = 0.0f;
    float m_minX = 0.0f;
    float m_maxX = 0.0f;
    float m_minY = 0.0f;
    float m_maxY = 0.0f;
    float m_minZ = 0.0f;
    float m_maxZ = 1.0f;
    float m_yaw = 0.0f;
    float m_pitch = -72.0f;
    float m_zoom = 1.0f;
    int m_pointSize = 2;
    int m_colorMode = 0;
    float m_overlay = 1.0f;
    float m_mapMin = 0.0f;
    float m_mapMax = 1.0f;
    pcv::render::RenderOverlayOptions m_overlayOptions;
    bool m_uploadPending = false;
    bool m_stateUploadPending = false;
    bool m_contourUploadPending = false;
    bool m_planarMaskUploadPending = false;
    bool m_selectionMode = false;
    bool m_edgeSelectionMode = false;
    bool m_mouseMoved = false;
    quint64 m_renderRevision = 0;
    quint64 m_lastPaintedRevision = 0;
    qsizetype m_vertexBufferCapacityBytes = 0;
    qsizetype m_stateBufferCapacityBytes = 0;
    qsizetype m_textureBufferCapacityBytes = 0;
    qsizetype m_markerBufferCapacityBytes = 0;
    QVector<QVector4D> m_textureColors;
    QVector<QVector4D> m_markerColors;
    QVector<quint8> m_textureValid;
    bool m_textureUploadPending = false;
    bool m_markerUploadPending = false;
    int m_textureFallbackColorMode = 0;
    QColor m_backgroundColor = Qt::black;
    QColor m_selectionColor = Qt::white;
    QColor m_uniformPointColor;
    QString m_emptyMessage = QStringLiteral("打开 PLY 文件开始预览");

    int displayIndexForFullIndex(int fullIndex) const;
    int fullIndexForDisplayIndex(int displayIndex) const;
};
