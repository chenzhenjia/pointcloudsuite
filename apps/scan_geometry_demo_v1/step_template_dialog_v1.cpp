#include "step_template_dialog_v1.h"
#include "ui_step_template_dialog_v1.h"
#include <pcv/render/cad_mesh_canvas_v1.h>
#include <pcv/render/pointcloud_canvas.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/io/artifact_transaction.h>
#include <QtConcurrent/QtConcurrentRun>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QPointer>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <exception>
namespace pcv::demo {
namespace {
void displaySubset(const QVector<pointcloud::Point3D> &points,QVector<pointcloud::Point3D> &display,QVector<int> &mapping) {
    qsizetype n=std::min<qsizetype>(points.size(),1000000);display.reserve(n);mapping.reserve(n);
    for(qsizetype i=0;i<n;++i){int index=int(quint64(i)*quint64(points.size())/quint64(n));display.append(points.at(index));mapping.append(index);}
}
pcv::render::PlanarMaskOverlay overlay(const pcv::workpiece::TemplatePlaneV1 &plane,const QMatrix4x4 &transform) {
    pcv::render::PlanarMaskOverlay out;out.triangleIndices=plane.region.triangleIndices;out.boundaryRanges=plane.region.boundaryRanges;
    out.color=plane.interference?QVector4D(240/255.f,68/255.f,82/255.f,1):QVector4D(22/255.f,117/255.f,232/255.f,1);out.opacity=.25f;
    out.vertices.reserve(plane.region.vertices.size());out.boundaryVertices.reserve(plane.region.boundaryVertices.size());
    for(auto v:plane.region.vertices)out.vertices.append(transform.map(v));for(auto v:plane.region.boundaryVertices)out.boundaryVertices.append(transform.map(v));return out;
}
bool externalRoot(const QString &path) {
    QFileInfo current(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));QStringList suffix;
    while(!current.exists()&&!current.isRoot()){suffix.prepend(current.fileName());current=QFileInfo(current.absolutePath());}
    QString resolved=current.canonicalFilePath();if(resolved.isEmpty())resolved=current.absoluteFilePath();for(auto part:suffix)resolved=QDir(resolved).filePath(part);
    resolved=QDir::fromNativeSeparators(QDir::cleanPath(resolved));const QString source=QDir::fromNativeSeparators(QFileInfo(QString::fromUtf8(PCV_STEP_TEMPLATE_SOURCE_ROOT)).canonicalFilePath());
    return !source.isEmpty()&&resolved.compare(source,Qt::CaseInsensitive)!=0&&!resolved.startsWith(source+"/",Qt::CaseInsensitive);
}
}
StepTemplateDialogV1::StepTemplateDialogV1(ScanGeometrySessionV2 *session,QWidget *parent)
    :QDialog(parent),ui(std::make_unique<Ui::StepTemplateDialogV1>()),m_session(session) {
    ui->setupUi(this);resize(1280,800);m_pool.setMaxThreadCount(1);m_pool.setExpiryTimeout(-1);
    m_cad=new pcv::render::CadMeshCanvasV1(ui->cadHost);m_cad->setAppearance(QColor("#e9edf2"),QColor("#89929e"));ui->cadCanvasLayout->addWidget(m_cad);
    m_canvas=new PointCloudCanvas(ui->matchHost);m_canvas->setAppearance(QColor("#e9edf2"),QColor("#59636f"));m_canvas->setUniformPointColor(QColor("#59636f"));m_canvas->setPointSize(2);
    auto options=m_canvas->overlayOptions();options.showColorBar=false;m_canvas->setOverlayOptions(options);ui->matchCanvasLayout->addWidget(m_canvas);
    const auto root=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("logs/step_template_match_v1");QDir().mkpath(root);
    m_logPath=QDir(root).filePath(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128)+".jsonl");
    connect(ui->importStep,&QPushButton::clicked,this,[this]{importStep(false);});connect(ui->reopenStep,&QPushButton::clicked,this,[this]{importStep(true);});
    m_cad->faceClicked=[this](int id){chooseFace(id);};
    connect(ui->faces,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){chooseFace(item->data(Qt::UserRole).toInt());});
    connect(ui->clearFaces,&QPushButton::clicked,this,[this]{m_work.clear();m_interference.clear();invalidateTemplate();refreshFaces();});
    connect(ui->spacing,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{invalidateTemplate();refreshFaces();});
    connect(ui->generateTemplate,&QPushButton::clicked,this,&StepTemplateDialogV1::generate);
    connect(ui->saveTemplate,&QPushButton::clicked,this,&StepTemplateDialogV1::savePackage);
    connect(ui->loadTemplate,&QPushButton::clicked,this,[this]{loadPackage(false);});connect(ui->loadTemplateDirectory,&QPushButton::clicked,this,[this]{loadPackage(true);});
    connect(ui->startMatch,&QPushButton::clicked,this,&StepTemplateDialogV1::match);
    auto parameterChanged=[this]{++m_parameterRevision;invalidateMatch();};
    connect(ui->distance,qOverload<double>(&QDoubleSpinBox::valueChanged),this,parameterChanged);connect(ui->coverage,qOverload<double>(&QDoubleSpinBox::valueChanged),this,parameterChanged);
    for(auto box:{ui->matches,ui->hypotheses,ui->refinements})connect(box,qOverload<int>(&QSpinBox::valueChanged),this,parameterChanged);
    connect(ui->candidates,&QListWidget::currentRowChanged,this,[this]{m_confirmed=-1;preview();updateControls();});
    connect(ui->confirmCandidate,&QPushButton::clicked,this,[this]{int row=ui->candidates->currentRow();if(row<0||row>=m_match.candidates.size()||!m_match.candidates.at(row).acceptable)return;
        m_confirmed=row;ui->status->setText(tr("已人工确认候选 %1，可以另存匹配结果").arg(row+1));log("candidate_confirmed",{{"candidate",row}});updateControls();});
    connect(ui->saveMatch,&QPushButton::clicked,this,&StepTemplateDialogV1::saveMatch);
    connect(ui->cancelTask,&QPushButton::clicked,this,[this]{if(m_cancel)m_cancel->store(true);log("cancel_requested");ui->status->setText(tr("正在取消，等待后台任务收敛…"));});
    connect(ui->closeDialog,&QPushButton::clicked,this,&QDialog::close);
    connect(ui->openLogs,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_logPath).absolutePath()));});
    connect(m_session,&ScanGeometrySessionV2::changed,this,[this](bool){const auto revision=m_session->resultRevision(),trim=m_session->trimRevision();
        if(revision!=m_targetRevision||trim!=m_targetTrimRevision){m_targetRevision=revision;m_targetTrimRevision=trim;if(m_targetTask&&m_cancel)m_cancel->store(true);invalidateMatch();m_targetIdentity.clear();m_canvas->setCloud({});ui->status->setText(tr("目标结果或修剪已变化，旧匹配已失效"));}updateControls();});
    m_targetRevision=m_session->resultRevision();m_targetTrimRevision=m_session->trimRevision();
    log("dialog_opened",{{"step_import_enabled",
#ifdef PCV_BUILD_STEP_IMPORT
        true
#else
        false
#endif
    }});updateControls();
}
StepTemplateDialogV1::~StepTemplateDialogV1(){shutdown();}
void StepTemplateDialogV1::shutdown(){if(m_closing)return;m_closing=true;if(m_cancel)m_cancel->store(true);m_watcher.waitForFinished();m_pool.waitForDone();
    if(m_busy&&m_watcher.future().resultCount()>0){const auto result=m_watcher.result();log("close_wait_completed",{{"committed",result.committed},{"path",result.path},{"error",result.error}});
        if(result.committed)QMessageBox::information(this,tr("事务已提交"),tr("关闭前任务已完成落盘：%1").arg(result.path));}
}
void StepTemplateDialogV1::closeEvent(QCloseEvent *event){shutdown();event->accept();}
void StepTemplateDialogV1::reject(){shutdown();QDialog::reject();}
void StepTemplateDialogV1::log(QString event,QJsonObject fields){fields.insert("event",event);fields.insert("timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));fields.insert("template_revision",QString::number(m_templateRevision));fields.insert("target_revision",QString::number(m_targetRevision));fields.insert("trim_revision",QString::number(m_targetTrimRevision));
    QFile file(m_logPath);if(file.open(QIODevice::WriteOnly|QIODevice::Append)){file.write(QJsonDocument(fields).toJson(QJsonDocument::Compact));file.write("\n");}else ui->events->appendPlainText(tr("日志写入失败：%1").arg(file.errorString()));
    ui->events->appendPlainText(QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact)));}
