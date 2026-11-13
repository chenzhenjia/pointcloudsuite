#ifdef PCV_BUILD_STEP_IMPORT
#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>
#endif
#include <pcv/render/cad_mesh_canvas_v1.h>
#include "features/cad_import_dialog_v1.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QElapsedTimer>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QThread>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>

#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;}}while(false)
struct CadImportDialogTestAccess {
    static bool wait(CadImportDialogV1 &d) {
        QElapsedTimer timer;timer.start();
        while(d.m_busy) {if(timer.elapsed()>20000)return false;QApplication::processEvents();QThread::msleep(1);}return true;
    }
    static bool run() {
        {CadImportDialogV1 d("missing.step");CHECK(wait(d));CHECK(!d.m_model.success);CHECK(!d.m_generateButton->isEnabled());}
        {CadImportDialogV1 d("missing.step");d.reject();CHECK(!d.m_read.isRunning() && !d.m_generate.isRunning());}
#ifdef PCV_BUILD_STEP_IMPORT
        QTemporaryDir tmp;CHECK(tmp.isValid());const auto path=tmp.path()+"/part.step";
        STEPControl_Writer writer;CHECK(writer.Transfer(BRepPrimAPI_MakeBox(10,20,30).Shape(),STEPControl_AsIs)==IFSelect_RetDone);
        CHECK(writer.Write(path.toLocal8Bit().constData())==IFSelect_RetDone);
        CadImportDialogV1 d(path);CHECK(wait(d));CHECK(d.m_model.success);CHECK(d.m_faces->rowCount()==6);
        CHECK(!d.m_generateButton->isEnabled());d.m_all->click();CHECK(d.m_generateButton->isEnabled());
        d.m_clear->click();CHECK(!d.m_generateButton->isEnabled());
        d.m_canvas->faceClicked(1);CHECK(d.m_faces->item(0,0)->checkState()==Qt::Checked);
        d.m_canvas->faceClicked(1);CHECK(d.m_faces->item(0,0)->checkState()==Qt::Unchecked);
        d.m_all->click();
        const QString screenshot=qEnvironmentVariable("PCV_CAD_DIALOG_SCREENSHOT");
        if(!screenshot.isEmpty()) { d.show();QApplication::processEvents();CHECK(d.grab().save(screenshot)); }
        d.m_spacing->setValue(.000001);CHECK(!d.m_generateButton->isEnabled());
        d.m_spacing->setValue(.5);d.generate();CHECK(d.m_busy);CHECK(wait(d));
        CHECK(d.result().success && d.result().points.size()==8800 && d.QDialog::result()==QDialog::Accepted);
        CadImportDialogV1 cancel(path);CHECK(wait(cancel));cancel.m_all->click();cancel.generate();cancel.m_cancel->store(true);
        CHECK(wait(cancel));CHECK(cancel.QDialog::result()!=QDialog::Accepted);
#endif
        return true;
    }
};
bool canvasTest(bool native)
{
    pcv::render::CadMeshCanvasV1 canvas;canvas.resize(640,480);
    pcv::CadMeshV1 mesh;mesh.vertices={{-1,-1,.1f},{1,-1,.1f},{1,1,.1f},{-1,1,.1f},
        {-1,-1,-.1f},{1,-1,-.1f},{1,1,-.1f},{-1,1,-.1f}};
    mesh.triangles={{0,1,2,1},{0,2,3,1},{4,5,6,2},{4,6,7,2}};
    CHECK(canvas.setMesh(mesh));CHECK(canvas.pickFaceAt(QPointF(320,240))==1);
    auto bad=mesh;bad.triangles[0].a=100;CHECK(!canvas.setMesh(bad));CHECK(canvas.pickFaceAt(QPointF(320,240))==1);
    if(native) {
        canvas.show();QElapsedTimer timer;timer.start();
        while(!canvas.isValid() && timer.elapsed()<10000){QApplication::processEvents();QThread::msleep(10);}
        CHECK(canvas.isValid());const auto before=canvas.grabFramebuffer();CHECK(!before.isNull());
        canvas.setSelectedFaces({1});QApplication::processEvents();const auto after=canvas.grabFramebuffer();CHECK(!after.isNull() && before!=after);
        const QString screenshot=qEnvironmentVariable("PCV_CAD_CANVAS_SCREENSHOT");if(!screenshot.isEmpty())CHECK(after.save(screenshot));
    }
    return true;
}
int main(int argc,char **argv)
{
    QSurfaceFormat fmt;fmt.setVersion(3,3);fmt.setProfile(QSurfaceFormat::CoreProfile);fmt.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(!canvasTest(app.arguments().contains("--native-gl")))return 1;
    if(!CadImportDialogTestAccess::run())return 2;
    std::cout<<"CAD face picking and dialog lifecycle passed\n";return 0;
}
