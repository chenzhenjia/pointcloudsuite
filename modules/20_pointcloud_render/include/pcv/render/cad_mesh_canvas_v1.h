#pragma once
#include <pcv/core/cad_mesh_v1.h>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QSet>
#include <functional>
namespace pcv::render {
class CadMeshCanvasV1 final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit CadMeshCanvasV1(QWidget *parent=nullptr);
    ~CadMeshCanvasV1() override;
    bool setMesh(pcv::CadMeshV1 mesh);
    void setSelectedFaces(QSet<int> faces);
    int pickFaceAt(const QPointF &position) const;
    std::function<void(int)> faceClicked;
protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    QMatrix4x4 matrix() const;
    QVector3D normalized(const QVector3D &p) const;
    pcv::CadMeshV1 m_mesh;
    QSet<int> m_selected;
    QVector3D m_center;
    float m_span=1, m_zoom=1, m_yaw=30, m_pitch=-35;
    QPointF m_press, m_last, m_pan;
    bool m_drag=false, m_dirty=true, m_ready=false;
    QString m_error;
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_buffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
};
}