void StepTemplateDialogV1::run(QString kind,std::function<Outcome(const std::function<bool()> &)> work,std::function<void(Outcome)> accept,bool dependsOnTarget) {
    if(m_busy||m_closing||m_session->busy())return;m_busy=true;m_targetTask=dependsOnTarget;m_cancel=std::make_shared<std::atomic_bool>(false);auto cancel=m_cancel;auto serial=++m_taskSerial;
    const auto templateRevision=m_templateRevision,parameterRevision=m_parameterRevision,targetRevision=m_targetRevision,trimRevision=m_targetTrimRevision;
    ui->status->setText(kind);log("task_started",{{"kind",kind},{"task",QString::number(serial)}});updateControls();
    disconnect(&m_watcher,nullptr,this,nullptr);
    connect(&m_watcher,&QFutureWatcher<Outcome>::finished,this,[this,kind,serial,templateRevision,parameterRevision,targetRevision,trimRevision,accept=std::move(accept),cancel,dependsOnTarget]()mutable{
        Outcome result=m_watcher.result();m_busy=false;
        const bool stale=serial!=m_taskSerial||templateRevision!=m_templateRevision||parameterRevision!=m_parameterRevision||(dependsOnTarget&&(targetRevision!=m_targetRevision||trimRevision!=m_targetTrimRevision));
        log("task_finished",{{"kind",kind},{"task",QString::number(serial)},{"success",result.ok},{"cancelled",cancel->load()},{"stale",stale},{"committed",result.committed},{"path",result.path},{"error",result.error},{"timings",result.timings}});
        if(result.committed)ui->status->setText(tr("事务已提交：%1").arg(result.path));
        if(m_closing)return;
        if(stale||cancel->load()){if(!result.committed)ui->status->setText(tr("任务取消或结果过期，未发布结果"));}
        else if(!result.ok&&!result.publishFailedSearch)ui->status->setText(result.error.isEmpty()?tr("任务未完成"):result.error);
        else accept(std::move(result));updateControls();
    });
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[work=std::move(work),cancel]{QElapsedTimer timer;timer.start();Outcome result;
        try{result=work([cancel]{return cancel->load();});}catch(const std::bad_alloc &){result.error=QStringLiteral("内存不足，任务未发布");}catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}catch(...){result.error=QStringLiteral("后台任务出现未知错误");}
        result.timings.insert("worker_ms",timer.nsecsElapsed()/1e6);return result;}));
}
void StepTemplateDialogV1::updateControls(){bool idle=!m_busy&&!m_closing&&!m_session->busy();bool step=false;
#ifdef PCV_BUILD_STEP_IMPORT
    step=true;
#endif
    ui->importStep->setEnabled(idle&&step);ui->importStep->setToolTip(step?QString():tr("此构建未启用 PCV_BUILD_STEP_IMPORT；仍可读取模板并匹配"));
    ui->reopenStep->setEnabled(idle&&step&&m_package.source.value("cad").isObject());
    ui->faceRole->setEnabled(idle);ui->faces->setEnabled(idle);ui->spacing->setEnabled(idle);ui->clearFaces->setEnabled(idle&&m_model.success);
    ui->generateTemplate->setEnabled(idle&&m_model.success&&!m_work.isEmpty());ui->saveTemplate->setEnabled(idle&&m_package.ok);
    ui->loadTemplate->setEnabled(idle);ui->loadTemplateDirectory->setEnabled(idle);
    for(auto w:{static_cast<QWidget *>(ui->distance),static_cast<QWidget *>(ui->coverage),static_cast<QWidget *>(ui->matches),static_cast<QWidget *>(ui->hypotheses),static_cast<QWidget *>(ui->refinements)})w->setEnabled(idle);
    ui->startMatch->setEnabled(idle&&!m_session->busy()&&m_package.ok&&m_session->hasResult()&&m_session->result().points.size()-m_session->removed().size()>=100);
    ui->startMatch->setToolTip(tr("目标必须为至少 100 点的当前完整修剪结果；扫描后台任务需先收敛"));
    ui->candidates->setEnabled(idle);int row=ui->candidates->currentRow();const bool valid=row>=0&&row<m_match.candidates.size()&&m_match.candidates.at(row).acceptable&&m_resultTemplateRevision==m_templateRevision&&m_resultParameterRevision==m_parameterRevision&&m_resultTargetRevision==m_targetRevision&&m_resultTrimRevision==m_targetTrimRevision;
    ui->confirmCandidate->setEnabled(idle&&valid);ui->saveMatch->setEnabled(idle&&valid&&m_confirmed==row);ui->cancelTask->setEnabled(m_busy);
    ui->templateInfo->setText(m_package.ok?tr("%1 · 工件 %2 点 / 干涉 %3 点\n模板身份 %4").arg(m_package.coordinateFrame).arg(m_package.points.size()).arg(m_package.interferencePoints.size()).arg(m_package.identity.left(16)):tr("尚未读取或生成有效模板"));
}
void StepTemplateDialogV1::invalidateMatch(){m_match={};m_confirmed=-1;{QSignalBlocker blocker(ui->candidates);ui->candidates->clear();}m_canvas->clearPlanarMaskOverlays();updateControls();}
void StepTemplateDialogV1::invalidateTemplate(){if(m_busy)return;++m_templateRevision;m_package={};invalidateMatch();m_canvas->setCloud({});}
void StepTemplateDialogV1::refreshFaces(){ui->faces->clear();QHash<int,QColor> colors;double spacing=ui->spacing->value();
    for(const auto &f:m_model.faces){QString role=m_work.contains(f.id)?tr("工件"):m_interference.contains(f.id)?tr("干涉"):tr("未选");
        auto *item=new QListWidgetItem(tr("面 %1 · %2 · %3\n%4 mm² · 预计 %5 点").arg(f.id).arg(f.surfaceType,role).arg(f.areaMm2,0,'f',2).arg(std::ceil(f.areaMm2/spacing/spacing),0,'f',0),ui->faces);item->setData(Qt::UserRole,f.id);
        if(m_work.contains(f.id))colors.insert(f.id,QColor("#1675e8"));if(m_interference.contains(f.id))colors.insert(f.id,QColor("#f04452"));}
    m_cad->setFaceColors(colors);updateControls();}
