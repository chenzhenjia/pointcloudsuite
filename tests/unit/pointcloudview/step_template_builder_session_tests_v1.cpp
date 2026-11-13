#include "step_template_builder_session_v1.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <cstring>
#include <stdexcept>
namespace pcv::stepbuilder {
struct StepTemplateBuilderSessionTestAccessV1 {
    static bool wait(StepTemplateBuilderSessionV1 &s){
        QElapsedTimer timer;timer.start();while(s.busy()&&timer.elapsed()<10000){QCoreApplication::processEvents();QThread::msleep(1);}return !s.busy();
    }
    static bool run(){
#define CHECK(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;}}while(false)
        StepTemplateBuilderSessionV1 s;
        // Overlapping projected points at distinct depths, including a point absent from display.
        s.m_cloud.success=true;s.m_cloud.points={{0,0,-.5f,0,0,1},{0,0,.5f,0,0,1},{.8f,.8f,0,0,0,1}};
        s.m_cloud.faceRanges={{1,0,3}};s.m_retained.insert(1,3);s.m_work.insert(1);
        const auto shared=s.m_cloud.points;
        pcv::render::CloudProjectionSnapshotV1 view;view.span=2;view.viewport={100,100};
        s.selectRectangle(view,QRectF(45,45,10,10));CHECK(wait(s));CHECK(s.m_pending==QSet<int>({0,1}));
        s.erasePending();CHECK(wait(s));CHECK(s.m_removed==QSet<int>({0,1}));CHECK(s.displayMap()==QVector<int>({2}));CHECK(s.canUndo());
        s.undo();CHECK(wait(s));CHECK(s.removedCount()==0&&s.displayMap().size()==3&&!s.canUndo());
        s.m_pending={0,1,2};s.erasePending();CHECK(wait(s));CHECK(s.displayPoints().isEmpty()&&!s.canProcess());
        s.restore();CHECK(wait(s));CHECK(s.displayPoints().size()==3&&s.canProcess());
        CHECK(std::memcmp(shared.constData(),s.cloud().points.constData(),size_t(shared.size())*sizeof(pointcloud::Point3D))==0);
        pcv::workpiece::CadFaceInfo face;face.id=1;face.planar=true;face.surfaceType="plane";
        auto curved=face;curved.id=2;curved.planar=false;curved.surfaceType="cylinder";s.m_model.faces={face,curved};
        s.toggleFace(1,true);CHECK(s.interferenceFaces().contains(1)&&s.workFaces().isEmpty());
        s.toggleFace(2,false);CHECK(s.workFaces().isEmpty());s.toggleFace(1,false);CHECK(s.workFaces().contains(1)&&s.interferenceFaces().isEmpty());
        s.m_package.ok=true;s.confirm();CHECK(s.confirmed());s.toggleFace(1,false);CHECK(!s.confirmed()&&!s.package().ok);
        s.start(StepTemplateBuilderSessionV1::Kind::Process,"stale",[](const auto &){StepTemplateBuilderSessionV1::Result r;r.ok=true;r.package.ok=true;return r;});
        ++s.m_revision;CHECK(wait(s));CHECK(!s.package().ok);
        s.start(StepTemplateBuilderSessionV1::Kind::Process,"exception",[](const auto &)->StepTemplateBuilderSessionV1::Result{throw std::runtime_error("expected failure");});
        CHECK(wait(s));CHECK(s.status().contains("expected failure")&&!s.package().ok);
        s.start(StepTemplateBuilderSessionV1::Kind::Process,"cancel",[](const auto &flag){while(!flag->load())QThread::msleep(1);StepTemplateBuilderSessionV1::Result r;r.ok=true;r.package.ok=true;return r;});
        s.cancel();CHECK(wait(s));CHECK(!s.package().ok);
        s.start(StepTemplateBuilderSessionV1::Kind::Process,"close",[](const auto &flag){while(!flag->load())QThread::msleep(1);return StepTemplateBuilderSessionV1::Result{};});
        s.shutdown();CHECK(!s.m_watcher.isRunning());
        return true;
#undef CHECK
    }
};
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==3&&app.arguments().at(1)=="--step"){
        pcv::stepbuilder::StepTemplateBuilderSessionV1 session;
        session.importStep(app.arguments().at(2),.5);
        QElapsedTimer timer;timer.start();
        while(session.busy()&&timer.elapsed()<120000){QCoreApplication::processEvents();QThread::msleep(1);}
        if(session.busy()||!session.cloud().success){std::cerr<<session.status().toStdString()<<'\n';return 2;}
        const auto &model=session.model();const auto size=model.maximum-model.minimum;
        int planar=0;for(const auto &face:model.faces)if(face.planar)++planar;
        std::cout<<"STEP_IMPORT_OK points="<<session.cloud().points.size()<<" display="<<session.displayPoints().size()
            <<" faces="<<model.faces.size()<<" planar="<<planar<<" size_mm="<<size.x()<<","<<size.y()<<","<<size.z()<<'\n';
        return 0;
    }
    return pcv::stepbuilder::StepTemplateBuilderSessionTestAccessV1::run()?0:1;
}
