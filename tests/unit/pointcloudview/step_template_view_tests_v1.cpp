#include "step_template_dialog_v1.h"
#include <QApplication>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
namespace pcv::demo {
struct StepTemplateViewTestAccessV1 {
    static bool run() {
#define CHECK(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;}}while(false)
        ScanGeometrySessionV2 session;StepTemplateDialogV1 dialog(&session);
        CHECK(!dialog.findChild<QPushButton *>("startMatch")->isEnabled());CHECK(!dialog.findChild<QPushButton *>("saveMatch")->isEnabled());
#ifndef PCV_BUILD_STEP_IMPORT
        CHECK(!dialog.findChild<QPushButton *>("importStep")->isEnabled());
#endif
        CHECK(dialog.findChild<QPushButton *>("loadTemplate")->isEnabled());
        dialog.m_model.success=true;pcv::workpiece::CadFaceInfo plane;plane.id=1;plane.planar=true;plane.surfaceType="plane";plane.areaMm2=10;plane.planeNormal={0,0,1};
        auto curved=plane;curved.id=2;curved.planar=false;curved.surfaceType="cylinder";dialog.m_model.faces={plane,curved};
        dialog.chooseFace(1);CHECK(dialog.m_work.contains(1));dialog.findChild<QComboBox *>("faceRole")->setCurrentIndex(1);dialog.chooseFace(1);
        CHECK(!dialog.m_work.contains(1)&&dialog.m_interference.contains(1));dialog.chooseFace(2);CHECK(!dialog.m_interference.contains(2));
        dialog.m_match.success=true;dialog.m_match.candidates.append({});dialog.m_confirmed=0;
        dialog.findChild<QDoubleSpinBox *>("distance")->setValue(3);CHECK(dialog.m_match.candidates.isEmpty()&&dialog.m_confirmed==-1);
        bool accepted=false;dialog.run(QStringLiteral("stale_test"),[](const auto &){StepTemplateDialogV1::Outcome out;out.ok=true;return out;},[&accepted](auto){accepted=true;});
        ++dialog.m_templateRevision;QElapsedTimer timer;timer.start();while(dialog.m_busy&&timer.elapsed()<10000){QApplication::processEvents();QThread::msleep(1);}
        CHECK(!dialog.m_busy&&!accepted);
        dialog.run(QStringLiteral("target_stale_test"),[](const auto &){StepTemplateDialogV1::Outcome out;out.ok=true;return out;},[&accepted](auto){accepted=true;},true);
        ++dialog.m_targetTrimRevision;timer.restart();while(dialog.m_busy&&timer.elapsed()<10000){QApplication::processEvents();QThread::msleep(1);}CHECK(!dialog.m_busy&&!accepted);
        dialog.run(QStringLiteral("close_test"),[](const auto &cancel){while(!cancel())QThread::msleep(1);StepTemplateDialogV1::Outcome out;out.error=QStringLiteral("已取消");return out;},[](auto){});
        dialog.reject();CHECK(!dialog.m_watcher.isRunning());return true;
#undef CHECK
    }
};
}
int main(int argc,char **argv){QApplication app(argc,argv);return pcv::demo::StepTemplateViewTestAccessV1::run()?0:1;}
