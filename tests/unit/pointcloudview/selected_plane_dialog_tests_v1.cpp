// Regression for the multi-frame (six-panel) selected-plane session.
//
// Covers the display-subset contract of the current frame, the display mapping
// of the canvas, and one complete two-frame session on real PLY files:
// load -> display subset -> full index -> click candidate -> round confirm ->
// complete stitch -> result view. Every dynamic expectation runs on complete
// point clouds; the display subset only affects what is painted.
#include "selectedplanedialog.h"
#include "features/workpiece_template_dialog_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFutureWatcher>
#include <QDataStream>
#include <QDir>
#include <QtConcurrent>
#include <pcv/io/ply_reader.h>
#include <pcv/infrastructure/runtime_paths.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <limits>

struct PointCloudCanvasTestAccess {
    static bool rendered(const PointCloudCanvas &canvas) {
        return canvas.isValid()&&canvas.m_initializationError.isEmpty()
            &&canvas.m_uploadError.isEmpty()&&canvas.hasRenderedCurrentCloud()
            &&!canvas.m_uploadPending&&!canvas.m_stateUploadPending;
    }
    static bool clickPoint(PointCloudCanvas &canvas,int displayIndex) {
        if(!rendered(canvas))return false;
        const auto &p=canvas.m_points.at(displayIndex);
        const QVector3D normalized=(QVector3D(p.x,p.y,p.z)-canvas.m_center)*(2.0f/canvas.m_span);
        QVector4D clip=canvas.viewTransform()*QVector4D(normalized,1);
        clip.setX(clip.x()+float(canvas.m_pan.x())*clip.w());
        clip.setY(clip.y()+float(canvas.m_pan.y())*clip.w());
        clip/=clip.w();
        const QPointF local((clip.x()*0.5f+0.5f)*canvas.width(),(0.5f-clip.y()*0.5f)*canvas.height());
        const QPointF global=canvas.mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&press);
        QApplication::sendEvent(&canvas,&release);
        return true;
    }
    static bool fullDisplay(const PointCloudCanvas &canvas,qsizetype count) {
        return !canvas.m_hasDisplayMapping&&canvas.m_points.size()==count;
    }
    static bool frameDisplay(const PointCloudCanvas &canvas,qsizetype count) {
        return canvas.m_hasDisplayMapping&&canvas.m_fullPointCount==count
            &&canvas.m_points.size()==qMin<qsizetype>(count,300000);
    }
    static bool mappedDisplay(PointCloudCanvas &canvas) {
        const QVector<pointcloud::Point3D> points={{0,0,30},{1,0,30},{2,0,30}};
        canvas.setDisplayCloud(points,{2,5,8},10);
        if(!canvas.m_hasDisplayMapping||canvas.m_points.size()!=3||canvas.m_fullPointCount!=10
            ||canvas.fullIndexForDisplayIndex(1)!=5||canvas.displayIndexForFullIndex(5)!=1
            ||canvas.displayIndexForFullIndex(4)!=-1||canvas.hasRenderedCurrentCloud())return false;
        canvas.setSelectedIndices({5,4});canvas.setExtractedPlane({2,4,8});
        if(canvas.m_selectedIndices!=QVector<int>{1}||canvas.m_planeResultIndices!=QVector<int>({0,2}))return false;
        const auto revision=canvas.m_renderRevision;
        canvas.setDisplayCloud(points,{2,2,8},10);
        canvas.setDisplayCloud(points,{2,5,10},10);
        canvas.setDisplayCloud(points,{2,5},10);
        auto invalid=points;invalid[0].x=std::numeric_limits<float>::quiet_NaN();
        canvas.setDisplayCloud(invalid,{2,5,8},10);
        if(canvas.m_renderRevision!=revision||canvas.m_displayToFullIndices!=QVector<int>({2,5,8}))return false;
        canvas.setCloud(points);
        return !canvas.m_hasDisplayMapping&&canvas.fullIndexForDisplayIndex(1)==1
            &&canvas.m_fullToDisplayIndices.isEmpty()&&canvas.m_fullPointCount==3;
    }
};

