#include "scan_geometry_session_v2.h"
#include "scan_geometry_output_v1.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSysInfo>
#include <QFile>
#include <QVariant>
#include <cmath>
#include <algorithm>
#include <utility>
#include <exception>

namespace pcv::demo {
using namespace pcv::planefitting;
namespace { constexpr qsizetype kDisplayPointLimit = 1000000; }
ScanGeometrySessionV2::ScanGeometrySessionV2(QObject *parent) : QObject(parent),
    m_token(std::make_shared<std::atomic_bool>(false)),
    m_budget(std::make_shared<pcv::ProcessingBudget>(512u*1024u*1024u)) {
    m_pool.setMaxThreadCount(2);
    m_logDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("logs/scan_geometry_load_v1");
    const QString source=QDir::cleanPath(QString::fromUtf8(PCV_SCAN_GEOMETRY_SOURCE_ROOT));
    if(m_logDirectory.compare(source,Qt::CaseInsensitive)==0||m_logDirectory.startsWith(source+"/",Qt::CaseInsensitive))
        m_logDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("pcv_load_logs_v1");
    m_logPath=QDir(m_logDirectory).filePath(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128)+".jsonl");
    writeTiming({{"event","session"},{"qt",qVersion()},{"cpu_arch",QSysInfo::currentCpuArchitecture()},
        {"os",QSysInfo::prettyProductName()},{"build",QStringLiteral(__DATE__ " " __TIME__)},
#ifdef QT_NO_DEBUG
        {"configuration","Release"},{"performance_version","v3"}
#else
        {"configuration","Debug"},{"performance_version","v3"}
#endif
    });
}
ScanGeometrySessionV2::~ScanGeometrySessionV2() { shutdown(); }
void ScanGeometrySessionV2::recordOperation(const QString &operation) {
    writeTiming({{"event","operation"},{"operation",operation},{"active_frame",m_active},
        {"frame_id",activeFrame()?QString::number(activeFrame()->id):QString()},
        {"attempt",activeFrame()?QString::number(activeFrame()->loadAttempt):QString()},
        {"reference_revision",QString::number(m_referenceRevision)}});
}
void ScanGeometrySessionV2::cancelSelection() {recordOperation(QStringLiteral("cancelSelection"));m_seeds.clear();pump();}
void ScanGeometrySessionV2::writeTiming(QJsonObject event) {
    event.insert("utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    event.insert("input_revision",QString::number(m_revision));
    if(!event.contains("level"))event.insert("level",!event.value("error").toString().isEmpty()?"error"
        :!event.value("warning").toString().isEmpty()?"warning":"info");
    const auto line=QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
    m_recentLog.append(line);if(m_recentLog.size()>200)m_recentLog.removeFirst();
    if(!QDir().mkpath(m_logDirectory)){m_logError=tr("无法创建耗时日志目录");return;}
    QFile file(m_logPath);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Append)){m_logError=file.errorString();return;}
    const auto bytes=QJsonDocument(event).toJson(QJsonDocument::Compact)+'\n';
    if(file.write(bytes)!=bytes.size())m_logError=file.errorString();
}
bool ScanGeometrySessionV2::interactionBlocked() const {
    if(!m_pending.empty())return true;
    for(const auto &job:m_jobKinds)
        if(job.second!=Task::LoadDisplay&&job.second!=Task::PrepareIndex)return true;
    return m_token->load();
}
bool ScanGeometrySessionV2::activeFrameBusy() const {
    const auto *f=activeFrame();
    return interactionBlocked()||(f&&(f->loading||f->indexQueued||f->indexPreparing));
}
bool ScanGeometrySessionV2::canSelectActive() const {
    const auto *f=activeFrame();
    return !activeFrameBusy()&&(!m_matching||m_paused)&&f&&f->sourceVerification==SourceVerificationV3::Verified&&f->index&&!f->cloud.fullPoints.isEmpty()&&!f->skipped;
}
QString ScanGeometrySessionV2::timingSummary() const {
    const auto format=[](const QJsonValue &v){return v.isDouble()?QString::number(v.toDouble(),'f',1)+" ms":QStringLiteral("未观察");};
    QString text=tr("日志：%1").arg(m_logDirectory);
    if(const auto *f=activeFrame()) {
        text+=tr("\n当前帧 %1 · attempt %2\n首显 %3 · 可选面 %4\n读取 %5 · 转换 %6 · 索引 %7")
            .arg(m_active+1).arg(f->loadAttempt).arg(format(f->loadTiming.value("first_frame_ms")),format(f->loadTiming.value("selectable_ms")),
            format(f->loadTiming.value("input_ms")),format(f->loadTiming.value("transform_ms")),format(f->loadTiming.value("index_ms")));
        text+=tr("\n来源校验完成 %1").arg(format(f->loadTiming.value("verified_ms")));
        text+=tr("\n本次切帧至显示 %1 · 可见等待 %2").arg(format(f->loadTiming.value("view_wait_ms")),format(f->loadTiming.value("visible_wait_ms")));
    }
    text+=tr("\n当前轮：%1 · 总等待 %2").arg(m_roundTiming.value("status").toString(tr("未加载")),format(m_roundTiming.value("wall_ms")));
    if(!m_logError.isEmpty())text+=tr("\n日志写入失败：%1").arg(m_logError);
    return text;
}
void ScanGeometrySessionV2::observeCanvasTiming(quint64 frameId,quint64 attempt,const QJsonObject &timing) {
    for(auto &f:m_frames)if(f.id==frameId&&f.loadAttempt==attempt) {
        QJsonObject event=timing;event.insert("frame_id",QString::number(frameId));event.insert("attempt",QString::number(attempt));
        if(event.value("event").toString()=="view_frame_swapped") {
            f.loadTiming.insert("view_wait_ms",event.value("view_wait_ms"));
            f.loadTiming.insert("visible_wait_ms",event.value("visible_wait_ms"));
        }
        writeTiming(event);
        if(event.value("event").toString()=="view_frame_swapped")emit changed(false);
        return;
    }
}
void ScanGeometrySessionV2::observeFirstFrame(quint64 frameId,quint64 attempt) {
    for(auto &f:m_frames)if(f.id==frameId&&f.loadAttempt==attempt&&f.requestedAt.isValid()
        &&!f.loadTiming.value("first_frame_ms").isDouble()) {
        if(m_token->load()||f.loadTiming.value("status").toString()=="cancelled")return;
        f.loadTiming.insert("first_frame_ms",double(f.requestedAt.elapsed()));
        QJsonObject event=f.loadTiming;event.insert("event","first_frame_swapped");
        writeTiming(event);emit changed(false);return;
    }
}
void ScanGeometrySessionV2::finishRoundTiming() {
    if(!m_roundTimingOpen||!m_loadQueue.empty())return;
    for(const auto &job:m_jobKinds)if(job.second==Task::LoadDisplay||job.second==Task::PrepareIndex)return;
    for(const auto &pending:m_pending)if(pending.kind==Task::LoadDisplay||pending.kind==Task::PrepareIndex)return;
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6);++i)
        if(m_frames.at(i).indexQueued||m_frames.at(i).indexPreparing)return;
    m_roundTimingOpen=false;bool failed=false;QJsonArray frames;
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6);++i) {
        failed|=m_frames.at(i).problem;frames.append(m_frames.at(i).loadTiming);
    }
    m_roundTiming.insert("frames",frames);
    m_roundTiming.insert("status",m_token->load()?"cancelled":failed?"failed":"ready");
    m_roundTiming.insert("wall_ms",double(m_roundTimer.elapsed()));writeTiming(m_roundTiming);
}
void ScanGeometrySessionV2::enqueueLoad(int frame,bool front) {
    auto &f=m_frames[frame];
    if(f.loading)return;
    m_loadQueue.erase(std::remove(m_loadQueue.begin(),m_loadQueue.end(),frame),m_loadQueue.end());
    ++f.loadAttempt;f.requestedAt.start();f.sourceVerification=SourceVerificationV3::Unverified;
    f.loadTiming={{"frame_id",QString::number(f.id)},{"attempt",QString::number(f.loadAttempt)},
        {"first_frame_ms",QJsonValue::Null},{"verified_ms",QJsonValue::Null},{"selectable_ms",QJsonValue::Null},
        {"event","load_requested"},{"status","queued"}};
    f.loadTiming.insert("source",m_config.frames.size()>frame?m_config.frames.at(frame).path:QString());
    writeTiming(f.loadTiming);
    if(front)m_loadQueue.push_front(frame);else m_loadQueue.push_back(frame);
}
const SessionFrameV2 *ScanGeometrySessionV2::activeFrame() const {
    return m_active>=0 && m_active<m_frames.size()?&m_frames.at(m_active):nullptr;
}
bool ScanGeometrySessionV2::configure(const InputConfigV2 &config, QString *error) {
    recordOperation(QStringLiteral("configure"));
    auto fail=[&](const QString &s) {writeTiming({{"event","configuration_failed"},{"error",s}});if(error)*error=s;return false; };
    if(busy())return fail(tr("请先取消并等待当前任务结束"));
    if(config.frames.size()<2)return fail(tr("至少配置两帧输入"));
    if(!validateScanGeometryContractV1(config.contract,error))return false;
    if(config.outputRoot.trimmed().isEmpty())return fail(tr("请选择输出根目录"));
    if(!QFileInfo(config.outputRoot).isAbsolute())return fail(tr("输出根目录必须是源码树外的绝对路径"));
    const auto output=QDir::cleanPath(QFileInfo(config.outputRoot).absoluteFilePath());
    const auto source=QDir::cleanPath(QString::fromUtf8(PCV_SCAN_GEOMETRY_SOURCE_ROOT));
    if(output.compare(source,Qt::CaseInsensitive)==0||output.startsWith(source+"/",Qt::CaseInsensitive))
        return fail(tr("正式输出不得写入源码树，请选择外部目录"));
    if(!std::isfinite(config.tolerance)||config.tolerance<=0 || !std::isfinite(config.maximumGap)||config.maximumGap<0||config.maximumGap>50
        || !std::isfinite(config.searchDistance)||config.searchDistance<=0
        || !std::isfinite(config.searchAngle)||config.searchAngle<=0||config.searchAngle>90
        || config.maximumSeeds<1||config.maximumSeeds>64
        || !std::isfinite(config.highSideMinimumHeightMm)||config.highSideMinimumHeightMm<0
        || config.highSideMinimumPointCount<1)return fail(tr("选面或搜索参数无效"));
    for(const auto &input:config.frames) {
        if(input.depthFrame) {
            if(!QFileInfo(QDir(input.path).filePath(QStringLiteral("frame.json"))).isFile())
                return fail(tr("深度会话帧缺少 frame.json：%1").arg(input.path));
        } else if(!QFileInfo(input.path).isFile())return fail(tr("PLY 文件不存在：%1").arg(input.path));
        if(!QFileInfo(input.calibration.isEmpty()?config.calibration:input.calibration).isFile())return fail(tr("手眼 XML 文件不存在：%1").arg(input.path));
        for(const auto &pose:{input.start,input.end})
            for(double v:{pose.x,pose.y,pose.z,pose.rx,pose.ry,pose.rz})
                if(!std::isfinite(v))return fail(tr("位姿必须是有限数字"));
    }
    reset();m_config=config;
    if(!m_config.calibration.isEmpty())m_config.calibration=QFileInfo(m_config.calibration).absoluteFilePath();
    for(auto &input:m_config.frames) {
        input.path=QFileInfo(input.path).absoluteFilePath();
        input.calibration=QFileInfo(input.calibration.isEmpty()?m_config.calibration:input.calibration).absoluteFilePath();
    }
    for(const auto &input:config.frames) { Q_UNUSED(input);SessionFrameV2 f;f.id=m_nextFrameId++;m_frames.append(f); }
    m_active=0;m_status=tr("配置完成：%1 帧；点击加载当前轮").arg(m_frames.size());emit changed(true);return true;
}
void ScanGeometrySessionV2::invalidateResult() {
    ++m_resultRevision;m_result={};m_hasResult=false;invalidateGeometry();m_removed.clear();m_undo.clear();
    ++m_trimRevision;m_resultRegions={};
}
void ScanGeometrySessionV2::invalidateGeometry() {
    m_geometryConfirmed=false;m_wobj1={};m_savedGeometryPath.clear();
}
ResultRegionsRequestV1 ScanGeometrySessionV2::resultRegionRequest() const {
    ResultRegionsRequestV1 request;
    request.points=m_result.points;request.corrections=m_result.corrections;request.removed=m_removed;
    request.basis=m_resultRegions.basis;request.budget=m_budget;
    const auto token=m_token;request.isCancelled=[token]{return token->load();};
    int kept=0;
    for(int i=0;i<m_frames.size();++i)if(!m_frames.at(i).skipped) {
        const auto &f=m_frames.at(i);
        if(i==m_reference){request.reference=f.candidate;request.referenceKeptIndex=kept;}
        ResultInterferenceFrameV1 frame;
        if(!f.interferenceIndices.isEmpty()) {
            frame.mask=f.interferenceMask;frame.error=f.interferenceMaskError;
            if(frame.mask.triangleIndices.isEmpty()&&frame.error.isEmpty())frame.error=tr("已确认干涉区域缺失，请重新标注");
        }
        request.frames.append(std::move(frame));++kept;
    }
    return request;
}
void ScanGeometrySessionV2::acceptResultRegions(const Task &task) {
    if(!m_hasResult||task.resultRevision!=m_resultRevision||task.trimRevision!=m_trimRevision
        ||task.annotationRevision!=m_annotationRevision)return;
    m_resultRegions.resultRevision=m_resultRevision;m_resultRegions.trimRevision=m_trimRevision;
    m_resultRegions.annotationRevision=m_annotationRevision;
    if(!task.regions) {
        m_resultRegions.state=ResultRegionStateV1::Failed;
        m_resultRegions.error=task.error.isEmpty()?tr("区域生成未返回结果，可重建"):task.error;
    } else {
        const auto &regions=*task.regions;
        m_resultRegions.basis=regions.basis;
        if(regions.workpieceOk)m_resultRegions.workpiece=regions.workpiece;
        // Keep the previous valid red region on cancellation; clear/replace
        // actions remove obsolete annotations before dispatching this task.
        if(!regions.cancelled&&(regions.interferenceOk||!regions.interference.vertices.isEmpty()))m_resultRegions.interference=regions.interference;
        m_resultRegions.error=regions.error;
        m_resultRegions.state=regions.cancelled?ResultRegionStateV1::Cancelled:
            regions.workpieceOk&&regions.interferenceOk?ResultRegionStateV1::Ready:ResultRegionStateV1::Failed;
    }
    writeTiming({{"event","result_regions"},{"result_revision",QString::number(m_resultRevision)},
        {"trim_revision",QString::number(m_trimRevision)},{"annotation_revision",QString::number(m_annotationRevision)},
        {"status",m_resultRegions.state==ResultRegionStateV1::Ready?"success":m_resultRegions.state==ResultRegionStateV1::Cancelled?"cancelled":"failed"},
        {"error",m_resultRegions.error},{"performance",task.regions?task.regions->performance:task.performance}});
}
void ScanGeometrySessionV2::rebuildResultRegions() {
    recordOperation(QStringLiteral("rebuildResultRegions"));
    if(!m_hasResult||busy()||m_shutdown)return;
    m_resultRegions.workpiece={};m_resultRegions.state=ResultRegionStateV1::Building;m_resultRegions.error.clear();
    const auto request=resultRegionRequest();
    const auto retained=std::make_shared<const ResultRegionsRequestV1>(request);
    const auto resultRevision=m_resultRevision,trimRevision=m_trimRevision,annotationRevision=m_annotationRevision;
    m_status=tr("正在重建结果区域；点云及磁盘产物保持可用");
    submit(-1,Task::BuildResultRegions,[request,retained,resultRevision,trimRevision,annotationRevision] {
        Q_UNUSED(retained);
        Task task;task.kind=Task::BuildResultRegions;task.resultRevision=resultRevision;
        task.trimRevision=trimRevision;task.annotationRevision=annotationRevision;
        try {
            task.regions=std::make_shared<ResultRegionsResultV1>(buildResultRegionsV1(request));
            task.ok=task.regions->workpieceOk&&task.regions->interferenceOk;task.cancelled=task.regions->cancelled;
            task.error=task.regions->error;task.performance=task.regions->performance;
        }catch(const std::exception &e){task.error=QString::fromUtf8(e.what());}
        catch(...){task.error=QStringLiteral("区域生成异常，可重建");}
        return task;
    });
    emit changed(true);
}
void ScanGeometrySessionV2::reset() {
    recordOperation(QStringLiteral("reset"));
    if(busy())return;
    ++m_revision;++m_referenceRevision;m_token->store(true);m_token=std::make_shared<std::atomic_bool>(false);
    m_frames.clear();m_config={};m_loadQueue.clear();m_pending.clear();m_roundTimingOpen=false;m_active=-1;m_round=0;m_reference=-1;m_wobj1={};m_savedGeometryPath.clear();
    m_matching=false;m_paused=false;m_seeds.clear();m_progress=-1;m_committedOutputs.clear();invalidateResult();
    m_status=tr("请配置 PLY/SDK深度帧、手眼 XML 和逐帧 Start/End 位姿");emit changed(true);
}
void ScanGeometrySessionV2::cancel() {
    recordOperation(QStringLiteral("cancel"));
    for(int i:m_loadQueue)m_frames[i].loadTiming.insert("status","cancelled");
    m_token->store(true);++m_revision;m_loadQueue.clear();m_pending.clear();m_matching=false;m_paused=false;m_seeds.clear();
    if(m_resultRegions.state==ResultRegionStateV1::Building) {
        m_resultRegions.state=ResultRegionStateV1::Cancelled;m_resultRegions.error=tr("区域生成已取消，可重建；点云及修剪记录保留");
    }
    for(auto &f:m_frames){if(f.loading||f.indexQueued||f.indexPreparing){f.loading=false;f.indexQueued=f.indexPreparing=false;f.loadTiming.insert("status","cancelled");f.status=tr("已取消，可重试");if(f.sourceVerification!=SourceVerificationV3::Verified)f.sourceVerification=SourceVerificationV3::Unverified;}f.highSideChecking=false;}
    m_status=tr("已请求取消；等待任务安全结束，保留原始帧和已审核候选");emit changed(false);
    if(!busy()){finishRoundTiming();m_token=std::make_shared<std::atomic_bool>(false);emit changed(false);}
}
void ScanGeometrySessionV2::shutdown() {
    if(m_shutdown)return;
    m_shutdown=true;
    recordOperation(QStringLiteral("shutdown"));
    for(auto &f:m_frames){
        if(f.loading||f.indexQueued||f.indexPreparing)f.loadTiming.insert("status","cancelled");
        f.indexQueued=false;f.indexPreparing=false;
    }
    m_token->store(true);++m_revision;m_matching=false;m_loadQueue.clear();m_pending.clear();
    for(auto *job:std::as_const(m_jobs)) {
        disconnect(job,nullptr,this,nullptr);job->waitForFinished();
        // A transaction may already have committed before cancellation.
        try {const auto task=job->result();if(task.kind==Task::Stitch && task.stitched.success) {
            m_status=tr("正式产物已落盘：%1").arg(task.stitched.outputPly);
            if(!task.stitched.outputPly.isEmpty())m_committedOutputs.append(task.stitched.outputPly);}
            if(task.kind==Task::SaveGeometry&&task.geometrySaved&&!task.savedGeometryPath.isEmpty()) {
                m_committedOutputs.append(task.savedGeometryPath);m_status=tr("关闭前几何事务已提交：%1").arg(task.savedGeometryPath);
            }}
        catch(...) {m_status=tr("关闭时后台任务异常，未发布会话结果");}
        delete job;
    }
    m_jobs.clear();m_jobKinds.clear();m_pool.waitForDone();finishRoundTiming();
    writeTiming({{"event","session_closed"},{"status",m_status},{"committed_outputs",QJsonArray::fromStringList(m_committedOutputs)}});
    for(const auto &f:m_frames)if(f.loadAttempt){auto event=f.loadTiming;event.insert("event","close_attempt");writeTiming(event);}
}
void ScanGeometrySessionV2::submit(int frame, Task::Kind kind, std::function<Task()> work) {
    QElapsedTimer queuedAt;queuedAt.start();
    if(m_jobs.size()>=2) {m_pending.push_back({frame,kind,std::move(work),queuedAt});return;}
    const auto token=m_token;const auto revision=m_revision,referenceRevision=m_referenceRevision;
    const quint64 attempt=frame>=0?m_frames.at(frame).loadAttempt:0;
    const quint64 frameId=frame>=0?m_frames.at(frame).id:0;
    writeTiming({{"event","task_started"},{"kind",int(kind)},{"frame_id",QString::number(frameId)},
        {"attempt",QString::number(attempt)},{"reference_revision",QString::number(referenceRevision)}});
    auto *watcher=new QFutureWatcher<Task>(this);m_jobs.append(watcher);m_jobKinds.insert(watcher,{frame,kind});
    connect(watcher,&QFutureWatcher<Task>::finished,this,[this,watcher,frame,frameId,attempt,kind,revision,referenceRevision,token] {
        Task task;
        try { task=watcher->result(); }
        catch(const std::exception &e) { task.error=QString::fromUtf8(e.what()); }
        catch(...) { task.error=tr("后台任务异常，未发布结果"); }
        const bool success=task.ok||task.stitched.success||task.geometrySaved;
        QString warning=task.planeMaskError;
        if(task.stitched.success&&task.stitched.report.value("quality_policy").toString()=="warn_only")
            warning=tr("几何质量仅警告，未执行严格质量验收");
        const bool stale=revision!=m_revision||token!=m_token
            ||(frame>=0&&(frame>=m_frames.size()||m_frames.at(frame).id!=frameId||m_frames.at(frame).loadAttempt!=attempt))
            ||((kind!=Task::LoadDisplay&&kind!=Task::PrepareIndex)&&referenceRevision!=m_referenceRevision);
        writeTiming({{"event","task_finished"},{"kind",int(kind)},{"frame_id",QString::number(frameId)},
            {"attempt",QString::number(attempt)},{"reference_revision",QString::number(referenceRevision)},
            {"task_input_revision",QString::number(revision)},
            {"worker_ms",double(task.workerMs)},{"warning",warning},{"output_ply",task.stitched.outputPly},{"saved_geometry",task.savedGeometryPath},
            {"status",token->load()||task.cancelled?"cancelled":stale?"stale":success?"success":"failed"},
            {"performance",task.performance},{"message",task.stitched.message},
            {"digest_ms",task.digestMs<0?QJsonValue(QJsonValue::Null):QJsonValue(double(task.digestMs))},
            {"error",success?QString():task.error.isEmpty()?task.stitched.message:task.error}});
        m_jobs.removeOne(watcher);m_jobKinds.remove(watcher);watcher->deleteLater();
        if(frame>=0&&(frame>=m_frames.size()||m_frames.at(frame).id!=frameId)){emit changed(false);return;}
        if(frame>=0&&(kind==Task::LoadDisplay||kind==Task::PrepareIndex)) {
            QJsonObject event{{"event",kind==Task::LoadDisplay?"load_worker":"index_worker"},
                {"frame_id",QString::number(frameId)},{"attempt",QString::number(attempt)},
                {"status",revision!=m_revision||token!=m_token||attempt!=m_frames.at(frame).loadAttempt?"stale":task.cancelled?"cancelled":task.ok?"success":"failed"},
                {"error",task.error},{"worker_ms",task.startedAt.isValid()?QJsonValue(double(task.workerMs)):QJsonValue(QJsonValue::Null)},
                {"gui_wait_ms",task.completedAt.isValid()?QJsonValue(double(task.completedAt.elapsed())):QJsonValue(QJsonValue::Null)}};
            if(task.startedAt.isValid())event.insert("pool_wait_ms",double(task.startedAt.msecsSinceReference()-watcher->property("submitted_ms").toLongLong()));
            if(kind==Task::LoadDisplay) {
                const auto add=[&event](const char *key,qint64 value){event.insert(key,value<0?QJsonValue(QJsonValue::Null):QJsonValue(double(value)));};
                add("xml_ms",task.xmlMs);add("sdk_materialize_ms",task.sdkMaterializeMs);add("digest_ms",task.digestMs);
                add("height_range_ms",task.heightRangeMs);add("input_ms",task.plyReadMs);
                add("fingerprint_ms",task.fingerprintMs);add("cache_read_ms",task.cacheReadMs);add("cache_write_ms",task.cacheWriteMs);
                add("parse_ms",task.plyParseMs);add("sampling_ms",task.displaySamplingMs);add("display_budget_ms",task.displayBudgetMs);add("transform_ms",task.transformMs);
                add("header_ms",task.headerMs);add("boundary_scan_ms",task.boundaryScanMs);
            } else {
                event.insert("performance",task.performance);
                event.insert("digest_ms",task.digestMs<0?QJsonValue(QJsonValue::Null):QJsonValue(double(task.digestMs)));
                event.insert("index_ms",task.preparationMs);event.insert("grid_ms",task.gridMs);
                event.insert("normals_ms",task.normalsMs);event.insert("index_budget_ms",task.indexBudgetMs);
            }
            event.insert("task_input_revision",QString::number(revision));
            writeTiming(event);
        }
        if(frame>=0&&attempt!=m_frames.at(frame).loadAttempt){pump();emit changed(false);return;}
        finish(frame,kind,revision,referenceRevision,token,std::move(task));
    });
    watcher->setProperty("submitted_ms",queuedAt.msecsSinceReference());
    watcher->setFuture(QtConcurrent::run(&m_pool,[work=std::move(work)]() mutable {
        QElapsedTimer started;started.start();Task task;
        try {task=work();}catch(const std::exception &e){task.error=QString::fromUtf8(e.what());}
        catch(...){task.error=QStringLiteral("后台任务异常");}
        task.startedAt=started;task.workerMs=started.elapsed();task.completedAt.start();return task;
    }));
}
void ScanGeometrySessionV2::releaseOutsideRound() {
    for(int i=0;i<m_frames.size();++i)if(i/6!=m_round) {
        auto &f=m_frames[i];f.index.reset();f.cloud={};f.display.clear();f.displayToFull.clear();f.displayLease.reset();
    }
}
void ScanGeometrySessionV2::loadRound(int round) {
    recordOperation(QStringLiteral("loadRound"));
    if(busy()||m_matching||m_frames.isEmpty())return;
    invalidateGeometry();
    m_round=qBound(0,round,int((m_frames.size()-1)/6));m_active=m_round*6;m_seeds.clear();
    releaseOutsideRound();m_loadQueue.clear();
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6);++i)
        if(!m_frames.at(i).skipped && (m_config.frames.at(i).depthFrame
            ? !m_frames.at(i).depthReady : m_frames.at(i).cloud.fullPoints.isEmpty()))enqueueLoad(i);
    m_roundTimer.start();m_roundTimingOpen=true;m_roundTiming={{"event","round_finished"},{"round",m_round+1},{"status","loading"},{"wall_ms",QJsonValue::Null}};
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6);++i)
        if(!m_frames.at(i).skipped&&!m_frames.at(i).cloud.fullPoints.isEmpty()&&!m_frames.at(i).index)m_frames[i].indexQueued=true;
    m_status=tr("读取当前轮源帧：第 %1 轮").arg(m_round+1);pump();emit changed(true);
}
void ScanGeometrySessionV2::dispatchLoad(int frame) {
    m_frames[frame].loading=true;m_frames[frame].status=tr("读取与坐标转换中");
    m_frames[frame].loadTiming.insert("application_queue_ms",double(m_frames.at(frame).requestedAt.elapsed()));
    const auto input=m_config.frames.at(frame);const auto xml=input.calibration;const auto token=m_token;const auto budget=m_budget;
    const auto raw=m_frames.at(frame).rawDepthFrame;
    const bool materialize=input.depthFrame&&bool(raw);
    submit(frame,Task::LoadDisplay,[input,xml,token,budget,raw,materialize] {
        auto task=input.depthFrame
            ? (materialize?pcv::ui::materializeSelectedDepthFrameV2(raw,input.path,xml,input.start,input.end,token,2,budget,kDisplayPointLimit)
                          :pcv::ui::loadSelectedDepthFrameV2(input.path,xml,input.start,input.end,token,2,budget,kDisplayPointLimit))
            :pcv::ui::loadSelectedPlaneFrameV2(input.path,xml,input.start,input.end,token,2,budget,input.layout,kDisplayPointLimit);
        if(!task.ok||token->load())return task;
        if(input.depthFrame&&!materialize)return task;
        if(!pcv::ui::prepareHeightRangeV1(&task,token)){task.ok=false;return task;}
        task.cancelled=token->load();task.ok=!task.cancelled;return task;
    });
}
void ScanGeometrySessionV2::dispatchIndex(int frame) {
    auto &f=m_frames[frame];
    writeTiming({{"event","index_dispatch"},{"frame_id",QString::number(f.id)},{"attempt",QString::number(f.loadAttempt)},
        {"application_queue_ms",f.indexQueuedAt.isValid()?QJsonValue(double(f.indexQueuedAt.elapsed())):QJsonValue(QJsonValue::Null)}});
    const bool verify=!m_config.frames.at(frame).depthFrame&&f.sourceVerification!=SourceVerificationV3::Verified;
    f.indexQueued=false;f.indexPreparing=true;
    if(verify)f.sourceVerification=SourceVerificationV3::Checking;
    f.status=verify?tr("点云已显示，来源校验中；暂不可选面"):tr("点云已显示，正在准备完整选面索引");
    const auto points=f.cloud.fullPoints;
    const auto retained=std::make_shared<const QVector<pointcloud::Point3D>>(points);
    const auto token=m_token;const auto budget=m_budget;const auto source=f.cloud;
    const auto revision=m_revision,attempt=f.loadAttempt,frameId=f.id;
    const auto requestedAt=f.requestedAt;
    submit(frame,Task::PrepareIndex,[this,frame,points,retained,token,budget,source,verify,revision,attempt,frameId,requestedAt]() -> Task {
        Q_UNUSED(retained);
        QByteArray digest;double digestMs=-1,verifiedMs=-1;
        if(verify) {
            QElapsedTimer timer;timer.start();digest=pcv::ui::loadedCloudDigestV2(source,token);digestMs=timer.elapsed();
            verifiedMs=requestedAt.isValid()?double(requestedAt.elapsed()):-1;
            if(token->load()||digest.isEmpty()) {
                Task failed;failed.kind=Task::PrepareIndex;failed.cancelled=token->load();failed.digestMs=qint64(digestMs);
                failed.error=failed.cancelled?QStringLiteral("cancelled"):QStringLiteral("完整来源摘要校验失败");return failed;
            }
            // Receiver context discards this callback on destruction; close joins the worker.
            QMetaObject::invokeMethod(this,[this,frame,frameId,revision,attempt,token,digest,digestMs,verifiedMs] {
                if(frame<0||frame>=m_frames.size()||m_frames.at(frame).id!=frameId)return;
                acceptSourceVerification(frame,revision,attempt,token,digest,digestMs,verifiedMs);
            },Qt::QueuedConnection);
        }
        auto task=pcv::ui::prepareSelectedPlaneIndexV2(points,token,2,budget,65536,true);
        task.sourceDigest=digest;task.sourceVerified=verify;task.verifiedMs=verifiedMs;task.digestMs=qint64(digestMs);
        return task;
    });
}
void ScanGeometrySessionV2::acceptSourceVerification(int frame,quint64 revision,quint64 attempt,
    const pcv::ui::CancellationTokenV2 &token,const QByteArray &digest,double digestMs,double verifiedMs) {
    if(revision!=m_revision||token!=m_token||token->load()||frame<0||frame>=m_frames.size())return;
    auto &f=m_frames[frame];
    if(f.loadAttempt!=attempt||digest.isEmpty()||f.sourceVerification==SourceVerificationV3::Verified)return;
    const bool sourceChanged=!f.digest.isEmpty()&&f.digest!=digest;
    if(sourceChanged) {
        f.hasCandidate=false;f.candidate={};f.planeIndices.clear();f.interferenceIndices.clear();f.pendingInterference.clear();
        f.workpieceMask={};f.interferenceMask={};f.pendingInterferenceMask={};
        f.workpieceMaskError.clear();f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();
        f.reviewed=f.confirmed=f.automatic=false;
        f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();f.highSideChecking=false;
        invalidateResult();if(frame==m_reference){invalidateReferenceDependents();m_reference=-1;}
    }
    f.digest=digest;f.sourceVerification=SourceVerificationV3::Verified;
    f.loadTiming.insert("digest_ms",digestMs);f.loadTiming.insert("verified_ms",verifiedMs);
    f.status=sourceChanged?tr("来源变化，旧候选已失效；准备索引中"):tr("来源校验通过，准备完整索引中");
    auto event=f.loadTiming;event.insert("event","source_verified");event.insert("status","success");
    event.insert("source_changed",sourceChanged);writeTiming(event);emit changed(true);
}
void ScanGeometrySessionV2::processActive() {
    recordOperation(QStringLiteral("processActive"));
    if(activeFrameBusy()||m_active<0||m_active>=m_frames.size()||m_matching)return;
    auto &frame=m_frames[m_active];
    if(!m_config.frames.at(m_active).depthFrame||!frame.depthReady||!frame.rawDepthFrame)return;
    if(!frame.cloud.fullPoints.isEmpty()&&frame.index)return;
    invalidateGeometry();enqueueLoad(m_active,true);
    m_status=tr("正在物化当前深度帧并建立完整选面索引");pump();emit changed(true);
}
void ScanGeometrySessionV2::pump() {
    if(m_token->load()){finishRoundTiming();return;}
    while(!m_pending.empty()&&m_jobs.size()<2) {
        auto pending=std::move(m_pending.front());m_pending.pop_front();
        writeTiming({{"event","foreground_queue"},{"kind",int(pending.kind)},
            {"frame_id",pending.frame>=0?QString::number(m_frames.at(pending.frame).id):QString()},
            {"attempt",pending.frame>=0?QString::number(m_frames.at(pending.frame).loadAttempt):QString()},
            {"application_queue_ms",double(pending.queuedAt.elapsed())}});
        submit(pending.frame,pending.kind,std::move(pending.work));
    }
    if(interactionBlocked()||!m_seeds.isEmpty())return;
    if(m_active>=0&&m_active<m_frames.size()&&m_jobs.size()<2&&m_frames.at(m_active).indexQueued)dispatchIndex(m_active);
    while(!m_loadQueue.empty()&&m_jobs.size()<2) {const int i=m_loadQueue.front();m_loadQueue.pop_front();dispatchLoad(i);}
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6)&&m_jobs.size()<2;++i)
        if(m_frames.at(i).indexQueued)dispatchIndex(i);
    finishRoundTiming();
    if(busy()||!m_loadQueue.empty()||!m_matching||m_paused)return;
    while(m_nextMatch<m_frames.size() && (m_nextMatch==m_reference||(m_frames.at(m_nextMatch).hasCandidate&&!m_frames.at(m_nextMatch).problem&&m_frames.at(m_nextMatch).sourceVerification==SourceVerificationV3::Verified)||m_frames.at(m_nextMatch).skipped))++m_nextMatch;
    if(m_nextMatch>=m_frames.size()) {m_matching=false;m_status=tr("匹配结束；逐帧预览候选后确认全部候选");return;}
    const int i=m_nextMatch;
    if(m_frames.at(i).problem){pauseAt(i,m_frames.at(i).status);return;}
    if(m_frames.at(i).cloud.fullPoints.isEmpty()) {
        m_round=i/6;m_active=i;releaseOutsideRound();enqueueLoad(i,true);pump();return;
    }
    if(!m_frames.at(i).index||m_frames.at(i).sourceVerification!=SourceVerificationV3::Verified){m_frames[i].indexQueued=true;pump();return;}
    dispatchMatch(i);
}
void ScanGeometrySessionV2::invalidateReferenceDependents() {
    ++m_referenceRevision;
    for(int i=0;i<m_frames.size();++i)if(i!=m_reference) {
        auto &f=m_frames[i];f.confirmed=false;f.problem=false;
        if(f.automatic){f.hasCandidate=false;f.reviewed=false;f.candidate={};f.planeIndices.clear();f.automatic=false;
            f.workpieceMask={};f.workpieceMaskError.clear();
            f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();f.highSideChecking=false;
            f.status=tr("参考面已变更，待重新匹配");}
    }
    m_matching=false;m_paused=false;invalidateResult();
}
void ScanGeometrySessionV2::pauseAt(int frame,const QString &reason) {
    m_paused=m_matching;m_active=frame;m_round=frame/6;m_frames[frame].problem=true;m_frames[frame].status=reason;
    m_status=tr("第 %1 帧需要处理：%2").arg(frame+1).arg(reason);
}
void ScanGeometrySessionV2::finish(int frame,Task::Kind kind,quint64 revision,quint64 referenceRevision,
    const pcv::ui::CancellationTokenV2 &token,Task task) {
    const bool dependsOnReference=kind==Task::Match||kind==Task::Fit||kind==Task::HighSideSearch||kind==Task::Stitch||kind==Task::SaveGeometry||kind==Task::BuildResultRegions;
    if(revision!=m_revision||(dependsOnReference&&referenceRevision!=m_referenceRevision)||token!=m_token||token->load()) {
        // Cancellation can arrive after the stitch transaction committed, while
        // only the display mesh is still running. Configuration/reset wait for
        // this task, so this exact token/result generation can retain its cloud.
        if(kind==Task::Stitch&&task.stitched.success&&token==m_token&&token->load()&&!m_shutdown
            &&referenceRevision==m_referenceRevision&&task.resultRevision==m_resultRevision+1) {
            m_result=std::move(task.stitched);m_hasResult=true;m_active=-1;++m_resultRevision;m_trimRevision=0;
            m_resultRegions={};acceptResultRegions(task);
            if(!m_result.outputPly.isEmpty())m_committedOutputs.append(m_result.outputPly);
            m_resultRegions.state=ResultRegionStateV1::Cancelled;
            m_resultRegions.error=tr("拼接已提交，区域生成已取消，可重建");
            m_status=m_resultRegions.error;
            if(!busy())m_token=std::make_shared<std::atomic_bool>(false);
            emit changed(true);return;
        }
        if(kind==Task::Stitch&&task.stitched.success){m_status=tr("取消前事务已提交：%1；未发布会话结果").arg(task.stitched.outputPly);
            if(!task.stitched.outputPly.isEmpty())m_committedOutputs.append(task.stitched.outputPly);}
        if(kind==Task::SaveGeometry&&task.geometrySaved&&!task.savedGeometryPath.isEmpty()) {
            m_committedOutputs.append(task.savedGeometryPath);m_status=tr("取消前几何事务已提交：%1；未发布会话结果").arg(task.savedGeometryPath);
        }
        finishRoundTiming();
        if(!busy()&&m_token->load()) {m_token=std::make_shared<std::atomic_bool>(false);m_progress=-1;}
        emit changed(false);return;
    }
    if(kind==Task::PrepareIndex&&frame>=0) {
        if(task.sourceVerified)acceptSourceVerification(frame,revision,m_frames.at(frame).loadAttempt,token,task.sourceDigest,double(task.digestMs),task.verifiedMs);
        m_frames[frame].indexPreparing=false;
        if(!task.ok&&m_frames.at(frame).sourceVerification!=SourceVerificationV3::Verified)
            m_frames[frame].sourceVerification=task.cancelled?SourceVerificationV3::Unverified:SourceVerificationV3::Failed;
    }
    if(kind==Task::BuildResultRegions) {
        acceptResultRegions(task);
        m_status=m_resultRegions.state==ResultRegionStateV1::Ready?tr("结果区域已更新；灰色点云 / 蓝色工件模板 / 红色干涉面")
            :tr("点云结果保留，区域显示未完成：%1").arg(m_resultRegions.error);
        pump();emit changed(true);return;
    }
    if(frame>=0&&frame<m_frames.size()&&kind==Task::LoadDisplay)m_frames[frame].loading=false;
    if(kind==Task::Stitch) {
        m_progress=-1;
        if(task.stitched.success) {
            m_resultHeightRange=task.heightRange;
            m_result=std::move(task.stitched);m_hasResult=true;m_active=-1;++m_resultRevision;
            m_trimRevision=0;m_resultRegions={};
            acceptResultRegions(task);
            m_committedOutputs.append(m_result.outputPly);
            int fallback=0;for(const auto &p:m_result.pairs)if(p.poseFallback)++fallback;
            m_status=tr("事务提交完成 · 回退 %1 对 · 质量策略 warn_only（非严格质量通过）\n%2").arg(fallback).arg(m_result.outputPly);
            m_status+=tr("\n来源映射：%1\n报告：%2").arg(m_result.mappingFile,m_result.reportFile);
            for(int i=0;i<m_result.pairs.size();++i){const auto &pair=m_result.pairs.at(i);
                m_status+=tr("\n相邻对 %1：%2 · %3").arg(i+1).arg(pair.fineApplied?tr("精配准已应用"):tr("位姿回退/仅位姿")).arg(pair.reason);}
            if(!m_resultRegions.error.isEmpty())m_status+=tr("\n拼接成功，区域显示失败：%1").arg(m_resultRegions.error);
        } else m_status=tr("拼接未完成：%1 %2").arg(task.stitched.errorCode,task.stitched.message);
        emit changed(true);return;
    }
    if(kind==Task::SaveGeometry) {
        m_progress=-1;
        if(task.geometrySaved) {
            m_savedGeometryPath=task.savedGeometryPath;m_geometryConfirmed=true;
            m_status=tr("几何结果已保存：%1").arg(task.savedGeometryPath);m_committedOutputs.append(task.savedGeometryPath);
            if(m_config.contract.enabled()) {
                const auto file=QDir(pointCloudContractDirectoryV1(m_config.contract)).filePath("point_cloud_result.json");
                m_status+=tr("\n候选几何契约：%1（未发布模板；缺实际扫描来源时为 partial）").arg(file);
                m_committedOutputs.append(file);
            }
        } else m_status=tr("几何保存未完成：%1").arg(task.error);
        emit changed(true);return;
    }
    auto &f=m_frames[frame];
    if(kind==Task::HighSideSearch) {
        f.highSideChecking=false;f.highSideError.clear();f.highSideCandidates.clear();
        if(task.ok&&!task.cancelled) {
            for(const auto &candidate:task.highSideCandidates)
                if(!f.dismissedHighSideSeeds.contains(candidate.seedIndex))f.highSideCandidates.append(candidate);
            if(!f.highSideCandidates.isEmpty())
                m_status=tr("发现 %1 个未标注的高侧平面候选；可标注或忽略").arg(f.highSideCandidates.size());
        } else if(!task.cancelled) f.highSideError=task.error;
        pump();emit changed(true);return;
    }
    if(!task.ok||task.cancelled) {
        if(kind==Task::Fit)m_seeds.clear();
        if(kind==Task::LoadDisplay||kind==Task::PrepareIndex) {f.problem=true;f.loadTiming.insert("status",task.cancelled?"cancelled":"failed");f.status=task.cancelled?tr("已取消，可重试"):task.error;m_status=tr("第 %1 帧：%2").arg(frame+1).arg(f.status);if(m_matching)pauseAt(frame,f.status);}
        else {if(m_matching)pauseAt(frame,task.error);else {f.status=task.error;m_status=task.error;}}
    } else if(kind==Task::PrepareIndex) {
        f.index=std::move(task.index);f.problem=false;
        if(f.sourceVerification!=SourceVerificationV3::Verified){f.problem=true;f.status=tr("来源尚未校验，请重试");pump();emit changed(true);return;}
        f.status=tr("完整索引就绪，可以选面");
        f.loadTiming.insert("index_complete_ms",f.requestedAt.isValid()?QJsonValue(double(f.requestedAt.elapsed())):QJsonValue(QJsonValue::Null));
        f.loadTiming.insert("index_ms",task.preparationMs);
        f.loadTiming.insert("selectable_ms",f.requestedAt.isValid()?QJsonValue(double(f.requestedAt.elapsed())):QJsonValue(QJsonValue::Null));
        auto event=f.loadTiming;event.insert("event","index_ready");event.insert("grid_ms",task.gridMs);
        event.insert("normals_ms",task.normalsMs);event.insert("index_budget_ms",task.indexBudgetMs);event.insert("optimization_bytes",double(task.optimizationBytes));writeTiming(event);
        if(f.hasCandidate)dispatchHighSideSearch(frame);
    } else if(kind==Task::LoadDisplay) {
        f.loadTiming.insert("status","display_ready");
        f.heightRange=task.heightRange;f.hasHeightRange=task.hasHeightRange;
        f.loadTiming.insert("input_ms",task.plyReadMs<0?QJsonValue(QJsonValue::Null):QJsonValue(double(task.plyReadMs)));
        f.loadTiming.insert("transform_ms",task.transformMs<0?QJsonValue(QJsonValue::Null):QJsonValue(double(task.transformMs)));
        QJsonObject event=f.loadTiming;event.insert("event","display_ready");event.insert("status","success");
        event.insert("file_bytes",m_config.frames.at(frame).depthFrame?QJsonValue(QJsonValue::Null)
            :QJsonValue(double(QFileInfo(m_config.frames.at(frame).path).size())));
        event.insert("full_points",double(task.loaded.fullPoints.size()));event.insert("display_points",double(task.displayPoints.size()));
        event.insert("used_cache",task.usedCache);event.insert("format",task.sourceFormat);
        const auto add=[&event](const char *key,qint64 value){event.insert(key,value<0?QJsonValue(QJsonValue::Null):QJsonValue(double(value)));};
        add("xml_ms",task.xmlMs);add("sdk_materialize_ms",task.sdkMaterializeMs);add("digest_ms",task.digestMs);
        add("height_range_ms",task.heightRangeMs);add("fingerprint_ms",task.fingerprintMs);add("cache_read_ms",task.cacheReadMs);
        add("cache_write_ms",task.cacheWriteMs);add("parse_ms",task.plyParseMs);add("header_ms",task.headerMs);
        add("boundary_scan_ms",task.boundaryScanMs);add("sampling_ms",task.displaySamplingMs);add("display_budget_ms",task.displayBudgetMs);
        event.insert("transform_merge_ms",task.transformMergeMs);event.insert("workers",task.transformWorkers);writeTiming(event);
        const bool depthSourceOnly=m_config.frames.at(frame).depthFrame
            && task.rawDepthFrame && task.loaded.fullPoints.isEmpty();
        if(depthSourceOnly) {
            if(!f.digest.isEmpty()&&f.digest!=task.sourceDigest) {
                f.hasCandidate=false;f.candidate={};f.planeIndices.clear();f.interferenceIndices.clear();f.pendingInterference.clear();
                f.workpieceMask={};f.interferenceMask={};f.pendingInterferenceMask={};
                f.workpieceMaskError.clear();f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();f.reviewed=f.confirmed=false;
                invalidateResult();if(frame==m_reference){invalidateReferenceDependents();m_reference=-1;}
                m_status=tr("第 %1 帧深度源发生变化，旧候选已失效").arg(frame+1);
            }
            f.digest=task.sourceDigest;f.rawDepthFrame=std::move(task.rawDepthFrame);f.depthReady=true;
            f.problem=false;f.status=tr("深度源已就绪，等待按需转换");m_status=tr("第 %1 帧深度源已就绪；点击开始当前帧处理以生成点云").arg(frame+1);
            pump();emit changed(true);return;
        }
        m_status=tr("第 %1 帧已完成读取与坐标转换，后台准备索引").arg(frame+1);
        if(m_config.frames.at(frame).depthFrame&&!f.digest.isEmpty()&&f.digest!=task.sourceDigest) {
            f.hasCandidate=false;f.candidate={};f.planeIndices.clear();f.interferenceIndices.clear();f.pendingInterference.clear();
            f.workpieceMask={};f.interferenceMask={};f.pendingInterferenceMask={};
            f.workpieceMaskError.clear();f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();f.reviewed=f.confirmed=false;
            f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();f.highSideChecking=false;
            invalidateResult();if(frame==m_reference){invalidateReferenceDependents();m_reference=-1;}
            m_status=tr("第 %1 帧来源/几何发生变化，旧候选已失效").arg(frame+1);
        }
        if(m_config.frames.at(frame).depthFrame){f.digest=task.sourceDigest;f.sourceVerification=SourceVerificationV3::Verified;}
        else f.sourceVerification=SourceVerificationV3::Checking;
        f.cloud=std::move(task.loaded);f.display=std::move(task.displayPoints);
        f.displayToFull=std::move(task.displayToFullIndices);f.displayLease=std::move(task.displayLease);f.index.reset();f.indexQueued=true;f.indexQueuedAt.start();
        f.highSideCandidates.clear();f.highSideError.clear();f.highSideChecking=false;
        f.problem=false;f.status=f.sourceVerification==SourceVerificationV3::Verified?tr("已加载，准备索引"):tr("点云已显示，来源校验中；旧标注暂不显示");
    } else if(kind==Task::Match || kind==Task::Fit) {
        if(kind==Task::Fit&&m_seedInterference) {
            f.pendingInterference=std::move(task.planeIndices);f.pendingInterferenceMask=std::move(task.planeMask);
            f.pendingInterferenceMaskError=task.planeMaskError;invalidateGeometry();m_status=tr("干涉面预览；确认前请检查与工件面冲突");
            if(conflicts(f.pendingInterference,f.planeIndices))m_status=tr("干涉候选与工件面重叠，请重选或清除干涉标注后继续审核");
        } else if(conflicts(task.planeIndices,f.interferenceIndices)) {
            pauseAt(frame,tr("工件候选与干涉标注重叠，请清除干涉标注后重选"));
        } else {
            if(frame==m_reference)invalidateReferenceDependents();
            f.candidate=std::move(task.selected);f.planeIndices=std::move(task.planeIndices);
            f.workpieceMask=std::move(task.planeMask);f.workpieceMaskError=task.planeMaskError;
            f.candidate.selectionParameters.insert("frame_id",QString::number(f.id));
            f.hasCandidate=true;f.automatic=kind==Task::Match;f.reviewed=false;f.confirmed=false;f.skipped=false;f.problem=false;
            f.status=f.automatic?tr("自动候选 · 待预览"):tr("人工候选 · 待确认");invalidateResult();
            m_status=f.status;
            f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();
            dispatchHighSideSearch(frame);
        }
        m_seeds.clear();
        if(kind==Task::Match)++m_nextMatch;
    }
    pump();
    if(kind==Task::LoadDisplay&&!busy()&&m_loadQueue.empty()&&!m_matching)m_status+=tr("；当前轮任务结束");
    emit changed(true);
}
bool ScanGeometrySessionV2::conflicts(const QVector<int> &a,const QVector<int> &b) const {
    // Full-plane task indices are sorted in their immutable source order.
    qsizetype i=0,j=0;
    while(i<a.size()&&j<b.size()){if(a.at(i)==b.at(j))return true;if(a.at(i)<b.at(j))++i;else ++j;}
    return false;
}
void ScanGeometrySessionV2::activate(int frame) {
    recordOperation(QStringLiteral("activate"));
    if(interactionBlocked()||(m_matching&&!m_paused)||frame<0||frame>=m_frames.size()||frame/6!=m_round)return;
    m_active=frame;m_seeds.clear();
    if(m_frames.at(frame).cloud.fullPoints.isEmpty()&&!m_frames.at(frame).skipped
       && frame<m_config.frames.size()
       && (!m_config.frames.at(frame).depthFrame || !m_frames.at(frame).depthReady)) {
        if(!m_config.frames.at(frame).depthFrame)invalidateGeometry();
        if(!m_frames.at(frame).loading)enqueueLoad(frame,true);pump();
    }
    pump();
    emit changed(true);
}
void ScanGeometrySessionV2::showResult() {recordOperation(QStringLiteral("showResult"));if(!busy()&&m_hasResult){m_active=-1;m_seeds.clear();emit changed(true);}}
void ScanGeometrySessionV2::retry() {
    recordOperation(QStringLiteral("retry"));
    if(activeFrameBusy()||m_active<0||(m_matching&&!m_paused))return;
    auto &f=m_frames[m_active];
    if(!f.cloud.fullPoints.isEmpty()&&(!f.index||f.sourceVerification!=SourceVerificationV3::Verified)){f.problem=false;f.index.reset();f.indexQueued=true;f.indexQueuedAt.start();pump();emit changed(false);return;}
    if(f.skipped)invalidateResult();f.problem=false;f.skipped=false;
    f.cloud={};f.index.reset();f.display.clear();f.displayToFull.clear();f.displayLease.reset();
    m_seeds.clear();
    invalidateGeometry();
    enqueueLoad(m_active,true);m_status=tr("重新读取当前帧，核对原候选来源");pump();
    emit changed(false);
}
void ScanGeometrySessionV2::pick(int fullIndex,bool interference) {
    recordOperation(QStringLiteral("pick"));
    if(!canSelectActive())return;
    auto &f=m_frames[m_active];
    if(fullIndex<0||fullIndex>=f.cloud.fullPoints.size()||!f.index)return;
    if(!interference) {
        if(m_active==m_reference){invalidateReferenceDependents();m_reference=-1;}
        f.candidate={};f.planeIndices.clear();f.workpieceMask={};f.hasCandidate=f.confirmed=f.reviewed=f.automatic=false;
        f.workpieceMaskError.clear();
        f.problem=false;f.status=tr("正在重新选择工件面");f.highSideCandidates.clear();
        f.dismissedHighSideSeeds.clear();f.highSideError.clear();f.highSideChecking=false;invalidateResult();
    } else {
        f.pendingInterference.clear();f.pendingInterferenceMask={};f.pendingInterferenceMaskError.clear();
    }
    if(m_seedInterference!=interference){m_seeds.clear();m_seedInterference=interference;}
    if(m_seeds.contains(fullIndex)){m_status=tr("请选不同控制点");emit changed(false);return;}
    m_seeds.append(fullIndex);
    if(m_config.threePoint&&m_seeds.size()<3){m_status=tr("三点选面：已选 %1/3，切换帧或 Esc 取消").arg(m_seeds.size());emit changed(true);return;}
    const auto source=f.cloud;const auto retained=std::make_shared<const pointcloud::RobotBaseFrame>(source);
    const auto index=f.index;const auto seeds=m_seeds;const auto config=m_config;const auto token=m_token;const auto budget=m_budget;
    m_status=tr("正在完整数据上选面并生成快照");
    submit(m_active,Task::Fit,[source,retained,index,seeds,config,token,budget] {
        Q_UNUSED(retained);return pcv::ui::fitFullPlaneV2(source,seeds,config.tolerance,config.maximumGap,!config.threePoint,index,token,2,budget);
    });emit changed(true);
}
void ScanGeometrySessionV2::setThreePoint(bool enabled) {
    recordOperation(QStringLiteral("setThreePoint"));
    if(activeFrameBusy())return;m_config.threePoint=enabled;m_seeds.clear();pump();emit changed(false);
}
bool ScanGeometrySessionV2::confirmCandidate() {
    recordOperation(QStringLiteral("confirmCandidate"));
    if(activeFrameBusy()||!activeFrame()||activeFrame()->sourceVerification!=SourceVerificationV3::Verified||!activeFrame()->hasCandidate)return false;
    auto &f=m_frames[m_active];
    if(m_reference<0) {
        for(int i=0;i<m_active;++i)if(!m_frames.at(i).skipped){m_status=tr("请先确认首个未跳过帧作为参考面");emit changed(false);return false;}
        m_reference=m_active;invalidateReferenceDependents();
    } else if(m_reference==m_active)invalidateReferenceDependents();

    f.reviewed=true;f.confirmed=true;f.problem=false;f.status=m_active==m_reference?tr("已确认参考面"):tr("已确认");
    if(m_active<m_config.frames.size()&&m_config.frames.at(m_active).depthFrame) {
        f.cloud={};f.index.reset();f.display={};f.displayToFull={};f.displayLease.reset();
    }
    m_status=f.status;pump();emit changed(true);return true;
}
bool ScanGeometrySessionV2::confirmInterference() {
    recordOperation(QStringLiteral("confirmInterference"));
    if(activeFrameBusy()||!activeFrame()||activeFrame()->sourceVerification!=SourceVerificationV3::Verified||activeFrame()->pendingInterference.isEmpty())return false;
    auto &f=m_frames[m_active];
    if(conflicts(f.pendingInterference,f.planeIndices)) {m_status=tr("干涉面与工件面重叠；请重选干涉面，或先重置工件面");emit changed(false);return false;}
    f.interferenceIndices=std::move(f.pendingInterference);f.pendingInterference.clear();
    f.interferenceMask=std::move(f.pendingInterferenceMask);f.pendingInterferenceMask={};
    f.interferenceMaskError=std::move(f.pendingInterferenceMaskError);f.pendingInterferenceMaskError.clear();
    ++m_annotationRevision;
    if(m_hasResult)m_resultRegions.interference={};
    f.highSideCandidates.erase(std::remove_if(f.highSideCandidates.begin(),f.highSideCandidates.end(),[&](const auto &candidate) {
        return std::binary_search(f.interferenceIndices.cbegin(),f.interferenceIndices.cend(),candidate.seedIndex);
    }),f.highSideCandidates.end());
    invalidateGeometry();
    m_status=tr("人工干涉标注已确认，不参与拼接；请保存几何结果以持久化");
    if(m_hasResult)rebuildResultRegions();emit changed(true);return true;
}
bool ScanGeometrySessionV2::selectHighSideCandidate(int seedIndex) {
    recordOperation(QStringLiteral("selectHighSideCandidate"));
    if(activeFrameBusy()||!activeFrame()||activeFrame()->sourceVerification!=SourceVerificationV3::Verified)return false;
    const auto &candidates=activeFrame()->highSideCandidates;
    if(std::none_of(candidates.cbegin(),candidates.cend(),[seedIndex](const auto &candidate){return candidate.seedIndex==seedIndex;}))return false;
    const auto source=activeFrame()->cloud;const auto retained=std::make_shared<const pointcloud::RobotBaseFrame>(source);
    const auto index=activeFrame()->index;const auto config=m_config;const auto token=m_token;const auto budget=m_budget;
    m_seeds.clear();m_seedInterference=true;m_status=tr("正在预览高侧候选平面");
    submit(m_active,Task::Fit,[source,retained,index,seedIndex,config,token,budget] {
        Q_UNUSED(retained);
        const QVector<int> seeds{seedIndex};
        return pcv::ui::fitFullPlaneV2(source,seeds,config.tolerance,config.maximumGap,true,index,token,2,budget);
    });
    emit changed(true);return true;
}
bool ScanGeometrySessionV2::ignoreHighSideCandidate(int seedIndex) {
    recordOperation(QStringLiteral("ignoreHighSideCandidate"));
    if(activeFrameBusy()||!activeFrame()||activeFrame()->sourceVerification!=SourceVerificationV3::Verified)return false;
    auto &f=m_frames[m_active];
    const auto it=std::find_if(f.highSideCandidates.begin(),f.highSideCandidates.end(),[seedIndex](const auto &candidate){return candidate.seedIndex==seedIndex;});
    if(it==f.highSideCandidates.end())return false;
    f.dismissedHighSideSeeds.insert(seedIndex);f.highSideCandidates.erase(it);
    m_status=tr("已忽略所选高侧提醒；不会影响拼接输入");emit changed(true);return true;
}
void ScanGeometrySessionV2::clearInterference() {
    recordOperation(QStringLiteral("clearInterference"));
    if(activeFrameBusy()||!activeFrame())return;auto &f=m_frames[m_active];f.interferenceIndices.clear();f.pendingInterference.clear();
    f.interferenceMask={};f.pendingInterferenceMask={};f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();invalidateGeometry();
    ++m_annotationRevision;
    if(m_hasResult){m_resultRegions.interference={};rebuildResultRegions();}
    else if(f.hasCandidate)dispatchHighSideSearch(m_active);emit changed(true);
}
void ScanGeometrySessionV2::clearPlane() {
    recordOperation(QStringLiteral("clearPlane"));
    if(activeFrameBusy()||!activeFrame())return;
    if(m_active==m_reference){invalidateReferenceDependents();m_reference=-1;}
    auto &f=m_frames[m_active];f.candidate={};f.planeIndices.clear();f.workpieceMask={};f.workpieceMaskError.clear();f.hasCandidate=f.confirmed=f.reviewed=f.automatic=false;
    f.status=tr("等待重新选面");f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();
    m_seeds.clear();invalidateResult();pump();emit changed(true);
}
void ScanGeometrySessionV2::matchRemaining() {
    recordOperation(QStringLiteral("matchRemaining"));
    if(busy()||!m_seeds.isEmpty()||m_reference<0||!m_frames.at(m_reference).confirmed||m_frames.at(m_reference).sourceVerification!=SourceVerificationV3::Verified)return;
    m_matching=true;m_paused=false;m_nextMatch=0;m_status=tr("以固定参考面匹配其余帧");pump();emit changed(false);
}
void ScanGeometrySessionV2::dispatchMatch(int frame) {
    const auto source=m_frames.at(frame).cloud;const auto retained=std::make_shared<const pointcloud::RobotBaseFrame>(source);
    const auto index=m_frames.at(frame).index;const auto ref=m_frames.at(m_reference).candidate;
    PlaneModel plane;plane.a=ref.normal.x();plane.b=ref.normal.y();plane.c=ref.normal.z();plane.d=-QVector3D::dotProduct(ref.normal,ref.center);
    const auto config=m_config;const auto token=m_token;const auto budget=m_budget;const auto referenceId=m_frames.at(m_reference).id;
    const auto referenceSource=ref.cloud.sourceFile;
    m_status=tr("自动匹配第 %1/%2 帧").arg(frame+1).arg(m_frames.size());
    submit(frame,Task::Match,[source,retained,index,plane,config,token,budget,referenceId,referenceSource] {
        Q_UNUSED(retained);
        ReferencePlaneMatchOptionsV2 options;options.maximumDistanceMm=config.searchDistance;
        options.maximumNormalAngleDegrees=config.searchAngle;options.maximumSeeds=config.maximumSeeds;
        options.selection.surfaceToleranceMm=config.tolerance;options.selection.maximumGapMm=config.maximumGap;
        options.selection.parallelEnabled=true;options.selection.maxParallelTasks=2;options.selection.processingBudget=budget;
        options.selection.isCancelled=[token]{return token->load();};
        QElapsedTimer searchTimer;searchTimer.start();
        const auto match=matchReferencePlaneV2(source.fullPoints,index,plane,options);
        const double searchMs=searchTimer.nsecsElapsed()/1e6;
        QJsonArray seedMs;for(double ms:match.seedSelectionMs)seedMs.append(ms);
        const QJsonObject searchTiming{{"match_search_ms",searchMs},{"attempted_seeds",match.attemptedSeeds},
            {"eligible_buckets",match.eligibleBuckets},{"seed_selection_ms",seedMs},{"reused_selection",false}};
        Task task;task.kind=Task::Match;task.matchStatus=match.status;task.error=match.error;task.performance=searchTiming;
        task.cancelled=match.status==ReferencePlaneMatchStatusV2::Cancelled;
        if(match.status!=ReferencePlaneMatchStatusV2::Unique)return task;
        const int seed=match.candidates.at(0).seedIndex;
        task=pcv::ui::snapshotFromSingleClickV2(source,{seed},config.tolerance,match.candidates.at(0).selection,token,budget);
        for(auto it=searchTiming.begin();it!=searchTiming.end();++it)task.performance.insert(it.key(),it.value());
        task.performance.insert("reused_selection",true);
        task.kind=Task::Match;task.matchStatus=match.status;
        auto &p=task.selected.selectionParameters;p.insert("reference_frame_id",QString::number(referenceId));
        p.insert("seed_origin","reference_plane_match_v2");p.insert("automatic_seed_full_index",seed);
        p.insert("search_distance_mm",config.searchDistance);p.insert("search_angle_deg",config.searchAngle);
        p.insert("maximum_seeds",config.maximumSeeds);p.insert("attempted_seeds",match.attemptedSeeds);
        p.insert("reference_source_file",referenceSource);
        QJsonArray attempted;for(int i:match.attemptedSeedIndices)attempted.append(i);
        p.insert("attempted_seed_full_indices",attempted);
        return task;
    });
}
void ScanGeometrySessionV2::dispatchHighSideSearch(int frame) {
    if(frame<0||frame>=m_frames.size())return;
    auto &f=m_frames[frame];
    if(f.sourceVerification!=SourceVerificationV3::Verified||!f.hasCandidate||f.cloud.fullPoints.isEmpty()||!f.index)return;
    const auto source=f.cloud;const auto retained=std::make_shared<const pointcloud::RobotBaseFrame>(source);
    const auto index=f.index;const auto selected=f.candidate;const auto config=m_config;
    const auto token=m_token;const auto budget=m_budget;QVector<int> excluded=f.planeIndices;
    excluded.append(f.interferenceIndices);
    f.highSideChecking=true;f.highSideError.clear();f.highSideCandidates.clear();
    submit(frame,Task::HighSideSearch,[source,retained,index,selected,config,token,budget,excluded] {
        Q_UNUSED(retained);
        HighSidePlaneSearchOptionsV1 options;options.minimumHeightMm=config.highSideMinimumHeightMm;
        options.minimumPointCount=config.highSideMinimumPointCount;options.maximumSeeds=config.maximumSeeds;
        options.selection.surfaceToleranceMm=config.tolerance;options.selection.maximumGapMm=config.maximumGap;
        options.selection.parallelEnabled=true;options.selection.maxParallelTasks=2;options.selection.processingBudget=budget;
        options.selection.isCancelled=[token]{return token->load();};
        const auto found=findHighSidePlaneCandidatesV1(source.fullPoints,index,selected.center,excluded,options);
        Task task;task.kind=Task::HighSideSearch;task.highSideSearchStatus=found.status;
        task.highSideCandidates=found.candidates;task.error=found.error;
        task.cancelled=found.status==HighSidePlaneSearchStatusV1::Cancelled;
        task.ok=found.status==HighSidePlaneSearchStatusV1::Success;
        return task;
    });
}
void ScanGeometrySessionV2::resumeMatching() {
    recordOperation(QStringLiteral("resumeMatching"));
    if(busy()||!m_seeds.isEmpty()||!m_paused||!activeFrame()
       ||(!activeFrame()->skipped&&!activeFrame()->reviewed))return;
    // A paused match can be resumed after the operator reviews or replaces
    // the failed frame. Keep an unresolved frame blocked until that review.
    if(activeFrame()->problem && !activeFrame()->hasCandidate)return;
    m_frames[m_active].problem=false;
    m_paused=false;m_matching=true;m_nextMatch=0;pump();emit changed(false);
}
void ScanGeometrySessionV2::skipActive() {
    recordOperation(QStringLiteral("skipActive"));
    if(activeFrameBusy()||!activeFrame())return;
    m_seeds.clear();
    if(m_active==m_reference){invalidateReferenceDependents();m_reference=-1;}
    auto &f=m_frames[m_active];f.skipped=true;f.problem=false;f.hasCandidate=f.confirmed=f.reviewed=false;
    f.candidate={};f.planeIndices.clear();f.workpieceMask={};f.workpieceMaskError.clear();
    f.pendingInterference.clear();f.pendingInterferenceMask={};f.pendingInterferenceMaskError.clear();
    f.highSideCandidates.clear();f.highSideError.clear();
    f.indexQueued=false;
    m_loadQueue.erase(std::remove(m_loadQueue.begin(),m_loadQueue.end(),m_active),m_loadQueue.end());
    f.status=tr("用户明确跳过");invalidateResult();m_status=f.status;pump();emit changed(true);
}
QString ScanGeometrySessionV2::summary() const {
    int keep=0,skip=0,problem=0,unreviewed=0;
    for(const auto &f:m_frames) {if(f.skipped)++skip;else if(f.hasCandidate)++keep;if(f.problem)++problem;if(!f.skipped&&(!f.hasCandidate||!f.reviewed))++unreviewed;}
    return tr("保留 %1 · 跳过 %2 · 异常 %3 · 未审核 %4").arg(keep).arg(skip).arg(problem).arg(unreviewed);
}
bool ScanGeometrySessionV2::canReviewAll() const {
    if(busy()||m_matching||!m_seeds.isEmpty()||m_reference<0)return false;int valid=0;
    for(const auto &f:m_frames)if(!f.skipped){if(f.sourceVerification!=SourceVerificationV3::Verified||f.problem||!f.hasCandidate||!f.reviewed
        ||conflicts(f.planeIndices,f.interferenceIndices)||conflicts(f.planeIndices,f.pendingInterference))return false;++valid;}
    return valid>=2;
}
void ScanGeometrySessionV2::confirmAll() {
    recordOperation(QStringLiteral("confirmAll"));
    if(!canReviewAll())return;
    for(int i=0;i<m_frames.size();++i)if(!m_frames[i].skipped){
        auto &f=m_frames[i];f.confirmed=true;f.status=tr("已批量确认");
        if(i<m_config.frames.size()&&m_config.frames.at(i).depthFrame){f.cloud={};f.index.reset();f.display={};f.displayToFull={};f.displayLease.reset();}
    }
    m_status=tr("全部候选已确认；可手动开始拼接");emit changed(true);
}
bool ScanGeometrySessionV2::canStitch() const {
    if(!canReviewAll())return false;
    for(const auto &f:m_frames)if(!f.skipped&&!f.confirmed)return false;return true;
}
void ScanGeometrySessionV2::stitch() {
    recordOperation(QStringLiteral("stitch"));
    if(!canStitch())return;invalidateResult();
    pcv::stitching::SelectedPlaneStitchRequest request;
    for(const auto &f:m_frames)if(!f.skipped)request.frames.append(f.candidate);
    request.calibrationPath=m_config.calibration;request.options.fineEnabled=!m_config.poseOnly;
    request.options.maxParallelTasks=4;
    const auto intermediateRoot=m_config.contract.enabled()
        ? QDir(m_config.contract.runtimeRoot).filePath(QStringLiteral("system/temp/%1/%2").arg(m_config.contract.jobId,m_config.contract.runId)) : m_config.outputRoot;
    request.outputDirectory=QDir(intermediateRoot).filePath(QStringLiteral("selected_plane_%1_%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"),QUuid::createUuid().toString(QUuid::Id128)));
    const auto token=m_token;request.isCancelled=[token]{return token->load();};const auto revision=m_revision;
    auto regionsRequest=resultRegionRequest();
    const auto nextResultRevision=m_resultRevision+1,annotationRevision=m_annotationRevision;
    request.progress=[this,token,revision](float value,const QString &stage) {
        QMetaObject::invokeMethod(this,[this,token,revision,value,stage] {
            if(token!=m_token||token->load()||revision!=m_revision)return;
            m_progress=qBound(0,qRound(value*100),100);m_status=stage;emit changed(false);
        },Qt::QueuedConnection);
    };
    m_status=tr("模块 30：所选平面拼接与事务输出");m_progress=-1;
    submit(-1,Task::Stitch,[request,regionsRequest,nextResultRevision,annotationRevision]() mutable {
        QElapsedTimer transaction;transaction.start();
        Task task;task.kind=Task::Stitch;task.stitched=pcv::stitching::stitchSelectedPlanes(request);
        task.performance.insert("stitch_transaction_ms",transaction.nsecsElapsed()/1e6);
        if(task.stitched.success) {
            task.resultRevision=nextResultRevision;task.trimRevision=0;task.annotationRevision=annotationRevision;
            try {
                regionsRequest.points=task.stitched.points;regionsRequest.corrections=task.stitched.corrections;
                const auto retained=std::make_shared<const ResultRegionsRequestV1>(regionsRequest);
                Q_UNUSED(retained);
                task.regions=std::make_shared<ResultRegionsResultV1>(buildResultRegionsV1(regionsRequest));
                for(auto it=task.regions->performance.begin();it!=task.regions->performance.end();++it)task.performance.insert(it.key(),it.value());
                task.performance.insert("regions_error",task.regions->error);
            }catch(const std::exception &e){task.error=QString::fromUtf8(e.what());}
            catch(...){task.error=QStringLiteral("拼接已提交，区域生成异常，可重建");}
        }
        return task;
    });emit changed(false);
}
void ScanGeometrySessionV2::erase(const QVector<int> &indices,quint64 resultRevision) {
    recordOperation(QStringLiteral("erase"));
    if(busy()||!m_hasResult||m_active!=-1||resultRevision!=m_resultRevision)return;auto next=m_removed;
    for(int i:indices)if(i>=0&&i<m_result.points.size())next.insert(i);
    if(next==m_removed)return;m_undo.append(m_removed);m_removed=std::move(next);invalidateGeometry();
    ++m_trimRevision;rebuildResultRegions();
}
void ScanGeometrySessionV2::undo() {
    recordOperation(QStringLiteral("undo"));if(!busy()&&m_active==-1&&!m_undo.isEmpty()){m_removed=m_undo.takeLast();invalidateGeometry();++m_trimRevision;rebuildResultRegions();emit changed(true);}}
