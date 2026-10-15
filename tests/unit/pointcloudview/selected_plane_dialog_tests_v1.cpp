#include "selectedplanedialog.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QThread>
#include <iostream>

struct SelectedPlaneDialogTestAccess {
    static void initialize(SelectedPlaneDialog &d) {
        QSignalBlocker block(d.m_table);d.m_table->setRowCount(2);
        for(int r=0;r<2;++r)for(int c=0;c<4;++c)d.m_table->setItem(r,c,new QTableWidgetItem(c==0?QStringLiteral("frame%1.ply").arg(r):QString()));
        d.m_selections.resize(2);d.m_confirmedIndices.resize(2);d.m_table->setCurrentCell(0,0);
    }
    static void load(SelectedPlaneDialog &d,int row,bool vertical=false) {
        d.invalidateLoaded();{QSignalBlocker block(d.m_table);d.m_table->setCurrentCell(row,0);}
        d.m_loadedRow=row;auto &f=d.m_loaded;f.sourceFile=QStringLiteral("frame%1.ply").arg(row);
        for(int layer=0;layer<2;++layer)for(int y=0;y<25;++y)for(int x=0;x<25;++x) {
            pointcloud::Point3D p=vertical?pointcloud::Point3D{float(layer*5+10),float(x)*0.5f,float(y)*0.5f+30}
                                           :pointcloud::Point3D{float(x)*0.5f,float(y)*0.5f,float(layer*5+30)};
            f.fullPoints.push_back(p);f.sourceIndices.push_back(f.sourceIndices.size()*2);f.scanRatios.push_back(float(y)/25);
        }
        f.declaredCount=f.fullPoints.size()*2;d.m_index=pcv::planefitting::preparePlaneSelectionIndex(f.fullPoints).index;
        d.m_canvas->setCloud(f.fullPoints);d.updateActions();
    }
    static void select(SelectedPlaneDialog &d) {d.picked(0);d.picked(24);d.picked(600);}
    static bool wait(SelectedPlaneDialog &d) {
        QElapsedTimer timer;timer.start();
        while(d.m_busy&&timer.elapsed()<30000) {QApplication::processEvents();QThread::msleep(1);}
        return !d.m_busy;
    }
    static bool run() {
        SelectedPlaneDialog d(0.4f);initialize(d);load(d,0,true);
        if(d.m_method->currentIndex()!=0||d.m_confirm->isEnabled()||d.m_maximumGap->value()!=3||!d.m_maximumGap->isEnabled())return false;
        d.picked(312);if(!wait(d)||!d.m_hasCandidate||!d.m_confirm->isEnabled()||d.m_start->isEnabled())return false;
        if(!d.m_selections.at(0).cloud.fullPoints.isEmpty())return false;
        const auto preview=d.m_candidate;
        if(preview.selectionParameters.value("algorithm_version")!="single_click_plane_selection_v2"||preview.selectionParameters.value("maximum_gap_mm").toDouble()!=3)return false;
        if(preview.cloud.fullPoints.size()!=625||std::abs(preview.normal.x())<0.99f||preview.seedSourceIndices.size()!=1)return false;
        d.discardPreview();if(d.m_hasCandidate||!d.m_selections.at(0).cloud.fullPoints.isEmpty())return false;
        d.picked(312);if(!wait(d))return false;
        d.m_selections[1]=d.m_candidate; // Avoid disk-loading the synthetic second row here.
        d.confirmPreview();const auto old=d.m_selections.at(0);
        if(d.m_hasCandidate||!d.m_start->isEnabled()||old.selectionMethod!=pcv::stitching::PlaneSelectionMethod::SingleClick)return false;
        for(auto index:old.cloud.sourceIndices)if(index>=1250)return false;
        d.picked(312);if(!wait(d)||!d.m_hasCandidate)return false;
        d.m_maximumGap->setValue(0);
        if(d.m_hasCandidate||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        d.picked(312);if(!wait(d)||d.m_candidate.selectionParameters.value("maximum_gap_mm").toDouble()!=0)return false;
        d.discardPreview();
        d.picked(312);if(!wait(d))return false;
        d.m_tolerance->setValue(0.3);
        if(d.m_hasCandidate||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        d.picked(312);d.m_token->store(true);
        if(!wait(d)||d.m_hasCandidate||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        d.picked(312);++d.m_revision;
        if(!wait(d)||d.m_hasCandidate||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        d.m_method->setCurrentIndex(1);if(d.m_maximumGap->isEnabled())return false;
        select(d);if(!wait(d)||!d.m_hasCandidate||d.m_candidate.seedSourceIndices.size()!=3)return false;
        if(d.m_selections.at(0).selectionMethod!=pcv::stitching::PlaneSelectionMethod::SingleClick)return false;
        d.confirmPreview();if(d.m_selections.at(0).selectionMethod!=pcv::stitching::PlaneSelectionMethod::ThreePoint)return false;
        // Cancelled or outdated replacements retain the previously confirmed data.
        select(d);d.m_token->store(true);if(!wait(d)||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        select(d);++d.m_revision;if(!wait(d)||d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        load(d,1);select(d);if(!wait(d)||!d.m_hasCandidate||d.m_candidate.cloud.fullPoints.size()!=625)return false;
        d.confirmPreview();
        if(d.m_selections.at(0).cloud.sourceIndices!=old.cloud.sourceIndices)return false;
        d.moveFrame(-1);if(d.m_loadedRow!=-1||d.m_selections.at(1).cloud.sourceFile!=old.cloud.sourceFile)return false;
        d.m_table->item(1,1)->setText("0 0 0 0 0 0");if(!d.m_selections.at(1).cloud.fullPoints.isEmpty())return false;
        d.m_calibration->setText("changed.xml");if(!d.m_selections.at(0).cloud.fullPoints.isEmpty())return false;
        load(d,0);select(d);d.close();d.m_watcher->waitForFinished();QApplication::processEvents();
        if(!d.m_selections.at(0).cloud.fullPoints.isEmpty())return false;
        SelectedPlaneDialog closingSingle(0.4f);initialize(closingSingle);load(closingSingle,0);
        closingSingle.picked(312);closingSingle.close();closingSingle.m_watcher->waitForFinished();QApplication::processEvents();
        if(closingSingle.m_hasCandidate||!closingSingle.m_selections.at(0).cloud.fullPoints.isEmpty())return false;
        return automaticNext();
    }
    static bool automaticNext() {
        QTemporaryDir root;if(!root.isValid())return false;
        const auto write=[](const QString &path,const QByteArray &data) {QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        if(!write(root.filePath("handeye.xml"),"<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>"))return false;
        QByteArray raw="ply\nformat ascii 1.0\nelement vertex 625\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
        for(int y=0;y<25;++y)for(int x=0;x<25;++x)raw+=QByteArray::number(x*0.5)+' '+QByteArray::number(y*0.5)+" 30\n";
        if(!write(root.filePath("one.ply"),raw)||!write(root.filePath("two.ply"),raw))return false;
        SelectedPlaneDialog d(0.4f);initialize(d);d.m_calibration->setText(root.filePath("handeye.xml"));
        {QSignalBlocker block(d.m_table);for(int r=0;r<2;++r) {
            d.m_table->item(r,0)->setText(root.filePath(r==0?"one.ply":"two.ply"));
            d.m_table->item(r,1)->setText("0 0 0 0 0 0");d.m_table->item(r,2)->setText("0 12 0 0 0 0");
        }}
        d.loadFrame();if(!wait(d)||d.m_loadedRow!=0||!d.m_index)return false;
        d.picked(312);if(!wait(d)||!d.m_hasCandidate)return false;
        d.confirmPreview();if(!wait(d)||d.m_loadedRow!=1||!d.m_index||d.m_selections.at(0).cloud.fullPoints.isEmpty()||!d.m_selections.at(1).cloud.fullPoints.isEmpty())return false;
        d.picked(312);if(!wait(d)||!d.m_hasCandidate)return false;
        d.confirmPreview();return d.m_loadedRow==1&&d.m_start->isEnabled()&&!d.m_busy&&!d.m_hasCandidate;
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    if(!SelectedPlaneDialogTestAccess::run()){std::cerr<<"selected-plane dialog state/auto-fit regression failed\n";return 1;}return 0;
}