struct SelectedPlaneDialogTestAccess {
    static bool sampling() {
        for(int count:{1,25,300000,300001,600001}) {
            SelectedPlaneTask task;
            for(int i=0;i<count;++i)task.loaded.fullPoints.push_back({float(i),float(i%17),30,0,0,1});
            const auto shared=task.loaded.fullPoints;
            if(!SelectedPlaneDialog::prepareDisplayCloud(&task,{})
                ||task.loaded.fullPoints.constData()!=shared.constData()
                ||task.displayPoints.size()!=qMin(count,300000)
                ||task.displayToFullIndices.size()!=task.displayPoints.size()
                ||task.displayToFullIndices.first()!=0||task.displayToFullIndices.last()!=count-1)return false;
            int previous=-1;
            for(qsizetype k=0;k<task.displayPoints.size();++k) {
                const int index=task.displayToFullIndices.at(k);
                const auto &p=task.displayPoints.at(k),&q=shared.at(index);
                if(index<=previous||p.x!=q.x||p.y!=q.y||p.z!=q.z||p.nz!=q.nz)return false;
                previous=index;
            }
            SelectedPlaneTask repeat;repeat.loaded=task.loaded;
            if(!SelectedPlaneDialog::prepareDisplayCloud(&repeat,{})||repeat.displayToFullIndices!=task.displayToFullIndices)return false;
            SelectedPlaneTask cancelled;cancelled.loaded=task.loaded;int checks=0;
            if(SelectedPlaneDialog::prepareDisplayCloud(&cancelled,[&checks]{return ++checks>=2;})
                ||!cancelled.cancelled||!cancelled.displayPoints.isEmpty()||!cancelled.displayToFullIndices.isEmpty())return false;
        }
        SelectedPlaneTask empty;
        return !SelectedPlaneDialog::prepareDisplayCloud(&empty,{});
    }

    static bool canvasMapping() {
        PointCloudCanvas canvas;
        return PointCloudCanvasTestAccess::mappedDisplay(canvas);
    }

    // --- deterministic fixture: identity hand-eye, Start [0 0 0 0 0 0] and
    // End [0 12 0 0 0 0] over a 25x25 grid with 0.5 mm spacing. The converted
    // robot_base cloud is the exact plane z = 30 with 625 points.
    static bool writeFixture(const QString &directory, int frameCount, QString *xml, QStringList *plies) {
        QFile calibration(directory+"/handeye.xml");
        if(!calibration.open(QIODevice::WriteOnly))return false;
        calibration.write("<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>");
        calibration.close();
        QByteArray raw="ply\nformat ascii 1.0\nelement vertex 625\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
        for(int y=0;y<25;++y)for(int x=0;x<25;++x)
            raw+=QByteArray::number(x*0.5)+' '+QByteArray::number(y*0.5)+" 30\n";
        *xml=calibration.fileName();
        plies->clear();
        for(int index=0;index<frameCount;++index) {
            const QString path=QStringLiteral("%1/frame%2.ply").arg(directory).arg(index);
            QFile file(path);
            if(!file.open(QIODevice::WriteOnly)||file.write(raw)!=raw.size())return false;
            plies->append(path);
        }
        return true;
    }