void ScanGeometrySessionV2::restore() {
    recordOperation(QStringLiteral("restore"));if(!busy()&&m_active==-1&&!m_removed.isEmpty()){m_undo.append(m_removed);m_removed.clear();invalidateGeometry();++m_trimRevision;rebuildResultRegions();emit changed(true);}}
bool ScanGeometrySessionV2::canConfirmGeometry() const {
    if(busy()||!m_hasResult||m_active!=-1||m_reference<0||m_reference>=m_frames.size()
       ||!m_frames.at(m_reference).confirmed||m_frames.at(m_reference).candidate.cloud.fullPoints.size()<3)return false;
    for(const auto &f:m_frames)if(!f.pendingInterference.isEmpty()||f.problem)return false;
    QVector3D a,b;bool first=false,second=false;
    for(int i=0;i<m_result.points.size();++i)if(!m_removed.contains(i)) {
        const auto &p=m_result.points.at(i);const QVector3D v(p.x,p.y,p.z);
        if(!first){a=v;first=true;}else if(!second){if((v-a).lengthSquared()>1e-12f){b=v;second=true;}}
        else if(QVector3D::crossProduct(b-a,v-a).lengthSquared()>1e-12f*(b-a).lengthSquared())return true;
    }
    return false;
}
bool ScanGeometrySessionV2::generateWobj1() {
    recordOperation(QStringLiteral("generateWobj1"));
    if(!canConfirmGeometry())return false;
    const auto &reference=m_frames.at(m_reference).candidate;
    QVector<int> indices;indices.reserve(reference.cloud.fullPoints.size());
    for(int i=0;i<reference.cloud.fullPoints.size();++i)indices.append(i);
    const auto bounds=pcv::planefitting::calculateBoundsCenter(reference.cloud.fullPoints,indices);
    if(!bounds.ok){m_status=bounds.error;emit changed(false);return false;}
    // Use the full confirmed reference snapshot. Project its bounds center
    // onto the confirmed plane; display sampling never enters this path.
    const auto normal=reference.normal.normalized();
    const auto origin=bounds.center-normal*QVector3D::dotProduct(normal,bounds.center-reference.center);
    m_wobj1=pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes(origin,normal);
    m_geometryConfirmed=false;m_savedGeometryPath.clear();
    m_status=m_wobj1.valid?tr("WObj1 已生成：robot_base 轴投影，等待保存确认"):m_wobj1.error;
    emit changed(true);return m_wobj1.valid;
}
void ScanGeometrySessionV2::saveGeometry() {
    recordOperation(QStringLiteral("saveGeometry"));
    if(!canConfirmGeometry())return;
    if(!m_wobj1.valid&&!generateWobj1())return;
    GeometryOutputRequestV1 request;request.result=m_result;request.removed=m_removed;request.wobj1=m_wobj1;
    request.contract=m_config.contract;
    if(request.contract.enabled() && QFileInfo::exists(QDir(pointCloudContractDirectoryV1(request.contract)).filePath("point_cloud_result.json"))) {
        m_status=tr("当前 run_id 已有正式成果，请配置新的 run_id 后重新处理；已有结果保持不变");emit changed(false);return;
    }
    request.referenceFrame=m_reference;request.resultRevision=m_resultRevision;
    request.outputRoot=QFileInfo(m_result.outputPly).absolutePath();
    for(int i=0;i<m_frames.size();++i) {
        GeometryFrameOutputV1 frame;frame.id=m_frames.at(i).id;frame.sourceFile=m_config.frames.at(i).path;
        frame.scanJson=m_config.frames.at(i).scanJson;frame.interferenceIndices=m_frames.at(i).interferenceIndices;
        request.frames.append(frame);
        if(request.contract.enabled()) {
            GeometryContractFrameV1 source;source.input=m_config.frames.at(i);source.sourceDigest=m_frames.at(i).digest;
            source.selected=m_frames.at(i).candidate;source.skipped=m_frames.at(i).skipped;
            source.interferenceMask=m_frames.at(i).interferenceMask;
            if(!m_frames.at(i).interferenceIndices.isEmpty() && source.interferenceMask.triangleIndices.isEmpty()) {
                m_status=tr("人工干涉掩码缺失，无法导出契约结果；请重新标注");emit changed(false);return;
            }
            request.contractFrames.append(source);
        }
    }
    const auto token=m_token;request.isCancelled=[token]{return token->load();};
    // Keep an extra shared immutable snapshot alive across all worker reads.
    const auto retained=std::make_shared<const GeometryOutputRequestV1>(request);
    m_status=tr("正在事务保存修剪结果、来源和人工干涉标注");
    submit(-1,Task::SaveGeometry,[request,retained] {
        Q_UNUSED(retained);const auto saved=saveSelectedPlaneGeometryV1(request);
        Task task;task.kind=Task::SaveGeometry;task.geometrySaved=saved.success;task.ok=saved.success;
        task.cancelled=saved.cancelled;task.error=saved.error;task.savedGeometryPath=saved.sidecar;
        if(!saved.success&&!saved.cancelled&&request.contract.enabled()) {
            const auto path=QDir(pointCloudContractDirectoryV1(request.contract)).filePath("diagnostics/"+QUuid::createUuid().toString(QUuid::Id128));
            pcv::io::ArtifactTransaction evidence(path,false);
            const QJsonObject diagnostic{{"kind","scan_geometry_contract_failure_v1"},{"job_id",request.contract.jobId},
                {"run_id",request.contract.runId},{"step_id",request.contract.stepId},{"status","failed"},
                {"result_revision",double(request.resultRevision)},{"error",saved.error},
                {"generated_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
            if(!evidence.stageJson("failure_v1.json",QJsonDocument(diagnostic).toJson())||!evidence.commit())
                task.error+=QStringLiteral("; failure evidence: ")+evidence.error();
        }
        return task;
    });emit changed(false);
}
void ScanGeometrySessionV2::confirmGeometry() {saveGeometry();}
} // namespace pcv::demo