void StepTemplateDialogV1::chooseFace(int id){if(m_busy||!m_model.success)return;
    auto it=std::find_if(m_model.faces.cbegin(),m_model.faces.cend(),[id](const auto &f){return f.id==id;});if(it==m_model.faces.cend())return;
    if(!it->planar){ui->status->setText(tr("面 %1 是 %2，仅支持解析平面，不将曲面近似成平面").arg(id).arg(it->surfaceType));return;}
    int mode=ui->faceRole->currentIndex();bool selected=(mode==0?m_work:m_interference).contains(id);m_work.remove(id);m_interference.remove(id);
    if(mode<2&&!selected)(mode==0?m_work:m_interference).insert(id);invalidateTemplate();refreshFaces();log("face_role_changed",{{"face_id",id},{"mode",mode}});
}
void StepTemplateDialogV1::importStep(bool reopen){QString file=QFileDialog::getOpenFileName(this,tr("读取 STEP"),{},tr("STEP (*.step *.stp);;所有文件 (*)"));if(file.isEmpty())return;
    const auto previous=m_package;run(tr("STEP 读取、单位转换及面准备"),[file,reopen,previous](const auto &cancel){Outcome out;pcv::workpiece::ReadStepModelRequest request;request.file=file;request.isCancelled=cancel;
        QElapsedTimer phase;phase.start();int stage=-1;QJsonObject phases;request.progress=[&](pcv::workpiece::CadStage current,qint64,qint64){if(int(current)==stage)return;if(stage>=0)phases.insert(QString::number(stage),phase.nsecsElapsed()/1e6);phase.restart();stage=int(current);};
        out.model=pcv::workpiece::readStepModel(request);if(stage>=0)phases.insert(QString::number(stage),phase.nsecsElapsed()/1e6);out.timings.insert("cad_stages_ms",phases);
        out.ok=out.model.success;out.error=out.model.message;
        if(out.ok&&reopen&&out.model.source.value("cad").toObject().value("sha256")!=previous.source.value("cad").toObject().value("sha256")){out.ok=false;out.error=QStringLiteral("STEP 来源指纹与模板不一致，不能重新选面");}return out;
    },[this,reopen,previous](Outcome out){m_model=std::move(out.model);m_work.clear();m_interference.clear();
        if(reopen)for(const auto &p:previous.planes)(p.interference?m_interference:m_work).insert(p.faceId);
        invalidateTemplate();m_cad->setMesh(m_model.mesh);refreshFaces();ui->modes->setCurrentIndex(0);ui->status->setText(tr("STEP 已就绪：cad_model / mm，请选择解析平面"));});}