    static bool pump(SelectedPlaneDialog &dialog, const std::function<bool()> &done, int timeoutMs=30000) {
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<timeoutMs) {
            QApplication::processEvents();
            if(done())return true;
            QThread::msleep(2);
        }
        QApplication::processEvents();
        return done();
    }

    static bool waitIdle(SelectedPlaneDialog &dialog) {
        return pump(dialog,[&dialog] {return dialog.activeJobCount()==0&&!dialog.m_globalBusy;});
    }

    static bool waitSelectable(SelectedPlaneDialog &dialog,int panel) {
        return pump(dialog,[&dialog,panel] {
            const auto frame=dialog.frameAtPanel(panel);
            return frame&&!frame->loaded.fullPoints.isEmpty()&&frame->index&&!frame->running;
        });
    }

    static bool waitCandidate(SelectedPlaneDialog &dialog,int panel) {
        return pump(dialog,[&dialog,panel] {
            const auto frame=dialog.frameAtPanel(panel);
            return frame&&frame->hasCandidate&&!frame->running;
        });
    }

    static void setPoses(SelectedPlaneDialog &dialog, int row) {
        const QSignalBlocker block(dialog.m_table);
        dialog.m_table->item(row,2)->setText(QStringLiteral("0 0 0 0 0 0"));
        dialog.m_table->item(row,3)->setText(QStringLiteral("0 12 0 0 0 0"));
    }

    static bool twoFrameSession(bool nativeGl) {
        QTemporaryDir root;
        if(!root.isValid())return false;
        QString xml;QStringList plies;
        if(!writeFixture(root.path(),2,&xml,&plies))return false;
        SelectedPlaneDialog dialog(0.4f);
        auto *configToggle=dialog.findChild<QToolButton *>(QStringLiteral("selected_plane_config_toggle"));
        auto *configPanel=dialog.findChild<QWidget *>(QStringLiteral("selected_plane_config_panel"));
        auto *logToggle=dialog.findChild<QToolButton *>(QStringLiteral("selected_plane_log_toggle"));
        auto *logPanel=dialog.findChild<QWidget *>(QStringLiteral("selected_plane_log_panel"));
        if(!configToggle||!configPanel||!logToggle||!logPanel||!configPanel->isHidden()||!logPanel->isHidden())return false;
        configToggle->click();logToggle->click();
        if(configPanel->isHidden()||logPanel->isHidden())return false;
        configToggle->click();logToggle->click();
        if(!configPanel->isHidden()||!logPanel->isHidden())return false;
        dialog.m_calibration->setText(xml);
        dialog.m_output->setText(root.path());
        dialog.m_tolerance->setValue(0.4);
        {QSignalBlocker block(dialog.m_table);dialog.appendFiles(plies);}
        for(int row=0;row<2;++row)setPoses(dialog,row);
        if(nativeGl)dialog.show();
        if(dialog.m_round!=0||dialog.m_frames.size()!=2)return false;
        if(dialog.m_panels.at(0).widget->isHidden()||dialog.m_panels.at(1).widget->isHidden())return false;
        if(!dialog.m_panels.at(2).widget->isHidden())return false;
        for(int panel=0;panel<2;++panel) {
            dialog.retryFrame(panel);
            if(!waitSelectable(dialog,panel))return false;
            const auto frame=dialog.frameAtPanel(panel);
            if(frame->loaded.fullPoints.size()!=625)return false;
            if(!PointCloudCanvasTestAccess::frameDisplay(*dialog.m_panels.at(size_t(panel)).canvas,625))return false;
            if(dialog.m_confirm->isEnabled()||dialog.m_start->isEnabled())return false;
        }
        // Single click on the complete cloud through the display mapping.
        dialog.picked(0,312);
        if(!waitCandidate(dialog,0))return false;
        const auto first=dialog.frameAtPanel(0);
        if(first->candidateIndices.size()!=625||!first->hasCandidate)return false;
        if(first->candidate.cloud.fullPoints.size()!=625)return false;
        if(std::abs(first->candidate.normal.z())<0.99f)return false;
        if(first->candidate.selectionParameters.value("algorithm_version")!="single_click_plane_selection_v2")return false;
        if(dialog.m_confirm->isEnabled())return false; // The second frame is still open.
        // Re-clicking replaces the candidate without touching the confirmed snapshot.
        dialog.picked(0,312);
        if(!waitCandidate(dialog,0))return false;
        if(dialog.frameAtPanel(0)->candidateIndices.size()!=625)return false;
        if(!dialog.frameAtPanel(0)->confirmed.cloud.fullPoints.isEmpty())return false;
        dialog.picked(1,312);
        if(!waitCandidate(dialog,1))return false;
        if(!dialog.roundReady()||!dialog.m_confirm->isEnabled())return false;
        if(dialog.m_start->isEnabled())return false; // Not confirmed yet.
        dialog.confirmRound();
        if(!dialog.roundReady())return false;
        if(dialog.frameAtPanel(0)->confirmedIndices.size()!=625)return false;
        if(!dialog.stitchReady()||!dialog.m_start->isEnabled())return false;
        dialog.m_method->setCurrentIndex(1);
        dialog.picked(1,0);dialog.picked(1,24);dialog.picked(1,600);
        if(!waitCandidate(dialog,1))return false;
        dialog.confirmRound();
        if(dialog.frameAtPanel(0)->confirmed.selectionMethod!=pcv::stitching::PlaneSelectionMethod::SingleClick||
           dialog.frameAtPanel(1)->confirmed.selectionMethod!=pcv::stitching::PlaneSelectionMethod::ThreePoint)return false;
        dialog.stitch();
        if(!pump(dialog,[&dialog] {return dialog.m_resultValid||(!dialog.m_globalBusy&&!dialog.m_resultValid);}))return false;
        if(!dialog.m_resultValid) {
            std::cerr<<"stitch failed: "<<dialog.m_log->toPlainText().toStdString()<<"\n";
            return false;
        }
        if(dialog.m_views->currentIndex()!=1)return false;
        if(!PointCloudCanvasTestAccess::fullDisplay(*dialog.m_resultCanvas,1250))return false;
        if(nativeGl&&!pump(dialog,[&dialog] {return PointCloudCanvasTestAccess::rendered(*dialog.m_resultCanvas);}))return false;
        if(!dialog.m_saveTemplate->isEnabled())return false;
        dialog.saveTemplate();
        auto *form=dialog.m_templateForm.data();
        if(!form||form->isWindow()||form->isModal()||QApplication::activeModalWidget()
            ||dialog.m_views->currentWidget()!=form||dialog.m_globalBusy)return false;
        auto *name=form->findChild<QLineEdit *>(QStringLiteral("template_name"));
        if(!name)return false;
        name->setText(QStringLiteral("retained name"));
        form->reject();
        if(!dialog.m_resultValid||dialog.m_views->currentIndex()!=1)return false;
        dialog.saveTemplate();
        if(dialog.m_templateForm!=form||name->text()!=QStringLiteral("retained name"))return false;
        // A changed revision must never submit the stale form's snapshot.
        ++dialog.m_revision;
        form->accept();
        if(dialog.m_globalBusy||!dialog.m_resultValid||dialog.m_views->currentIndex()!=1)return false;
        if(dialog.m_result.reportFile.isEmpty()||dialog.m_result.outputPly.isEmpty())return false;
        const auto formal=pcv::detail::io::readPly(dialog.m_result.outputPly);
        if(!formal.ok||formal.points.size()!=1250||dialog.m_result.cloudIds.size()!=1250||dialog.m_result.sourceIndices.size()!=1250)return false;
        QFile mapping(dialog.m_result.mappingFile);
        if(!mapping.open(QIODevice::ReadOnly))return false;
        QDataStream map(&mapping);map.setByteOrder(QDataStream::LittleEndian);map.setFloatingPointPrecision(QDataStream::SinglePrecision);
        quint32 magic=0,version=0;quint64 count=0;map>>magic>>version>>count;
        if(magic!=0x314D4350||version!=1||count!=1250)return false;
        for(int i=0;i<1250;++i) {
            qint32 cloud=0;qint64 source=0;float ratio=0;map>>cloud>>source>>ratio;
            if(cloud!=dialog.m_result.cloudIds.at(i)||source!=dialog.m_result.sourceIndices.at(i)||ratio!=dialog.m_result.scanRatios.at(i))return false;
        }
        if(map.status()!=QDataStream::Ok||!mapping.atEnd())return false;
        // The package contains the complete formal result, never display points.
        const auto sharedFull=dialog.m_result.points;
        pcv::workpiece::SaveWorkpieceTemplateRequest save;
        save.name=QStringLiteral("batch template");save.outputRoot=root.filePath("templates");
        save.coordinateFrame=QStringLiteral("robot_base");save.points=sharedFull;save.source=dialog.m_resultSource;
        const auto saved=pcv::workpiece::saveWorkpieceTemplate(save);
        const auto savedAgain=pcv::workpiece::saveWorkpieceTemplate(save);
        if(!saved.success||!savedAgain.success||saved.outputDirectory==savedAgain.outputDirectory)return false;
        const QString moved=root.filePath("moved_template");
        if(!QDir().rename(saved.outputDirectory,moved))return false;
        for(const QString &path:plies)if(!QFile::remove(path))return false;
        const auto loaded=pcv::workpiece::loadWorkpieceTemplate({QDir(moved).filePath(QString::fromLatin1(pcv::workpiece::kWorkpieceTemplateJson))});
        if(!loaded.success||loaded.points.size()!=sharedFull.size()||
           std::memcmp(loaded.points.constData(),sharedFull.constData(),size_t(sharedFull.size())*sizeof(pointcloud::Point3D)))return false;
        // Restore only these temporary test inputs for the round-revisit check.
        if(!writeFixture(root.path(),2,&xml,&plies))return false;
        // Changing a selection parameter invalidates the stitched result but keeps
        // the confirmed snapshots; the save entry stays closed.
        dialog.m_tolerance->setValue(0.3);
        if(dialog.m_resultValid||dialog.m_saveTemplate->isEnabled())return false;
        if(dialog.m_views->currentIndex()!=0)return false;
        if(dialog.frameAtPanel(0)->confirmedIndices.size()!=625)return false;
        dialog.loadRound(0);
        if(!waitIdle(dialog)||!dialog.roundReady())return false;
        // Skipping keeps the confirmed snapshot until the round is confirmed again,
        // and an unskip returns to the unconfirmed state.
        dialog.toggleSkip(1);
        if(!dialog.roundReady())return false;
        dialog.confirmRound();
        if(!dialog.frameAtPanel(1)->committedSkip)return false;
        if(!dialog.frameAtPanel(1)->confirmedIndices.isEmpty())return false;
        if(dialog.stitchReady())return false; // Only one frame is left.
        dialog.toggleSkip(1);
        if(dialog.frameAtPanel(1)->skip)return false;
        if(dialog.stitchReady())return false;
        dialog.close();
        dialog.cancelAndWait();
        QApplication::processEvents();
        return dialog.activeJobCount()==0&&!dialog.m_resultValid;
    }

    static bool invalidFrameLifecycle() {
        QTemporaryDir root;
        if(!root.isValid())return false;
        QString xml;QStringList plies;
        if(!writeFixture(root.path(),1,&xml,&plies))return false;
        SelectedPlaneDialog dialog(0.4f);
        dialog.m_calibration->setText(xml);
        {QSignalBlocker block(dialog.m_table);dialog.appendFiles(plies);}
        setPoses(dialog,0);
        dialog.retryFrame(0);
        if(!waitSelectable(dialog,0))return false;
        dialog.picked(0,312);
        if(!waitCandidate(dialog,0))return false;
        dialog.confirmRound();
        if(dialog.frameAtPanel(0)->confirmedIndices.size()!=625)return false;
        // A pose change invalidates that frame's complete data, index and selection.
        dialog.m_table->item(0,3)->setText(QStringLiteral("0 13 0 0 0 0"));
        const auto frame=dialog.frameAtPanel(0);
        if(!frame->loaded.fullPoints.isEmpty()||frame->index||!frame->confirmedIndices.isEmpty())return false;
        if(!frame->confirmed.cloud.fullPoints.isEmpty()||!frame->dirty)return false;
        if(!PointCloudCanvasTestAccess::fullDisplay(*dialog.m_panels.at(0).canvas,0))return false;
        // The calibration change is the global variant of the same rule.
        dialog.m_calibration->setText(QStringLiteral("changed.xml"));
        if(!frame->confirmedIndices.isEmpty())return false;
        dialog.close();
        dialog.cancelAndWait();
        return true;
    }

    static bool roundsAndSkip() {
        for(int count:{1,6,7,12,13}) {
            SelectedPlaneDialog d(0.4f);QStringList paths;
            for(int i=0;i<count;++i)paths.append(QStringLiteral("fixture_%1.ply").arg(i));
            d.appendFiles(paths);
            const int rounds=(count+5)/6;
            for(int round=0;round<rounds;++round) {
                if(d.m_round!=round||d.roundRows(round).size()!=qMin(6,count-round*6))return false;
                // No disk work is required to explicitly skip a missing frame.
                for(int panel=0;panel<d.roundRows(round).size();++panel)d.toggleSkip(panel);
                if(!d.roundReady()||d.stitchReady())return false;
                d.confirmRound();
            }
            for(const auto &f:std::as_const(d.m_frames))
                if(!f->processed||!f->committedSkip||f->dirty)return false;
            if(d.stitchReady())return false;
            if(rounds>1) {
                d.loadRound(rounds-2);
                if(!d.roundReady())return false;
                const auto frame=d.frameAtPanel(0);const auto id=frame->id;
                d.toggleSkip(0); // Undo commit requires a new selection.
                if(frame->skip||!frame->dirty||d.roundReady()||frame->id!=id)return false;
                d.toggleSkip(0);if(!d.roundReady())return false;
            }
        }
        return true;
    }

    static bool cancellationAndRevision() {
        QTemporaryDir root;QString xml;QStringList paths;
        if(!root.isValid()||!writeFixture(root.path(),2,&xml,&paths))return false;
        SelectedPlaneDialog d(0.4f);d.m_calibration->setText(xml);d.appendFiles(paths);
        for(int row=0;row<2;++row)setPoses(d,row);
        d.loadRound(0);if(!waitIdle(d))return false;
        // A genuine pair of fits exercises the two frame-job bound. Their
        // points have extra Qt sharing retained through completion.
        const auto shared0=d.frameAtPanel(0)->loaded.fullPoints;
        const auto shared1=d.frameAtPanel(1)->loaded.fullPoints;
        d.picked(0,312);d.picked(1,312);
        if(d.activeJobCount()>2)return false;
        d.cancelFrame(0);
        if(!waitIdle(d)||d.frameAtPanel(0)->hasCandidate||!d.frameAtPanel(1)->hasCandidate)return false;
        d.picked(0,312);++d.frameAtPanel(0)->revision;
        if(!waitIdle(d)||d.frameAtPanel(0)->hasCandidate)return false;
        d.m_method->setCurrentIndex(1);
        d.picked(0,0);d.picked(0,24);
        d.m_tolerance->setValue(0.3);
        if(!d.frameAtPanel(0)->seeds.isEmpty())return false;
        d.picked(0,0);d.picked(0,24);d.picked(0,600);
        if(!waitCandidate(d,0)||d.frameAtPanel(0)->candidate.seedSourceIndices.size()!=3)return false;
        const auto frame=d.frameAtPanel(0);d.discardPreview(0);
        d.m_method->setCurrentIndex(0);d.picked(0,312);
        d.close();QApplication::processEvents();
        if(d.activeJobCount()!=0||frame->hasCandidate)return false;
        return shared0.size()==625&&shared1.size()==625;
    }

    static bool budgetPause() {
        QTemporaryDir root;QString xml;QStringList paths;
        if(!root.isValid()||!writeFixture(root.path(),2,&xml,&paths))return false;
        SelectedPlaneDialog d(0.4f);d.m_calibration->setText(xml);d.appendFiles(paths);
        for(int row=0;row<2;++row)setPoses(d,row);
        d.loadRound(0);if(!waitIdle(d))return false;
        for(int panel=0;panel<2;++panel) {d.picked(panel,312);if(!waitCandidate(d,panel))return false;}
        d.confirmRound();
        const auto snapshot=d.frameAtPanel(0)->confirmed.cloud.fullPoints;
        d.m_budget=std::make_shared<pcv::ProcessingBudget>(0);
        d.retryFrame(0);if(!waitIdle(d)||!d.m_memoryBlocked)return false;
        if(d.frameAtPanel(0)->confirmed.cloud.fullPoints.constData()!=snapshot.constData())return false;
        d.toggleSkip(0);
        if(d.m_memoryBlocked||d.frameAtPanel(0)->confirmed.cloud.fullPoints.size()!=625)return false;
        d.close();return true;
    }

    static bool dispatchOrderAndCompletion() {
        SelectedPlaneDialog d(0.4f);
        QStringList paths;for(int i=0;i<7;++i)paths.append(QStringLiteral("dispatch_%1.ply").arg(i));
        d.appendFiles(paths);
        if(d.m_activePanel!=-1)return false;
        d.m_globalBusy=true; // Inspect dispatch without starting disk work.
        const auto first=d.frameAtPanel(0),second=d.frameAtPanel(1),third=d.frameAtPanel(2);
        first->loaded.fullPoints={{0,0,30}};first->indexQueued=true;
        second->loadQueued=true;
        SelectedPlaneTask::Kind kind;
        if(d.nextQueuedFrame(&kind)!=second||kind!=SelectedPlaneTask::LoadDisplay)return false;
        d.activatePanel(0);
        if(d.nextQueuedFrame(&kind)!=first||kind!=SelectedPlaneTask::PrepareIndex)return false;
        third->fitQueued=true;third->seeds={0};
        if(d.nextQueuedFrame(&kind)!=third||kind!=SelectedPlaneTask::Fit)return false;
        third->running=true;
        if(d.nextQueuedFrame(&kind)!=first)return false;
        third->running=false;
        for(const auto &frame:std::as_const(d.m_frames)) {
            frame->loadQueued=false;frame->indexQueued=false;frame->fitQueued=false;frame->seeds.clear();
            frame->processed=true;frame->dirty=false;frame->confirmed.cloud.fullPoints={{0,0,30}};
        }
        d.m_globalBusy=false;
        if(d.m_round!=0||!d.stitchReady())return false; // All seven committed, viewed from round one.
        third->dirty=true;
        if(d.stitchReady())return false;
        return true;
    }

    static bool stalePayloadKeepsReplacement() {
        for(int reason=0;reason<4;++reason) {
            SelectedPlaneDialog d(0.4f);d.appendFiles({QStringLiteral("stale.ply")});
            d.m_globalBusy=true; // Keep replacement requests queued for inspection.
            const auto frame=d.frameAtPanel(0);
            frame->hasCandidate=true;frame->candidateIndices={1,2,3};frame->seeds={7};
            frame->loadQueued=true;frame->indexQueued=true;frame->fitQueued=true;frame->running=true;
            auto &job=d.m_jobs.at(0);job.frame=frame;job.frameId=frame->id;job.revision=frame->revision;
            job.kind=SelectedPlaneTask::Fit;job.token=std::make_shared<std::atomic_bool>(false);
            if(reason==0)++frame->revision;
            if(reason==1)++job.frameId;
            if(reason==2)job.token->store(true);
            job.watcher=new QFutureWatcher<SelectedPlaneTask>(&d);
            job.watcher->setFuture(QtConcurrent::run([reason] {
                SelectedPlaneTask task;task.kind=reason==3?SelectedPlaneTask::LoadDisplay:SelectedPlaneTask::Fit;
                task.outOfMemory=true;return task;
            }));
            job.watcher->waitForFinished();d.finishJob(0);
            if(d.m_memoryBlocked||frame->running||d.activeJobCount()!=0
                ||!frame->loadQueued||!frame->indexQueued||!frame->fitQueued
                ||!frame->hasCandidate||frame->seeds!=QVector<int>{7}
                ||frame->candidateIndices!=QVector<int>({1,2,3}))return false;
        }
        return true;
    }

    static bool nativeSixCanvases() {
        QTemporaryDir root;QString xml;QStringList paths;
        if(!root.isValid()||!writeFixture(root.path(),6,&xml,&paths))return false;
        SelectedPlaneDialog d(0.4f);d.m_calibration->setText(xml);d.appendFiles(paths);
        for(int row=0;row<6;++row)setPoses(d,row);
        d.show();d.loadRound(0);
        if(!pump(d,[&d] {
            for(int panel=0;panel<6;++panel) {
                const auto frame=d.frameAtPanel(panel);
                if(!frame->index||frame->running||!PointCloudCanvasTestAccess::rendered(*d.m_panels.at(size_t(panel)).canvas))return false;
            }
            return true;
        }))return false;
        for(int panel=0;panel<6;++panel) {
            if(!PointCloudCanvasTestAccess::clickPoint(*d.m_panels.at(size_t(panel)).canvas,312)
                ||!waitCandidate(d,panel))return false;
            const auto frame=d.frameAtPanel(panel);
            if(frame->candidateIndices.size()!=625||frame->candidate.seedSourceIndices!=QVector<qsizetype>{312})return false;
            if(!pump(d,[frame] {return !frame->highlightPending;}))return false;
        }
        // The enlarged view reuses the native canvas and its view state.
        auto *canvas=d.m_panels.at(2).canvas;
        d.enlarge(2);QApplication::processEvents();
        if(d.m_panels.at(2).canvas!=canvas||!canvas->isVisible())return false;
        d.enlarge(2);QApplication::processEvents();
        for(int panel=0;panel<6;++panel)if(!d.m_panels.at(size_t(panel)).canvas->isVisible())return false;
        d.toggleSkip(5);
        if(!d.roundReady())return false;
        d.confirmRound();
        if(!d.stitchReady()||!d.frameAtPanel(5)->confirmed.cloud.fullPoints.isEmpty())return false;
        d.close();d.cancelAndWait();
        return d.activeJobCount()==0;
    }

    static bool run(bool nativeGl) {
        if(!sampling()||!canvasMapping()||!roundsAndSkip()||!cancellationAndRevision()||!budgetPause()
            ||!dispatchOrderAndCompletion()||!stalePayloadKeepsReplacement()) {
            std::cerr<<"selected-plane display subset regression failed\n";
            return false;
        }
        if(nativeGl&&!nativeSixCanvases()) {
            std::cerr<<"selected-plane native six-canvas rendering/picking regression failed\n";
            return false;
        }
        if(!twoFrameSession(nativeGl)) {
            std::cerr<<"selected-plane six-panel session regression failed\n";
            return false;
        }
        if(!invalidFrameLifecycle()) {
            std::cerr<<"selected-plane invalidation lifecycle regression failed\n";
            return false;
        }
        return true;
    }
};

int main(int argc,char **argv) {
    const bool nativeGl=std::find_if(argv+1,argv+argc,[](const char *arg) {return std::strcmp(arg,"--native-gl")==0;})!=argv+argc;
    if(nativeGl) {
        qputenv("QT_OPENGL","desktop");
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
        QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);
        format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);
    }
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir runtime;if(!runtime.isValid())return 1;
    pcv::runtime::configureDataDirectory(runtime.path());
    if(!SelectedPlaneDialogTestAccess::run(nativeGl))return 1;
    std::cout<<"selected_plane_dialog_tests_v1 passed\n";
    return 0;
}
