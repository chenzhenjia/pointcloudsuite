#include <pcv/render/cad_mesh_canvas_v1.h>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>
namespace pcv::render {
CadMeshCanvasV1::CadMeshCanvasV1(QWidget *parent):QOpenGLWidget(parent)
{
    setMinimumSize(600,450);
    setToolTip(tr("点击选择/取消整个面；左键拖动旋转，右键平移，滚轮缩放"));
}
CadMeshCanvasV1::~CadMeshCanvasV1()
{
    if(context() && context()->isValid()) { makeCurrent(); m_buffer.destroy(); m_vao.destroy(); m_program.removeAllShaders(); doneCurrent(); }
}
bool CadMeshCanvasV1::setMesh(pcv::CadMeshV1 mesh)
{
    Q_ASSERT(QThread::currentThread()==thread());
    if(mesh.vertices.isEmpty() || mesh.triangles.isEmpty()
        || mesh.triangles.size()>std::numeric_limits<int>::max()/72) return false;
    QVector3D lo=mesh.vertices.constFirst(),hi=lo;
    for(const auto &v:std::as_const(mesh.vertices)) {
        for(int k=0;k<3;++k) {
            if(!std::isfinite(v[k])) return false;
            lo[k]=std::min(lo[k],v[k]); hi[k]=std::max(hi[k],v[k]);
        }
    }
    for(const auto &t:std::as_const(mesh.triangles))
        if(t.faceId<=0 || t.a<0 || t.b<0 || t.c<0 || t.a>=mesh.vertices.size()
            || t.b>=mesh.vertices.size() || t.c>=mesh.vertices.size()) return false;
    const auto center=lo+(hi-lo)*0.5f;
    const float span=std::max({hi.x()-lo.x(),hi.y()-lo.y(),hi.z()-lo.z(),1e-6f});
    if(!std::isfinite(span) || !std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(center.z())) return false;
    m_mesh=std::move(mesh); m_center=center; m_span=span;
    m_selected.clear(); m_zoom=1; m_pan={}; m_dirty=true; update(); return true;
}
void CadMeshCanvasV1::setSelectedFaces(QSet<int> faces) { m_selected=std::move(faces); m_dirty=true; update(); }
void CadMeshCanvasV1::clearMesh() {m_mesh={};m_selected.clear();m_faceColors.clear();m_dirty=true;update();}
void CadMeshCanvasV1::setFaceColors(QHash<int,QColor> colors) {m_faceColors=std::move(colors);m_dirty=true;update();}
void CadMeshCanvasV1::setAppearance(QColor background,QColor neutral) {m_background=background;m_neutral=neutral;m_dirty=true;update();}
QVector3D CadMeshCanvasV1::normalized(const QVector3D &p) const { return (p-m_center)/m_span; }
QMatrix4x4 CadMeshCanvasV1::matrix() const
{
    QMatrix4x4 p; const float aspect=float(width())/std::max(1,height());
    p.ortho(-aspect/m_zoom,aspect/m_zoom,-1/m_zoom,1/m_zoom,-10,10);
    p.translate(float(m_pan.x()),float(m_pan.y()),0); p.rotate(m_pitch,1,0,0); p.rotate(m_yaw,0,0,1); return p;
}
void CadMeshCanvasV1::initializeGL()
{
    if(!initializeOpenGLFunctions()) { m_error=tr("CAD 画布需要 OpenGL 3.3"); return; }
    m_ready=m_program.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec3 c; uniform mat4 m; out vec3 color; void main(){gl_Position=m*vec4(p,1);color=c;}")
        && m_program.addShaderFromSourceCode(QOpenGLShader::Fragment,
        "#version 330 core\nin vec3 color; out vec4 frag; void main(){frag=vec4(color,1);}") && m_program.link();
    if(!m_ready) { m_error=m_program.log(); return; }
    m_vao.create(); m_buffer.create();
}
void CadMeshCanvasV1::paintGL()
{
    if(!m_ready) { QPainter p(this); p.drawText(rect(),Qt::AlignCenter,m_error); return; }
    glViewport(0,0,int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
    glClearColor(float(m_background.redF()),float(m_background.greenF()),float(m_background.blueF()),1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST);
    m_vao.bind(); m_buffer.bind(); m_program.bind();
    if(m_dirty) {
        QVector<float> vertices; vertices.reserve(m_mesh.triangles.size()*18);
        for(const auto &t:std::as_const(m_mesh.triangles)) {
            const auto a=normalized(m_mesh.vertices.at(t.a)),b=normalized(m_mesh.vertices.at(t.b)),c=normalized(m_mesh.vertices.at(t.c));
            const auto n=QVector3D::crossProduct(b-a,c-a).normalized();
            const float shade=.45f+.55f*std::abs(QVector3D::dotProduct(n,QVector3D(.3f,.4f,.866f)));
            const auto role=m_faceColors.value(t.faceId,m_selected.contains(t.faceId)?QColor::fromRgbF(1,.65,.12):m_neutral);
            const auto color=QVector3D(float(role.redF()),float(role.greenF()),float(role.blueF()))*shade;
            for(const auto &v:{a,b,c}) vertices << v.x()<<v.y()<<v.z()<<color.x()<<color.y()<<color.z();
        }
        m_buffer.allocate(vertices.constData(),int(vertices.size()*sizeof(float))); m_dirty=false;
    }
    m_program.setUniformValue("m",matrix());
    m_program.enableAttributeArray(0); m_program.setAttributeBuffer(0,GL_FLOAT,0,3,6*sizeof(float));
    m_program.enableAttributeArray(1); m_program.setAttributeBuffer(1,GL_FLOAT,3*sizeof(float),3,6*sizeof(float));
    glDrawArrays(GL_TRIANGLES,0,int(m_mesh.triangles.size()*3));
    m_program.release(); m_buffer.release(); m_vao.release();
}
int CadMeshCanvasV1::pickFaceAt(const QPointF &pos) const
{
    bool ok=false; const auto inverse=matrix().inverted(&ok); if(!ok) return -1;
    const float x=float(2*pos.x()/std::max(1,width())-1),y=float(1-2*pos.y()/std::max(1,height()));
    const auto origin=inverse.map(QVector3D(x,y,-1));
    const auto direction=(inverse.map(QVector3D(x,y,1))-origin).normalized();
    float closest=std::numeric_limits<float>::max(); int face=-1;
    for(const auto &t:m_mesh.triangles) {
        const auto a=normalized(m_mesh.vertices.at(t.a)),b=normalized(m_mesh.vertices.at(t.b)),c=normalized(m_mesh.vertices.at(t.c));
        const auto e1=b-a,e2=c-a,h=QVector3D::crossProduct(direction,e2);
        const float det=QVector3D::dotProduct(e1,h); if(std::abs(det)<1e-9f) continue;
        const float inv=1/det; const auto s=origin-a;
        const float u=inv*QVector3D::dotProduct(s,h); if(u<0 || u>1) continue;
        const auto q=QVector3D::crossProduct(s,e1); const float v=inv*QVector3D::dotProduct(direction,q);
        if(v<0 || u+v>1) continue;
        const float distance=inv*QVector3D::dotProduct(e2,q);
        if(distance>=0 && distance<closest) { closest=distance; face=t.faceId; }
    }
    return face;
}
void CadMeshCanvasV1::mousePressEvent(QMouseEvent *e) { m_press=m_last=e->position(); m_drag=false; }
void CadMeshCanvasV1::mouseMoveEvent(QMouseEvent *e)
{
    const auto delta=e->position()-m_last; m_last=e->position();
    if((e->position()-m_press).manhattanLength()>4) m_drag=true;
    if(e->buttons()&Qt::LeftButton) { m_yaw+=float(delta.x())*.5f; m_pitch+=float(delta.y())*.5f; }
    if(e->buttons()&Qt::RightButton) m_pan+=QPointF(delta.x(),-delta.y())*(2.0/(std::max(1,height())*m_zoom));
    update();
}
void CadMeshCanvasV1::mouseReleaseEvent(QMouseEvent *e)
{
    if(!m_drag && e->button()==Qt::LeftButton && faceClicked) { const int id=pickFaceAt(e->position()); if(id>0) faceClicked(id); }
}
void CadMeshCanvasV1::wheelEvent(QWheelEvent *e) { m_zoom=std::clamp(m_zoom*float(std::pow(1.001,e->angleDelta().y())),.05f,100.f); update(); }
}
