#include "scan_geometry_window_v1.h"
#include "scan_geometry_canvas_v2.h"
#include "input_config_dialog_v2.h"
#include "ui_scan_geometry_window_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QPushButton>
#include <QToolButton>
#include <QAbstractButton>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QTimer>
#include <QGridLayout>
#include <QToolButton>
#include <QComboBox>
#include <algorithm>
#include <QDesktopServices>
#include <QUrl>
#include <QPlainTextEdit>
#include <QScrollBar>
static void initializeDemoResourcesV1() { Q_INIT_RESOURCE(scan_geometry_v1); }
namespace pcv::demo {
namespace {
void available(QWidget *w,bool enabled,const QString &reason,const QString &help={}) {
    w->setEnabled(enabled);w->setToolTip(enabled?help:reason);
}
}
ScanGeometryWindowV1::ScanGeometryWindowV1(QWidget *parent)
    :QMainWindow(parent),ui(std::make_unique<Ui::ScanGeometryWindowV1>()) {
    initializeDemoResourcesV1();ui->setupUi(this);
    const QStringList modelStepNames{tr("工件信息"),tr("工件位置及姿态"),tr("扫描与几何选择"),tr("探针测量"),tr("发布模板")};
    for(int i=0;i<modelStepNames.size();++i) {
        auto *button=new QToolButton(ui->stepNavigationHost);button->setObjectName(QStringLiteral("modelStep%1").arg(i));
        button->setText(QStringLiteral("%1\n%2").arg(i+1).arg(modelStepNames.at(i)));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);button->setCheckable(true);button->setMinimumHeight(48);
        button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        ui->modelStepsLayout->addWidget(button,i/3,i%3);m_modelSteps.append(button);
        button->setEnabled(i==2);button->setToolTip(i==2?tr("当前 GUI 实现的扫描几何流程"):tr("该建模步骤尚未接入当前离线几何 GUI"));
    }
    m_modelSteps.at(2)->setChecked(true);
    QFile style(":/scan_geometry_v1/scan_geometry_v1.qss");
    if(style.open(QIODevice::ReadOnly))setStyleSheet(QString::fromUtf8(style.readAll()));
    m_view=new ScanGeometryCanvasV2(ui->canvasHost);ui->canvasLayout->addWidget(m_view);
    m_view->loadCanvasTiming=[this](quint64 frame,quint64 attempt,const QJsonObject &timing){m_session.observeCanvasTiming(frame,attempt,timing);};
    m_view->loadFirstFrame=[this](quint64 frame,quint64 attempt){m_session.observeFirstFrame(frame,attempt);};
    ui->loadTimingPanel->hide();
    connect(ui->toggleLoadTiming,&QToolButton::toggled,this,[this](bool shown){ui->loadTimingPanel->setVisible(shown);refresh(false);});
    connect(ui->openLoadLogDirectory,&QPushButton::clicked,this,[this] {
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(m_session.timingLogDirectory())))
            QMessageBox::warning(this,tr("日志目录"),tr("无法打开目录：%1").arg(m_session.timingLogDirectory()));
    });
    const QList<QPair<QToolButton *,QString>> icons{{ui->selectWorkpiece,"face"},{ui->selectInterference,"interference"},
        {ui->resetPlanes,"reset-planes"},{ui->rectangleTool,"rectangle"},{ui->undoTrim,"undo"},{ui->restoreTrim,"restore"}};
    for(const auto &entry:icons){entry.first->setIcon(QIcon(":/scan_geometry_v1/icons/"+entry.second+".svg"));entry.first->setIconSize(QSize(20,20));}
    connect(&m_session,&ScanGeometrySessionV2::changed,this,[this](bool publish){refresh(publish);});
    connect(ui->configureInputs,&QPushButton::clicked,this,[this]{configureInputs();});
    connect(ui->openScanJson,&QPushButton::clicked,this,[this]{configureInputs(true);});
    ui->configureInputs->setText(tr("导入数据…"));
    ui->openScanJson->hide();
    auto *moreMenu=new QMenu(ui->moreActions);
    ui->moreActions->setMenu(moreMenu);
    const auto addMoreAction=[moreMenu](const QString &name,const char *objectName,QWidget *target) {
        auto *action=moreMenu->addAction(name);action->setObjectName(QString::fromLatin1(objectName));
        QObject::connect(action,&QAction::triggered,target,[target]{
            if(auto *button=qobject_cast<QAbstractButton *>(target))button->click();
        });
        return action;
    };
    auto *threePointAction=moreMenu->addAction(tr("三点选面备用"));
    threePointAction->setObjectName(QStringLiteral("moreThreePoint"));threePointAction->setCheckable(true);
    connect(threePointAction,&QAction::toggled,ui->threePointSelection,&QCheckBox::setChecked);
    moreMenu->addSeparator();
    addMoreAction(tr("确认干涉标注"),"moreConfirmInterference",ui->confirmInterference);
    addMoreAction(tr("清除干涉标注"),"moreClearInterference",ui->clearInterference);
    moreMenu->addSeparator();
    addMoreAction(tr("重试当前帧"),"moreRetry",ui->retryLoad);
    addMoreAction(tr("重置会话"),"moreReset",ui->resetInputs);
    ui->retryLoad->hide();ui->resetInputs->hide();
    ui->threePointSelection->hide();ui->confirmInterference->hide();ui->clearInterference->hide();
    ui->saveGeometry->hide();ui->exportTrimmed->hide();
    ui->deviceNotice->hide();
    for(auto *label:{ui->wobjX,ui->wobjY,ui->wobjZ,ui->wobjA,ui->wobjB,ui->wobjC})label->hide();
    connect(ui->threePointSelection,&QCheckBox::toggled,&m_session,&ScanGeometrySessionV2::setThreePoint);
    connect(ui->loadInputs,&QPushButton::clicked,this,[this]{m_session.loadRound(m_session.round());});
    connect(ui->processDepthFrame,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::processActive);
    connect(ui->previousStage,&QPushButton::clicked,this,[this]{setWorkflowStage(m_workflowStage-1);refresh(false);});
    connect(ui->nextStage,&QPushButton::clicked,this,[this]{setWorkflowStage(m_workflowStage+1);refresh(false);});
    connect(ui->retryLoad,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::retry);
    connect(ui->cancelTask,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::cancel);
    connect(ui->resetInputs,&QPushButton::clicked,this,[this]{if(discardSession(tr("重置将清空输入、候选和当前会话结果。")))m_session.reset();});
    connect(ui->previousRound,&QPushButton::clicked,this,[this]{m_session.loadRound(m_session.round()-1);});
    connect(ui->nextRound,&QPushButton::clicked,this,[this]{m_session.loadRound(m_session.round()+1);});
    const QList<QPushButton *> tabs{ui->segment0,ui->segment1,ui->segment2,ui->segment3,ui->segment4,ui->segment5};
    for(int i=0;i<6;++i)connect(tabs.at(i),&QPushButton::clicked,this,[this,i]{m_view->setTool(ScanGeometryCanvasV2::Tool::Browse);const int target=m_session.round()*6+i;
        if(target<m_session.frames().size()){const auto &f=m_session.frames().at(target);m_view->beginViewObservation(f.id,f.loadAttempt);}
        m_session.activate(target);});
    connect(ui->mergedSegment,&QPushButton::clicked,this,[this]{m_view->setTool(ScanGeometryCanvasV2::Tool::Browse);m_session.showResult();});
    auto selectTool=[this](ScanGeometryCanvasV2::Tool tool) {
        if(tool==ScanGeometryCanvasV2::Tool::Workpiece && m_session.active()==m_session.reference()
            && m_session.activeFrame()&&m_session.activeFrame()->confirmed
            && m_view->tool()!=tool
            && QMessageBox::question(this,tr("重选参考面"),tr("重选参考面会使依赖它的自动候选、拼接结果和几何确认失效。是否继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        m_session.cancelSelection();m_view->setTool(m_view->tool()==tool?ScanGeometryCanvasV2::Tool::Browse:tool);refresh(false);
    };
    connect(ui->selectWorkpiece,&QToolButton::clicked,this,[selectTool]{selectTool(ScanGeometryCanvasV2::Tool::Workpiece);});
    connect(ui->selectInterference,&QToolButton::clicked,this,[selectTool]{selectTool(ScanGeometryCanvasV2::Tool::Interference);});
    connect(ui->rectangleTool,&QToolButton::clicked,this,[selectTool]{selectTool(ScanGeometryCanvasV2::Tool::Rectangle);});
    connect(ui->resetPlanes,&QToolButton::clicked,this,[this]{
        if(QMessageBox::question(this,tr("清除当前工件面"),tr("将清除当前帧工件面及拼接结果。若为参考帧，其自动候选同时失效。"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)m_session.clearPlane();
    });
    connect(ui->confirmCandidate,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::confirmCandidate);
    connect(ui->confirmInterference,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::confirmInterference);
    connect(ui->markHighSideCandidate,&QPushButton::clicked,this,[this]{
        const int seed=ui->highSideCandidates->currentData().toInt();
        if(m_session.selectHighSideCandidate(seed))m_view->setTool(ScanGeometryCanvasV2::Tool::Interference);
    });
    connect(ui->ignoreHighSideCandidate,&QPushButton::clicked,this,[this]{
        m_session.ignoreHighSideCandidate(ui->highSideCandidates->currentData().toInt());
    });
    connect(ui->clearInterference,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::clearInterference);
    connect(ui->matchFrames,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::matchRemaining);
    connect(ui->continueMatching,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::resumeMatching);
    connect(ui->skipFrame,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,tr("明确跳过"),tr("第 %1 帧将不参与拼接。是否跳过？").arg(m_session.active()+1),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)m_session.skipActive();
    });
    connect(ui->stitchFrames,&QPushButton::clicked,this,[this]{
        if(!m_session.removed().isEmpty()&&QMessageBox::question(this,tr("重新拼接"),tr("重新拼接会丢失当前会话修剪。磁盘既有完整产物保留，是否继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        m_session.stitch();
    });
    connect(ui->confirmGeometry,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::saveGeometry);
    connect(ui->rebuildResultRegions,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::rebuildResultRegions);
    connect(ui->generateWobj1,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::generateWobj1);
    connect(ui->saveGeometry,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::saveGeometry);
    connect(ui->exportTrimmed,&QPushButton::clicked,&m_session,&ScanGeometrySessionV2::saveGeometry);
    connect(ui->undoTrim,&QToolButton::clicked,&m_session,&ScanGeometrySessionV2::undo);
    connect(ui->restoreTrim,&QToolButton::clicked,&m_session,&ScanGeometrySessionV2::restore);
    auto *undoShortcut=new QShortcut(QKeySequence::Undo,this);connect(undoShortcut,&QShortcut::activated,&m_session,&ScanGeometrySessionV2::undo);
    auto *escapeShortcut=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escapeShortcut,&QShortcut::activated,this,[this]{m_session.cancelSelection();m_view->setTool(ScanGeometryCanvasV2::Tool::Browse);refresh(false);});
    m_view->planePicked=[this](int index){m_session.pick(index,m_view->tool()==ScanGeometryCanvasV2::Tool::Interference);};
    m_view->eraseRequested=[this]{m_session.erase(m_view->selectedIds(),m_view->selectedRevision());};
    m_view->selectionChanged=[this]{ui->interactionHint->setText(tr("已框选 %1 个可见点；点击擦除按钮确认，Esc 取消。").arg(m_view->selectedIds().size()));};
    m_view->toolCancelled=[this]{m_session.cancelSelection();refresh(false);};
    connect(ui->fitView,&QPushButton::clicked,this,[this]{m_view->clearSelection();m_view->canvas()->fitView();});
    connect(ui->topView,&QPushButton::clicked,this,[this]{m_view->clearSelection();m_view->canvas()->setStandardView(2);});
    refresh(true);
}
ScanGeometryWindowV1::~ScanGeometryWindowV1() {m_session.shutdown();}
bool ScanGeometryWindowV1::discardSession(const QString &reason) {
    if(m_session.frames().isEmpty())return true;
    return QMessageBox::question(this,tr("确认替换会话"),reason+tr("\n磁盘正式产物保留；未保存修剪会丢失。"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
}
int ScanGeometryWindowV1::maximumWorkflowStage() const {
    if(m_session.hasResult())return 3;
    if(m_session.canStitch())return 2;
    int loaded=0;
    for(const auto &frame:m_session.frames())if(!frame.cloud.fullPoints.isEmpty())++loaded;
    if(loaded>0)return 1;
    return 0;
}
void ScanGeometryWindowV1::setWorkflowStage(int stage) {
    if(m_session.interactionBlocked())return;
    m_workflowStage=qBound(0,stage,maximumWorkflowStage());
}
void ScanGeometryWindowV1::configureInputs(bool scanJson) {
    InputConfigDialogV2 dialog(m_session.config(),this);
    if(scanJson)QTimer::singleShot(0,&dialog,&InputConfigDialogV2::addScanJson);
    if(dialog.exec()!=QDialog::Accepted)return;
    if(!discardSession(tr("应用新输入配置会使原选择、匹配和确认结果失效。")))return;
    QString error;if(!m_session.configure(dialog.configuration(),&error))QMessageBox::warning(this,tr("配置失败"),error);
}
void ScanGeometryWindowV1::refresh(bool publish) {
    const auto &frames=m_session.frames();const auto *f=m_session.activeFrame();
    const bool busy=m_session.busy(),blocked=m_session.interactionBlocked();
    const bool navigate=!m_session.activeFrameBusy()&&(!m_session.matching()||m_session.paused());
    const bool canNavigate=!blocked&&(!m_session.matching()||m_session.paused());
    ui->loadTimingSummary->setText(m_session.timingSummary());
    if(ui->toggleLoadTiming->isChecked()) {
        const QString log=m_session.recentLog();
        if(ui->sessionLog->toPlainText()!=log){ui->sessionLog->setPlainText(log);ui->sessionLog->verticalScrollBar()->setValue(ui->sessionLog->verticalScrollBar()->maximum());}
    }
    const bool resultView=m_session.hasResult()&&m_session.active()==-1;
    const bool loaded=f&&!f->cloud.fullPoints.isEmpty();const bool editable=!busy&&resultView;
    const bool depthPreview=f&&f->depthReady&&f->rawDepthFrame&&f->cloud.fullPoints.isEmpty();
    const bool sourceVerified=f&&f->sourceVerification==SourceVerificationV3::Verified;
    const auto displayMode=resultView||(sourceVerified&&f->hasCandidate)?ScanGeometryCanvasV2::DisplayMode::UniformGray:ScanGeometryCanvasV2::DisplayMode::Height;
    ui->colorLegend->setText((displayMode==ScanGeometryCanvasV2::DisplayMode::UniformGray
        ?tr("<span style=\"color:#59636f\">●</span> 灰色点云　")
        :tr("高度色点云　"))+tr("<span style=\"color:#1675e8\">■</span> 蓝色工件拟合面　<span style=\"color:#f04452\">■</span> 红色干涉拟合面"));
    const bool depthInput=f&&m_session.active()>=0&&m_session.active()<m_session.config().frames.size()
        &&m_session.config().frames.at(m_session.active()).depthFrame;
    ui->processDepthFrame->setVisible(depthInput&&bool(f->rawDepthFrame));
    available(ui->processDepthFrame,depthPreview&&navigate&&!m_session.matching(),tr("先加载当前帧深度源"));
    if(m_session.hasResult()&&m_session.resultRevision()!=m_lastWorkflowResultRevision) {
        m_workflowStage=3;m_lastWorkflowResultRevision=m_session.resultRevision();
    }
    if(m_workflowStage>maximumWorkflowStage()&&!busy)m_workflowStage=maximumWorkflowStage();
    const QStringList stageNames{tr("导入数据"),tr("选面与审核"),tr("拼接"),tr("确认并保存")};
    ui->workflowStageTitle->setText(tr("阶段 %1 / 4 · %2").arg(m_workflowStage+1).arg(stageNames.at(m_workflowStage)));
    ui->previousStage->setVisible(m_workflowStage>0);ui->previousStage->setEnabled(!blocked&&m_workflowStage>0);
    const bool canAdvance=m_workflowStage<maximumWorkflowStage()&&!blocked;
    ui->nextStage->setVisible(m_workflowStage<3);ui->nextStage->setEnabled(canAdvance);
    const QStringList nextLabels{tr("下一步：审核"),tr("下一步：拼接"),tr("查看结果"),QString()};
    if(m_workflowStage<nextLabels.size())ui->nextStage->setText(nextLabels.at(m_workflowStage));
    ui->scanCard->setVisible(m_workflowStage==0||busy||m_session.matching()||depthPreview);
    ui->configureInputs->setVisible(m_workflowStage==0);
    ui->loadInputs->setVisible(m_workflowStage==0);
    ui->processingCard->setVisible(m_workflowStage==1||m_workflowStage==2);
    ui->geometryCard->setVisible(m_workflowStage==3&&resultView);
    const auto wait=tr("请先取消或等待当前任务结束");
    if(blocked)m_view->setTool(ScanGeometryCanvasV2::Tool::Browse);
    available(ui->configureInputs,!busy&&!m_session.matching(),wait);
    available(ui->loadInputs,!busy&&!m_session.matching()&&!frames.isEmpty(),tr("先配置输入，或先结束匹配"));
    const bool canRetry=navigate&&f&&(f->problem||f->status.contains(tr("可重试")));
    ui->retryLoad->setVisible(canRetry);
    available(ui->retryLoad,canRetry,tr("当前帧无需重试"));
    available(ui->cancelTask,busy||m_session.matching(),tr("当前无后台任务"));
    available(ui->resetInputs,!busy&&!frames.isEmpty(),wait);
    ui->cancelTask->setVisible(busy||m_session.matching());
    ui->scanProgress->setRange(0,busy?0:1);ui->scanProgress->setValue(0);
    int loadedCount=0,completedCount=0;
    for(const auto &frame:frames){if(!frame.cloud.fullPoints.isEmpty())++loadedCount;if(!frame.digest.isEmpty())++completedCount;}
    ui->scanStatus->setText(frames.isEmpty()?tr("未导入"):loaded&&!sourceVerified
        ?tr("%1 帧 · %2 已加载 · 来源校验中/待重试").arg(frames.size()).arg(loadedCount)
        :tr("%1 帧 · %2 已加载").arg(frames.size()).arg(loadedCount));
    const int rounds=frames.isEmpty()?0:int((frames.size()+5)/6);
    ui->roundStatus->setText(tr("%1 / %2").arg(rounds?m_session.round()+1:0).arg(rounds));
    available(ui->previousRound,!busy&&!m_session.matching()&&m_session.round()>0,wait);
    available(ui->nextRound,!busy&&!m_session.matching()&&m_session.round()+1<rounds,wait);
    const QList<QPushButton *> tabs{ui->segment0,ui->segment1,ui->segment2,ui->segment3,ui->segment4,ui->segment5};
    for(int i=0;i<6;++i) {
        const int index=m_session.round()*6+i;const bool exists=index<frames.size();auto *button=tabs.at(i);
        button->setVisible(exists||frames.isEmpty());button->setChecked(exists&&m_session.active()==index);
        QString state;
        if(exists){const auto &frame=frames.at(index);state=frame.skipped?tr("已跳过"):frame.loading?tr("载入中"):frame.indexPreparing||frame.indexQueued?(frame.sourceVerification==SourceVerificationV3::Checking?tr("来源校验/索引"):tr("准备索引")):frame.problem?tr("需处理")
            :frame.confirmed?tr("已确认"):frame.reviewed?tr("已预览"):frame.hasCandidate?tr("待预览"):frame.cloud.fullPoints.isEmpty()?tr("待加载"):tr("待选面");}
        button->setText(exists?tr("点云 %1\n%2").arg(index+1).arg(state):tr("未配置"));
        available(button,canNavigate&&exists,tr("等待当前任务结束"),exists?frames.at(index).status:QString());
    }
    ui->mergedSegment->setChecked(m_session.active()==-1&&m_session.hasResult());
    ui->mergedSegment->setText(m_session.hasResult()?tr("拼接结果\n已落盘"):tr("拼接结果\n未生成"));
    available(ui->mergedSegment,navigate&&m_session.hasResult(),tr("请先完成拼接"));
    available(ui->selectWorkpiece,m_session.canSelectActive(),tr("等待当前帧完整索引就绪；其他帧可继续后台加载"),tr("单击选面；高级设置可启用三点选面"));
    available(ui->selectInterference,m_session.canSelectActive(),tr("等待当前帧完整索引就绪；其他帧可继续后台加载"),tr("人工标注，不参与拼接；随几何结果保存"));
    available(ui->resetPlanes,navigate&&f&&f->hasCandidate,tr("当前帧无工件候选"),tr("清除当前工件面及其结果；干涉标注可单独清除"));
    const bool canConfirmFrame=navigate&&sourceVerified&&f->hasCandidate&&!f->reviewed;
    available(ui->confirmCandidate,canConfirmFrame,tr("请先选择工件面候选"));
    ui->confirmCandidate->setVisible(canConfirmFrame);
    available(ui->threePointSelection,m_session.canSelectActive(),tr("请先加载原始帧，并结束当前任务"));
    if(auto *action=ui->moreActions->menu()->findChild<QAction *>(QStringLiteral("moreThreePoint"))) {
        const QSignalBlocker blocker(action);action->setChecked(m_session.config().threePoint);action->setEnabled(m_session.canSelectActive());
    }
    for(const auto &pair:{qMakePair(QStringLiteral("moreConfirmInterference"),ui->confirmInterference),
                          qMakePair(QStringLiteral("moreClearInterference"),ui->clearInterference),
                          qMakePair(QStringLiteral("moreRetry"),ui->retryLoad),
                          qMakePair(QStringLiteral("moreReset"),ui->resetInputs)}) {
        if(auto *action=ui->moreActions->menu()->findChild<QAction *>(pair.first))action->setEnabled(pair.second->isEnabled());
    }
    {const QSignalBlocker blocker(ui->threePointSelection);ui->threePointSelection->setChecked(m_session.config().threePoint);}
    available(ui->confirmInterference,navigate&&sourceVerified&&!f->pendingInterference.isEmpty(),tr("先手工选择干涉面"));
    available(ui->clearInterference,navigate&&sourceVerified&&(!f->interferenceIndices.isEmpty()||!f->pendingInterference.isEmpty()),tr("当前帧无干涉标注"));
    available(ui->skipFrame,navigate&&f&&!f->skipped,tr("请选择未跳过的原始帧"));
    ui->skipFrame->setVisible(navigate&&f&&!f->skipped);
    bool hasUnmatched=false;
    for(const auto &frame:frames)if(!frame.skipped&&!frame.hasCandidate){hasUnmatched=true;break;}
    const bool canMatch=!busy&&!m_session.matching()&&m_session.pendingSeeds().isEmpty()
        &&m_session.reference()>=0&&m_session.reference()<frames.size()
        &&frames.at(m_session.reference()).confirmed&&hasUnmatched&&!m_session.paused();
    available(ui->matchFrames,canMatch,tr("先确认参考面，并处理当前帧"));ui->matchFrames->setVisible(canMatch);
    const bool canContinue=!busy&&m_session.pendingSeeds().isEmpty()&&m_session.paused()&&f&&!f->problem&&(f->reviewed||f->skipped);
    available(ui->continueMatching,canContinue,tr("先处理或跳过当前帧"));ui->continueMatching->setVisible(canContinue);
    const bool canStitch=m_session.canStitch();
    available(ui->stitchFrames,m_workflowStage==2&&canStitch,tr("至少两帧有效且均已审核"));ui->stitchFrames->setVisible(m_workflowStage==2);
    ui->stitchFrames->setText(m_session.hasResult()?tr("重新拼接"):tr("开始拼接"));
    ui->confirmCandidate->setVisible(m_workflowStage==1&&canConfirmFrame);
    ui->matchFrames->setVisible(m_workflowStage==1&&canMatch);
    ui->continueMatching->setVisible(m_workflowStage==1&&canContinue);
    ui->skipFrame->setVisible(m_workflowStage==1&&navigate&&f&&!f->skipped);
    ui->moreActions->setVisible(m_workflowStage==1||m_workflowStage==3);
    ui->processingStatus->setText(m_session.summary());
    ui->processingStatus->setToolTip(m_session.status());
    ui->processingProgress->setVisible(busy);
    ui->processingProgress->setRange(0,busy&&m_session.progress()<0?0:100);
    ui->processingProgress->setValue(qMax(0,m_session.progress()));
    ui->processingProgress->setTextVisible(m_session.progress()>=0);
    available(ui->rectangleTool,editable,tr("仅在拼接结果中可修剪"),tr("矩形预览仅选可见点，点击擦除按钮后才删除"));
    available(ui->undoTrim,editable&&m_session.canUndo(),tr("当前没有可撤销修剪"),tr("撤销最近一次会话修剪（Ctrl+Z）"));
    available(ui->restoreTrim,editable&&!m_session.removed().isEmpty(),tr("当前没有已擦除点"),tr("恢复完整拼接结果，此恢复操作也可撤销"));
    ui->selectWorkpiece->setChecked(m_view->tool()==ScanGeometryCanvasV2::Tool::Workpiece);
    ui->selectInterference->setChecked(m_view->tool()==ScanGeometryCanvasV2::Tool::Interference);
    ui->rectangleTool->setChecked(m_view->tool()==ScanGeometryCanvasV2::Tool::Rectangle);
    ui->geometryCard->setVisible(m_workflowStage==3&&resultView);
    const auto &regions=m_session.resultRegions();
    const bool regionsBusy=regions.state==ResultRegionStateV1::Building;
    ui->resultRegionStatus->setText(regionsBusy?tr("正在生成蓝色工件模板区域；可浏览，修剪和保存暂不可用")
        :regions.state==ResultRegionStateV1::Ready?tr("灰色实测点云 / 蓝色工件模板 / 红色干涉面；颜色仅用于显示")
        :tr("点云及磁盘结果保留，区域显示未完成：%1").arg(regions.error.isEmpty()?tr("请点击重建区域"):regions.error));
    ui->rebuildResultRegions->setVisible(regions.state!=ResultRegionStateV1::Ready);
    available(ui->rebuildResultRegions,editable&&!regionsBusy,tr("请先取消或等待当前任务结束"));
    ui->workpieceSummary->setText(f?tr("工件面 %1 点 · %2").arg(f->planeIndices.size()).arg(f->status):tr("所选平面拼接结果"));
    ui->interferenceSummary->setText(tr("干涉面：%1 点，预览 %2 点\n人工标注，不参与拼接；%3").arg(f?f->interferenceIndices.size():0).arg(f?f->pendingInterference.size():0)
        .arg(m_session.geometryConfirmed()?tr("已随几何结果保存"):tr("待保存几何结果")));
    QStringList maskErrors;
    if(sourceVerified&&!f->workpieceMaskError.isEmpty())maskErrors.append(tr("工件面：%1").arg(f->workpieceMaskError));
    if(sourceVerified&&!f->interferenceMaskError.isEmpty())maskErrors.append(tr("干涉面：%1").arg(f->interferenceMaskError));
    if(sourceVerified&&!f->pendingInterferenceMaskError.isEmpty())maskErrors.append(tr("干涉面预览：%1").arg(f->pendingInterferenceMaskError));
    ui->interactionHint->setToolTip(maskErrors.isEmpty()?QString():tr("平面拟合成功，区域显示失败：%1").arg(maskErrors.join("\n")));
    const QSignalBlocker highSideBlocker(ui->highSideCandidates);
    ui->highSideCandidates->clear();
    if(sourceVerified)for(const auto &candidate:f->highSideCandidates)
        ui->highSideCandidates->addItem(tr("+%1 mm · %2 点").arg(candidate.heightMm,0,'f',2).arg(candidate.pointCount),candidate.seedIndex);
    const bool hasHighSide=sourceVerified&&!f->highSideCandidates.isEmpty();
    ui->renderAndHeightNotice->setVisible(loaded||hasHighSide||(f&&f->highSideChecking));
    const bool highSidePending=f&&!f->pendingInterference.isEmpty()&&hasHighSide
        &&std::any_of(f->highSideCandidates.cbegin(),f->highSideCandidates.cend(),[f](const auto &candidate){return f->pendingInterference.contains(candidate.seedIndex);});
    ui->highSideNotice->setText(!f?QString():!sourceVerified?tr("来源尚未校验，旧标注暂不显示"):f->highSideChecking?tr("正在检查未标注的高侧平面…")
        :highSidePending?tr("高侧候选已载入干涉面预览；请在“更多”中确认标注。")
        :hasHighSide?tr("发现 %1 个世界 Z 高侧平面候选。提醒可忽略，不会自动进入拼接。").arg(f->highSideCandidates.size())
        :!f->highSideError.isEmpty()?tr("高侧平面检查失败：%1").arg(f->highSideError)
        :f->hasCandidate?tr("未发现符合门限的未标注高侧平面"):tr("请先选择工件面以检查高侧平面"));
    available(ui->highSideCandidates,hasHighSide&&navigate,tr("当前帧没有待处理高侧候选"));
    available(ui->markHighSideCandidate,hasHighSide&&navigate&&!highSidePending,tr("请选择待处理候选"),tr("按候选种子执行常规平面选择；确认后才记录为干涉面"));
    available(ui->ignoreHighSideCandidate,hasHighSide&&navigate&&!highSidePending,tr("当前帧没有待处理高侧候选"));
    ui->highSideCandidates->setVisible(hasHighSide);ui->markHighSideCandidate->setVisible(hasHighSide);
    ui->ignoreHighSideCandidate->setVisible(hasHighSide);
    available(ui->confirmGeometry,editable&&m_session.canConfirmGeometry()&&!m_session.geometryConfirmed(),tr("需保留至少三个非共线点"));
    ui->confirmGeometry->setText(m_session.geometryConfirmed()?tr("几何已保存"):tr("确认并保存几何"));
    ui->confirmationStatus->setText(m_session.geometryConfirmed()?tr("已保存 · %1").arg(QFileInfo(m_session.savedGeometryPath()).fileName()):tr("WObj1 将自动生成"));
    ui->confirmationStatus->setToolTip(m_session.savedGeometryPath());
    const bool canRegenerateWobj=editable&&m_session.canConfirmGeometry()&&m_session.wobj1().valid&&!m_session.geometryConfirmed();
    ui->generateWobj1->setVisible(canRegenerateWobj);ui->generateWobj1->setText(tr("重新生成 WObj1"));
    available(ui->generateWobj1,canRegenerateWobj,tr("当前无需重新生成 WObj1"));
    const auto &w=m_session.wobj1();
    const QList<QLabel *> values{ui->wobjX,ui->wobjY,ui->wobjZ,ui->wobjA,ui->wobjB,ui->wobjC};
    const QList<double> coordinates{w.originInRobotBase.x(),w.originInRobotBase.y(),w.originInRobotBase.z(),w.poseA,w.poseB,w.poseC};
    const QStringList names{"X","Y","Z","A","B","C"};
    for(int i=0;i<6;++i)values.at(i)->setText(names.at(i)+"  "+(w.valid?QString::number(coordinates.at(i),'f',3):tr("待生成")));
    ui->wobjLabel->setText(w.valid?tr("WObj1 · XYZ (%1, %2, %3) · ABC (%4, %5, %6)")
        .arg(coordinates.at(0),0,'f',2).arg(coordinates.at(1),0,'f',2).arg(coordinates.at(2),0,'f',2)
        .arg(coordinates.at(3),0,'f',2).arg(coordinates.at(4),0,'f',2).arg(coordinates.at(5),0,'f',2):tr("WObj1 · 待生成"));
    const auto axis=[](const QVector3D &v){return QString("[%1, %2, %3]").arg(v.x(),0,'f',4).arg(v.y(),0,'f',4).arg(v.z(),0,'f',4);};
    ui->wobjLabel->setToolTip(w.valid?tr("单位 mm / deg\n原点 %1\nX %2\nY %3\nZ %4\n正交误差 %5").arg(axis(w.originInRobotBase),axis(w.axisXInRobotBase),axis(w.axisYInRobotBase),axis(w.axisZInRobotBase)).arg(w.orthogonalityError):w.error);
    ui->footerNotice->setText(m_session.geometryConfirmed()?tr("已保存 · 完整结果保留"):tr("修剪后需保存"));
    ui->footerNotice->setToolTip(tr("人工干涉标注不代表碰撞检测；修剪结果作为派生文件保存。"));
    ui->moreActions->setToolTip(tr("干涉面 %1 点 · 待确认 %2 点").arg(f?f->interferenceIndices.size():0).arg(f?f->pendingInterference.size():0));
    available(ui->fitView,loaded||editable,tr("尚无可显示点云"));available(ui->topView,loaded||editable,tr("尚无可显示点云"));
    QString hint=tr("左键旋转 · 右键平移 · 滚轮缩放");
    if(m_view->tool()==ScanGeometryCanvasV2::Tool::Rectangle)hint=tr("拖框仅选可见点 → 点击擦除；Esc 取消 · Ctrl+Z 撤销。磁盘结果不变。");
    if(m_view->tool()==ScanGeometryCanvasV2::Tool::Workpiece||m_view->tool()==ScanGeometryCanvasV2::Tool::Interference)
        hint=m_session.config().threePoint?tr("依次点击三个不同、非共线点；Esc 取消。完整点集上选面后预览确认。"):tr("点击面内点；完整点集上扩展平面，预览后确认。Esc 退出。");
    ui->interactionHint->setText(hint);
    if(sourceVerified&&!maskErrors.isEmpty())ui->interactionHint->setText(hint+tr("\n平面拟合成功，区域显示失败；可继续确认，详情见提示。"));
    qsizetype count=0;
    if(!m_session.hasResult()) {
        m_resultDisplay.clear();m_resultMapping.clear();m_resultRemoved.clear();m_cachedResultRevision=0;
    }
    if(publish) {
        QVector<pointcloud::Point3D> points;QVector<int> mapping,work,interference,pending;qsizetype fullCount=0;
        const QVector<pointcloud::Point3D> *fullPoints=nullptr;
        const pointcloud::Point3D *cloudKey=nullptr;
        if(f&&loaded){points=f->display;mapping=f->displayToFull;fullCount=f->cloud.fullPoints.size();fullPoints=&f->cloud.fullPoints;cloudKey=f->cloud.fullPoints.constData();if(sourceVerified){work=f->planeIndices;interference=f->interferenceIndices;pending=f->pendingInterference;}}
        else if(resultView){const auto &full=m_session.result().points;fullPoints=&full;fullCount=full.size();cloudKey=full.constData();
            if(m_cachedResultRevision!=m_session.resultRevision()||m_resultRemoved!=m_session.removed()) {
                m_resultDisplay.clear();m_resultMapping.clear();
                m_resultDisplay.reserve(full.size()-m_session.removed().size());m_resultMapping.reserve(m_resultDisplay.capacity());
                for(int i=0;i<full.size();++i)if(!m_session.removed().contains(i)){m_resultDisplay.append(full.at(i));m_resultMapping.append(i);}
                m_cachedResultRevision=m_session.resultRevision();m_resultRemoved=m_session.removed();
            }
            points=m_resultDisplay;mapping=m_resultMapping;}
        count=points.size();const bool fit=m_lastActive!=m_session.active()||m_lastCloud!=cloudKey;
        m_lastActive=m_session.active();m_lastCloud=cloudKey;
        if(depthPreview) {m_view->bindLoadObservation(0,0);m_view->showDepthFrame(f->rawDepthFrame);m_view->showEmptyMessage(QString());}
        else {
            m_view->bindLoadObservation(f?f->id:0,f?f->loadAttempt:0);
            m_view->showPointCloud();
            m_view->publish(points,mapping,fullCount,work,interference,pending,f&&!f->reviewed,fit,f?0:m_session.resultRevision(),
                fullPoints?*fullPoints:QVector<pointcloud::Point3D>{},resultView?&regions.workpiece:loaded&&sourceVerified?&f->workpieceMask:nullptr,
                resultView?&regions.interference:loaded&&sourceVerified?&f->interferenceMask:nullptr,loaded&&sourceVerified?&f->pendingInterferenceMask:nullptr,displayMode,
                loaded?QByteArray("frame:")+QByteArray::number(f->id)+":"+QByteArray::number(f->loadAttempt)
                    :resultView?QByteArray("result:")+QByteArray::number(m_session.resultRevision()):QByteArray{},
                loaded&&f->hasHeightRange?std::optional<QPair<float,float>>(f->heightRange)
                    :resultView?std::optional<QPair<float,float>>(m_session.resultHeightRange())
                    :std::optional<QPair<float,float>>(QPair<float,float>{0,1}));
            m_view->showEmptyMessage(points.isEmpty()?tr("当前没有可显示点云\n请配置并加载，或撤销 / 恢复修剪"):QString());
        }
    }else count=f?f->display.size():m_session.hasResult()?m_session.result().points.size()-m_session.removed().size():0;
    ui->pointCount->setText(tr("显示 %1 点 · 会话擦除 %2 点").arg(count).arg(m_session.removed().size()));
    m_view->canvas()->setControlPointIndices(navigate&&m_session.config().threePoint
        &&(m_view->tool()==ScanGeometryCanvasV2::Tool::Workpiece||m_view->tool()==ScanGeometryCanvasV2::Tool::Interference)
        ?m_session.pendingSeeds():QVector<int>{});
    if(w.valid&&m_session.active()==-1) {
        pcv::render::CoordinateFrame frame;
        frame.originInRobotBase=w.originInRobotBase;frame.axisXInRobotBase=w.axisXInRobotBase;
        frame.axisYInRobotBase=w.axisYInRobotBase;frame.axisZInRobotBase=w.axisZInRobotBase;
        frame.workpieceToRobotBase=w.workpieceToRobotBase;frame.robotBaseToWorkpiece=w.robotBaseToWorkpiece;
        frame.poseA=w.poseA;frame.poseB=w.poseB;frame.poseC=w.poseC;frame.valid=true;
        m_view->canvas()->setWorkpieceCoordinateSystem(frame);
    } else m_view->canvas()->clearWorkpieceCoordinateSystem();
}
void ScanGeometryWindowV1::closeEvent(QCloseEvent *event) {
    QString message;
    if(m_session.busy())message=tr("关闭将取消并等待后台任务结束。已提交的正式产物保留。");
    if(!m_session.removed().isEmpty()&&!m_session.geometryConfirmed())message+=tr("\n未保存的会话修剪会丢失，磁盘结果不变。");
    if(!message.isEmpty()&&QMessageBox::question(this,tr("关闭会话"),message,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes){event->ignore();return;}
    const int committedBefore=m_session.committedOutputs().size();m_session.shutdown();
    if(m_session.committedOutputs().size()>committedBefore)
        QMessageBox::information(this,tr("正式产物已提交"),tr("关闭前事务已完成，以下文件已保留：\n%1").arg(m_session.committedOutputs().mid(committedBefore).join("\n")));
    event->accept();
}
}
