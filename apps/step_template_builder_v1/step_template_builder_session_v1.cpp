#include "step_template_builder_session_v1.h"
#include <pcv/core/processing_budget.h>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <exception>
#include <algorithm>
#include <utility>

namespace pcv::stepbuilder {
StepTemplateBuilderSessionV1::StepTemplateBuilderSessionV1(QObject *parent):QObject(parent),
    m_workpieceId("WP-"+QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    m_pool.setMaxThreadCount(1);m_pool.setExpiryTimeout(-1);
}
StepTemplateBuilderSessionV1::~StepTemplateBuilderSessionV1(){shutdown();}
void StepTemplateBuilderSessionV1::shutdown(){
    if(m_closing)return;
    m_closing=true;cancel();m_watcher.waitForFinished();m_pool.waitForDone();
    // A committed transaction remains a real artifact even if close arrived.
    if(m_watcher.future().isValid()&&m_watcher.isFinished()){
        const auto result=m_watcher.result();if(result.saved.success)m_saved=result.saved;
    }
}
void StepTemplateBuilderSessionV1::metadataChanged(){if(m_busy)return;invalidateGeometry();m_status=tr("工件信息已变化，请重新处理并确认几何");emit changed();}
void StepTemplateBuilderSessionV1::cancel(){if(m_cancel)m_cancel->store(true);}
void StepTemplateBuilderSessionV1::invalidateGeometry(){m_package={};m_confirmed=false;m_saved={};++m_revision;}
bool StepTemplateBuilderSessionV1::canProcess() const {
    if(m_busy||!m_cloud.success)return false;
    for(int id:m_work)if(m_retained.value(id)>0)return true;
    return false;
}
void StepTemplateBuilderSessionV1::start(Kind kind,const QString &status,Job job){
    if(m_busy||m_closing)return;
    m_busy=true;m_status=status;m_cancel=std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag=m_cancel;const auto revision=m_revision;
    disconnect(&m_watcher,nullptr,this,nullptr);
    connect(&m_watcher,&QFutureWatcher<Result>::finished,this,[this,kind,revision,cancelFlag]{
        const auto result=m_watcher.result();m_busy=false;
        if(m_closing)return;
        // A completed save is authoritative even if cancellation arrived after commit.
        if(kind==Kind::Save&&result.saved.success){m_saved=result.saved;m_status=tr("模板草案已保存");emit savedVersion(m_saved.outputDirectory);emit changed();return;}
        if(revision!=m_revision||cancelFlag->load()){m_status=tr("任务已取消，未发布结果");emit changed();return;}
        if(!result.ok){m_status=result.error;emit changed();return;}
        if(kind==Kind::Import){
            m_model=result.model;m_cloud=result.cloud;m_work.clear();m_interference.clear();
            m_removed.clear();m_pending.clear();m_history.clear();invalidateGeometry();
        }
        if(kind==Kind::Import||kind==Kind::Trim){
            m_display=result.display;m_mapping=result.mapping;m_retained=result.retained;
            if(kind==Kind::Trim){
                m_removed=result.removed;
                if(m_historyAction==1)m_history.append(m_historyDelta);
                else if(m_historyAction==-1&&!m_history.isEmpty())m_history.removeLast();
                else if(m_historyAction==0)m_history.clear();
                m_pending.clear();invalidateGeometry();
            }
            emit displayChanged();
        }
        if(kind==Kind::Selection)m_pending=result.pending;
        if(kind==Kind::Process){m_package=result.package;m_confirmed=false;}
        m_status=kind==Kind::Selection?tr("框选 %1 点，点击擦除确认").arg(m_pending.size()):
            kind==Kind::Process?tr("处理完成，请复核并确认几何结果"):tr("点云已更新");
        emit changed();
    });
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[job,cancelFlag]{
        try{return job(cancelFlag);}catch(const std::exception &e){Result r;r.error=QString::fromUtf8(e.what());return r;}
        catch(...){Result r;r.error=QStringLiteral("后台任务发生未知错误");return r;}
    }));
    emit changed();
}
bool StepTemplateBuilderSessionV1::buildDisplay(Result &r,const pcv::workpiece::CadCloudResult &cloud,
    const std::shared_ptr<std::atomic_bool> &cancelFlag){
    const auto &removed=std::as_const(r.removed);
    const qsizetype kept=cloud.points.size()-removed.size();
    const qsizetype count=std::min<qsizetype>(kept,1000000);
    r.display.reserve(count);r.mapping.reserve(count);
    qsizetype ordinal=0,chosen=0;qsizetype face=0;
    for(qsizetype i=0;i<cloud.points.size();++i){
        if((i&4095)==0&&cancelFlag->load())return false;
        while(face<cloud.faceRanges.size()&&i>=cloud.faceRanges.at(face).first+cloud.faceRanges.at(face).count)++face;
        if(removed.contains(int(i)))continue;
        if(face<cloud.faceRanges.size())++r.retained[cloud.faceRanges.at(face).faceId];
        if(chosen<count&&ordinal==qsizetype(quint64(chosen)*quint64(kept)/quint64(count))){
            r.display.append(cloud.points.at(i));r.mapping.append(int(i));++chosen;
        }
        ++ordinal;
    }
    return !cancelFlag->load();
}
void StepTemplateBuilderSessionV1::importStep(const QString &file,double spacing){
    if(m_busy)return;
    // A replacement attempt invalidates earlier geometry, including on failure.
    m_model={};m_cloud={};m_removed.clear();m_history.clear();m_pending.clear();m_work.clear();m_interference.clear();
    m_display.clear();m_mapping.clear();m_retained.clear();invalidateGeometry();emit displayChanged();
    start(Kind::Import,tr("正在读取 STEP 并转换完整点云…"),[file,spacing](const auto &cancelFlag){
        Result r;pcv::workpiece::ReadStepModelRequest req;req.file=file;req.isCancelled=[cancelFlag]{return cancelFlag->load();};
        r.model=pcv::workpiece::readStepModel(req);
        if(!r.model.success){r.error=r.model.message;return r;}
        pcv::workpiece::GenerateCadTemplateRequest gen;gen.model=r.model.model;gen.spacingMm=spacing;gen.isCancelled=req.isCancelled;
        for(const auto &face:std::as_const(r.model.faces))gen.faceIds.append(face.id);
        r.cloud=pcv::workpiece::generateCadTemplateCloud(gen);
        if(!r.cloud.success){r.error=r.cloud.message+QStringLiteral("\n点数超限时请增大采样间距后重新导入。");return r;}
        r.ok=buildDisplay(r,r.cloud,cancelFlag);return r;
    });
}
void StepTemplateBuilderSessionV1::invalidateSampling(){
    if(m_busy)return;
    m_model={};m_cloud={};m_display.clear();m_mapping.clear();m_retained.clear();m_removed.clear();m_history.clear();m_pending.clear();
    m_work.clear();m_interference.clear();invalidateGeometry();m_status=tr("采样间距已改变，请重新导入 STEP");emit displayChanged();emit changed();
}
void StepTemplateBuilderSessionV1::toggleFace(int id,bool interference){
    if(m_busy||!m_cloud.success)return;
    auto it=std::find_if(m_model.faces.cbegin(),m_model.faces.cend(),[id](const auto &f){return f.id==id;});
    if(it==m_model.faces.cend())return;
    if(!it->planar){m_status=tr("面 %1 为 %2，只支持选择解析平面").arg(id).arg(it->surfaceType);emit changed();return;}
    auto &role=interference?m_interference:m_work;const bool remove=role.contains(id);
    m_work.remove(id);m_interference.remove(id);if(!remove)role.insert(id);
    m_pending.clear();invalidateGeometry();m_status=tr("选面已更新，请重新处理点云");emit changed();
}
void StepTemplateBuilderSessionV1::clearRoles(){if(m_busy)return;m_work.clear();m_interference.clear();m_pending.clear();invalidateGeometry();emit changed();}
void StepTemplateBuilderSessionV1::clearPending(){if(m_busy)return;m_pending.clear();emit changed();}
void StepTemplateBuilderSessionV1::selectRectangle(pcv::render::CloudProjectionSnapshotV1 view,QRectF rectangle){
    if(m_busy||!m_cloud.success)return;
    m_pending.clear();const auto cloud=m_cloud;const auto removed=m_removed;
    start(Kind::Selection,tr("正在投影完整点集…"),[cloud,removed,view,rectangle](const auto &cancelFlag){
        Result r;
        for(qsizetype i=0;i<cloud.points.size();++i){
            if((i&4095)==0&&cancelFlag->load())return r;
            if(removed.contains(int(i)))continue;
            bool inside=false;const auto position=view.project(cloud.points.at(i),&inside);
            if(inside&&rectangle.contains(position))r.pending.insert(int(i));
        }
        r.ok=true;return r;
    });
}
void StepTemplateBuilderSessionV1::trimTo(QSet<int> removed,int historyAction,QSet<int> delta){
    if(m_busy)return;const auto cloud=m_cloud;m_historyAction=historyAction;m_historyDelta=std::move(delta);
    start(Kind::Trim,tr("正在更新完整点集修剪与展示映射…"),[cloud,removed](const auto &cancelFlag){
        Result r;r.removed=removed;r.ok=buildDisplay(r,cloud,cancelFlag);return r;
    });
}
void StepTemplateBuilderSessionV1::erasePending(){if(m_busy||m_pending.isEmpty())return;auto removed=m_removed;removed.unite(m_pending);trimTo(std::move(removed),1,m_pending);}
void StepTemplateBuilderSessionV1::undo(){if(m_busy||m_history.isEmpty())return;auto removed=m_removed;removed.subtract(m_history.last());trimTo(std::move(removed),-1);}
void StepTemplateBuilderSessionV1::restore(){if(m_busy||m_removed.isEmpty())return;trimTo({},0);}
void StepTemplateBuilderSessionV1::process(){
    if(!canProcess())return;
    m_pending.clear();invalidateGeometry();const auto model=m_model;const auto cloud=m_cloud;const auto work=m_work;const auto interference=m_interference;const auto removed=m_removed;
    start(Kind::Process,tr("正在生成模板候选与修剪边界…"),[model,cloud,work,interference,removed](const auto &cancelFlag){
        Result r;pcv::workpiece::TemplateFeatureOptionsV1 options;
        options.budget=std::make_shared<pcv::ProcessingBudget>(512ull*1024*1024);
        options.isCancelled=[cancelFlag]{return cancelFlag->load();};
        r.package=pcv::workpiece::generateStepTemplatePackageFromSampledV1(model,cloud,work,interference,removed,options);
        r.ok=r.package.ok;r.error=r.package.error;return r;
    });
}
void StepTemplateBuilderSessionV1::confirm(){if(m_busy||!m_package.ok)return;m_confirmed=true;emit changed();}
void StepTemplateBuilderSessionV1::save(const QString &name,const QString &material,const QString &root){
    if(m_busy||!m_confirmed||!m_package.ok||name.trimmed().isEmpty())return;
    const QString source=QDir(QString::fromUtf8(PCV_STEP_TEMPLATE_SOURCE_ROOT)).canonicalPath();
    const QString destination=QDir(root).canonicalPath();
    if(source.isEmpty()||destination.isEmpty()||destination.compare(source,Qt::CaseInsensitive)==0||destination.startsWith(source+"/",Qt::CaseInsensitive)){
        m_status=tr("请选择存在的源码树外目录");emit changed();return;
    }
    auto package=m_package;package.source.insert("workpiece",QJsonObject{{"id",m_workpieceId},{"name",name.trimmed()},{"material",material},{"status","draft"}});

    start(Kind::Save,tr("正在事务保存新版本…"),[package,name,destination](const auto &cancelFlag){
        Result r;r.saved=pcv::workpiece::saveTemplatePackageV1(package,name.trimmed(),destination,[cancelFlag]{return cancelFlag->load();});
        r.ok=r.saved.success;r.error=r.saved.message;return r;
    });
}
}