void StepTemplateDialogV1::generate(){auto model=m_model;auto work=m_work,interference=m_interference;double spacing=ui->spacing->value();auto budget=m_session->processingBudget();
    run(tr("完整 CAD 面采样与模板区域生成"),[model,work,interference,spacing,budget](const auto &cancel){Outcome out;pcv::workpiece::TemplateFeatureOptionsV1 o;o.isCancelled=cancel;o.budget=budget;out.package=pcv::workpiece::generateStepTemplatePackageV1(model,work,interference,spacing,o);out.ok=out.package.ok;out.error=out.package.error;out.timings=out.package.timings;
        if(out.ok)displaySubset(out.package.points,out.display,out.mapping);return out;
    },[this](Outcome out){++m_templateRevision;m_package=std::move(out.package);invalidateMatch();m_canvas->setDisplayCloud(out.display,out.mapping,m_package.points.size());preview();ui->status->setText(tr("模板已生成，可另存或在使用模板页匹配"));});}
void StepTemplateDialogV1::loadPackage(bool directory){QString path=directory?QFileDialog::getExistingDirectory(this,tr("模板或几何结果目录")):QFileDialog::getOpenFileName(this,tr("模板 JSON"),{},tr("JSON (*.json)"));if(path.isEmpty())return;auto budget=m_session->processingBudget();
    run(tr("模板回读与完整平面特征准备"),[path,budget](const auto &cancel){Outcome out;pcv::workpiece::TemplateFeatureOptionsV1 o;o.isCancelled=cancel;o.budget=budget;out.package=pcv::workpiece::loadTemplatePackageV1(path,o);out.ok=out.package.ok;out.error=out.package.error;out.timings=out.package.timings;if(out.ok)displaySubset(out.package.points,out.display,out.mapping);return out;
    },[this](Outcome out){++m_templateRevision;m_package=std::move(out.package);m_model={};m_cad->clearMesh();m_work.clear();m_interference.clear();invalidateMatch();refreshFaces();m_canvas->setDisplayCloud(out.display,out.mapping,m_package.points.size());preview();ui->status->setText(tr("模板已读取，无需原 STEP 即可匹配"));});}
