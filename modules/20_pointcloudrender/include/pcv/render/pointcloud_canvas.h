#pragma once

#include <pcv/core/point_types.h>
#include <pcv/render/pointcloud_canvas_contract.h>

#include <QCoreApplication>
#include <QFont>
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
    std::function<void(int)> pointPicked;
    std::function<void(const QRectF &)> edgeRectanglePicked;

    explicit PointCloudCanvas(QWidget *parent = nullptr);

    ~PointCloudCanvas() override;

    void setCloud(QVector<pointcloud::Point3D> points);

    void setSelectionMode(bool enabled);

    void setEdgeSelectionMode(bool enabled, const QVector<int> &edgeIndices);

    void setSelectedEdgeIndices(const QVector<int> &indices);

    QVector<int> pickRectangleForSelection(const QRectF &selection);

    void setSelectedIndices(const QVector<int> &indices);

    void setPlaneResult(const QVector<int> &planeIndices, const QVector<int> &edgeIndices,
                        const QVector<pcv::render::Contour> &contours);

    void setExtractedPlane(const QVector<int> &planeIndices);

    void clearPlaneResult();

    void setWorkpieceCoordinateSystem(const pcv::render::CoordinateFrame &frame);

    void clearWorkpieceCoordinateSystem();

    void setPointSize(int size);

    void setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax);

    void resetView();

protected:
    void initializeGL() override;

    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void resizeGL(int width, int height) override;

    void wheelEvent(QWheelEvent *event) override;

private:
    void validateRenderSnapshot();

    void updateBounds();

    void uploadCloudIfNeeded();

    void uploadStatesIfNeeded();

    void uploadContoursIfNeeded();

    int pickPoint(const QPointF &position);

    QVector<int> pickRectangle(const QRectF &selection);

    QMatrix4x4 rotationTransform() const;

    QMatrix4x4 viewTransform() const;

    void drawOverlay();

    QVector<pointcloud::Point3D> m_points;
    int m_coordinatePointIndex = -1;
    pcv::render::CoordinateFrame m_workpieceCoordinate;
    QVector<quint8> m_pointStates;
    QVector<int> m_selectedIndices;
    QVector<int> m_edgeIndices;
    QVector<int> m_selectedEdgeIndices;
    QVector<int> m_planeResultIndices;
    QVector<int> m_edgeResultIndices;
    QVector<pointcloud::Point3D> m_contourVertices;
    QVector<QPair<int, int>> m_contourRanges;
    QOpenGLShaderProgram m_program;
    QOpenGLShaderProgram m_pickingProgram;
    QOpenGLShaderProgram m_contourProgram;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLVertexArrayObject m_contourVertexArray;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_stateBuffer;
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
    float m_minZ = 0.0f;
    float m_maxZ = 1.0f;
    float m_yaw = 38.0f;
    float m_pitch = -28.0f;
    float m_zoom = 1.0f;
    int m_pointSize = 1;
    int m_colorMode = 0;
    float m_overlay = 1.0f;
    float m_mapMin = 0.0f;
    float m_mapMax = 1.0f;
    bool m_uploadPending = false;
    bool m_stateUploadPending = false;
    bool m_contourUploadPending = false;
    bool m_selectionMode = false;
    bool m_edgeSelectionMode = false;
    bool m_mouseMoved = false;
    quint64 m_renderRevision = 0;
};