void StepTemplateDialogV1::savePackage(){bool ok=false;QString name=QInputDialog::getText(this,tr("模板名称"),tr("名称"),QLineEdit::Normal,QStringLiteral("workpiece"),&ok);if(!ok||name.trimmed().isEmpty())return;
    QString root=QFileDialog::getExistingDirectory(this,tr("源码树外模板输出根目录"));if(root.isEmpty())return;auto package=m_package;
    run(tr("事务保存版本化模板包"),[package,name,root](const auto &cancel){Outcome out;auto saved=pcv::workpiece::saveTemplatePackageV1(package,name,root,cancel);out.ok=saved.success;out.committed=saved.success;out.path=saved.outputDirectory;out.error=saved.message;return out;},[this](Outcome out){ui->status->setText(tr("模板包已提交：%1").arg(out.path));});}
pcv::matching::TemplateMatchParametersV1 StepTemplateDialogV1::parameters()const{pcv::matching::TemplateMatchParametersV1 p;p.correspondenceMm=ui->distance->value();p.minimumCoverage=ui->coverage->value()/100;p.minimumMatches=ui->matches->value();p.maximumHypotheses=ui->hypotheses->value();p.refineCount=ui->refinements->value();return p;}
void StepTemplateDialogV1::match(){if(m_busy||m_session->busy()||!m_session->hasResult()||!m_package.ok)return;
    auto full=m_session->result().points;auto removed=m_session->removed();auto package=m_package;auto params=parameters();auto budget=m_session->processingBudget();
    m_targetRevision=m_session->resultRevision();m_targetTrimRevision=m_session->trimRevision();m_targetRemoved=removed;invalidateMatch();
    QPointer<StepTemplateDialogV1> guard(this);const quint64 nextSerial=m_taskSerial+1;
    auto progress=[guard,nextSerial](const QString &stage,int done,int total){if(!guard)return;
        QMetaObject::invokeMethod(guard.data(),[guard,nextSerial,stage,done,total]{if(!guard||guard->m_closing||!guard->m_busy||guard->m_taskSerial!=nextSerial)return;
            guard->ui->status->setText(total>0?QStringLiteral("%1 · %2 / %3").arg(stage).arg(done).arg(total):stage);
            if(done==0||done==total)guard->log("task_stage",{{"stage",stage},{"completed",done},{"total",total}});
        },Qt::QueuedConnection);};
    run(tr("完整目标准备、全局搜索、六自由度精化与核验"),[full,removed,package,params,budget,progress](const auto &cancel){Outcome out;QElapsedTimer timer;timer.start();progress(QStringLiteral("准备完整修剪目标"),0,0);
        QVector<pointcloud::Point3D> target;target.reserve(full.size()-removed.size());for(int i=0;i<full.size();++i){if((i&4095)==0&&cancel()){out.error=QStringLiteral("已取消");return out;}if(!removed.contains(i))target.append(full.at(i));}
        out.targetIdentity=pcv::workpiece::templatePointIdentityV1(target,cancel);out.timings.insert("target_prepare_ms",timer.nsecsElapsed()/1e6);
        progress(QStringLiteral("准备完整目标平面与边界特征"),0,0);
        pcv::workpiece::TemplateFeatureOptionsV1 fo;fo.isCancelled=cancel;fo.budget=budget;auto features=pcv::workpiece::prepareTemplateFeaturesV1(target,fo);out.timings.insert("target_features",features.timings);
        if(!features.ok){out.error=features.error;if(!cancel()){displaySubset(target,out.display,out.mapping);out.publishFailedSearch=true;}return out;}
        pcv::matching::TemplateMatchRequestV1 request;request.templatePoints=package.points;request.templatePlanes=package.planes;request.targetPoints=target;request.targetPlanes=features.planes;request.parameters=params;request.budget=budget;request.isCancelled=cancel;
        request.progress=[progress](const QString &stage,int done,int total){if(done%64==0||done==total||done==0)progress(stage,done,total);};
        out.match=pcv::matching::matchTemplateV1(request);out.timings.insert("matching",out.match.timings);out.ok=out.match.success;out.error=out.match.error;
        // Keep the target view even when search reports no qualifying candidate.
        if(!cancel()){displaySubset(target,out.display,out.mapping);out.publishFailedSearch=true;}return out;
    },[this](Outcome out){m_match=std::move(out.match);m_targetIdentity=out.targetIdentity;m_resultTemplateRevision=m_templateRevision;m_resultParameterRevision=m_parameterRevision;m_resultTargetRevision=m_targetRevision;m_resultTrimRevision=m_targetTrimRevision;
        m_canvas->setDisplayCloud(out.display,out.mapping,m_session->result().points.size()-m_session->removed().size());
        for(int i=0;i<m_match.candidates.size();++i){const auto &c=m_match.candidates.at(i);auto pose=pointcloud::matrixToRobotPose(c.targetFromTemplate);
            auto *item=new QListWidgetItem(tr("候选 %1 · RMS %2 mm · 覆盖 %3%\n有效 %4 点 · 边界约束 %5\nXYZ %6 / %7 / %8 · ABC %9 / %10 / %11")
                .arg(i+1).arg(c.rmsMm,0,'f',3).arg(c.coverage*100,0,'f',1).arg(c.matchedPoints).arg(c.observability,0,'f',3)
                .arg(pose.x,0,'f',2).arg(pose.y,0,'f',2).arg(pose.z,0,'f',2).arg(pose.rx,0,'f',2).arg(pose.ry,0,'f',2).arg(pose.rz,0,'f',2),ui->candidates);item->setToolTip(tr("T_target_from_template；必须人工确认"));}
        if(!m_match.success){m_canvas->clearPlanarMaskOverlays();ui->status->setText(out.error);return;}
        ui->candidates->setCurrentRow(0);preview();ui->status->setText(m_match.searchLimited?tr("已找到候选；搜索达到上限，可能仍存在其他解，请审核"):tr("候选已就绪，请检查蓝红区域并人工确认"));},true);}
void StepTemplateDialogV1::preview(){if(!m_package.ok)return;QMatrix4x4 transform;int row=ui->candidates->currentRow();if(row>=0&&row<m_match.candidates.size())transform=m_match.candidates.at(row).targetFromTemplate;
    QVector<pcv::render::PlanarMaskOverlay> masks;for(const auto &p:m_package.planes)masks.append(overlay(p,transform));if(!m_canvas->setPlanarMaskOverlays(masks))ui->status->setText(tr("模板有效，但区域叠加显示失败"));}
void StepTemplateDialogV1::saveMatch(){int selected=m_confirmed;if(selected<0||selected>=m_match.candidates.size()||!m_match.candidates.at(selected).acceptable)return;
    if(m_resultTargetRevision!=m_session->resultRevision()||m_resultTrimRevision!=m_session->trimRevision()){invalidateMatch();return;}
    QString root=QFileDialog::getExistingDirectory(this,tr("源码树外匹配结果根目录"));if(root.isEmpty())return;if(!externalRoot(root)){QMessageBox::warning(this,tr("输出目录"),tr("匹配结果必须位于源码树外"));return;}
    QJsonArray candidates;for(const auto &c:m_match.candidates)candidates.append(pcv::matching::matchCandidateJsonV1(c));
    auto deleted=m_session->removed().values();std::sort(deleted.begin(),deleted.end());QJsonArray removed;for(int i:deleted)removed.append(i);
    QJsonObject document{{"schema_version","pcv-template-match-v1"},{"units","mm"},{"transform_direction","T_target_from_template"},
        {"transform_name",m_package.coordinateFrame=="cad_model"?"T_robot_base_from_cad_model":"T_robot_base_from_template"},
        {"template",QJsonObject{{"identity_sha256",m_package.identity},{"info_file",m_package.infoFile},{"coordinate_frame",m_package.coordinateFrame},{"point_count",qint64(m_package.points.size())},{"source",m_package.source}}},
        {"target",QJsonObject{{"identity_sha256",m_targetIdentity},{"coordinate_frame","robot_base"},{"result_revision",QString::number(m_resultTargetRevision)},{"trim_revision",QString::number(m_resultTrimRevision)},{"point_count",qint64(m_session->result().points.size()-m_session->removed().size())},
            {"original_result_ply",m_session->result().outputPly},{"original_result_report",m_session->result().reportFile},{"deleted_result_indices",removed}}},
        {"parameters",pcv::matching::matchParametersJsonV1(parameters())},{"candidates",candidates},{"selected_candidate",selected},{"result",pcv::matching::matchCandidateJsonV1(m_match.candidates.at(selected))},
        {"search_limited",m_match.searchLimited},{"evaluated_hypotheses",m_match.evaluatedHypotheses},{"user_confirmation",QJsonObject{{"confirmed",true},{"confirmed_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}};
    run(tr("事务保存匹配结果"),[root,document](const auto &cancel){Outcome out;out.path=QDir(root).filePath("template_match_v1_"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128));
        pcv::io::ArtifactTransaction transaction(out.path,false,cancel);out.ok=transaction.stageJson("template_match_v1.json",QJsonDocument(document).toJson(QJsonDocument::Indented))&&transaction.commit();out.committed=out.ok;out.error=transaction.error();return out;
    },[this](Outcome out){ui->status->setText(tr("匹配结果已另存：%1").arg(out.path));},true);}
}
