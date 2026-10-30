#include "scan_geometry_session_v2.h"
#include "scan_geometry_output_v1.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QUuid>
#include <QJsonArray>
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
}
ScanGeometrySessionV2::~ScanGeometrySessionV2() { shutdown(); }
const SessionFrameV2 *ScanGeometrySessionV2::activeFrame() const {
    return m_active>=0 && m_active<m_frames.size()?&m_frames.at(m_active):nullptr;
}
bool ScanGeometrySessionV2::configure(const InputConfigV2 &config, QString *error) {
    auto fail=[&](const QString &s) { if(error)*error=s;return false; };
    if(busy())return fail(tr("请先取消并等待当前任务结束"));
    if(config.frames.size()<2)return fail(tr("至少配置两帧输入"));
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
}
void ScanGeometrySessionV2::invalidateGeometry() {
    m_geometryConfirmed=false;m_wobj1={};m_savedGeometryPath.clear();
}
void ScanGeometrySessionV2::reset() {
    if(busy())return;
    ++m_revision;++m_referenceRevision;m_token->store(true);m_token=std::make_shared<std::atomic_bool>(false);
    m_frames.clear();m_config={};m_loadQueue.clear();m_active=-1;m_round=0;m_reference=-1;m_wobj1={};m_savedGeometryPath.clear();
    m_matching=false;m_paused=false;m_seeds.clear();m_progress=-1;m_committedOutputs.clear();invalidateResult();
    m_status=tr("请配置 PLY/SDK深度帧、手眼 XML 和逐帧 Start/End 位姿");emit changed(true);
}
void ScanGeometrySessionV2::cancel() {
    m_token->store(true);++m_revision;m_loadQueue.clear();m_matching=false;m_paused=false;m_seeds.clear();
    for(auto &f:m_frames){if(f.loading){f.loading=false;f.status=tr("已取消，可重试");}f.highSideChecking=false;}
    m_status=tr("已请求取消；等待任务安全结束，保留原始帧和已审核候选");emit changed(false);
    if(!busy())m_token=std::make_shared<std::atomic_bool>(false);
}
void ScanGeometrySessionV2::shutdown() {
    m_token->store(true);++m_revision;m_matching=false;m_loadQueue.clear();
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
    m_jobs.clear();m_pool.waitForDone();
}
void ScanGeometrySessionV2::submit(int frame, Task::Kind kind, std::function<Task()> work) {
    const auto token=m_token;const auto revision=m_revision,referenceRevision=m_referenceRevision;
    const quint64 frameId=frame>=0?m_frames.at(frame).id:0;
    auto *watcher=new QFutureWatcher<Task>(this);m_jobs.append(watcher);
    connect(watcher,&QFutureWatcher<Task>::finished,this,[this,watcher,frame,frameId,kind,revision,referenceRevision,token] {
        Task task;
        try { task=watcher->result(); }
        catch(const std::exception &e) { task.error=QString::fromUtf8(e.what()); }
        catch(...) { task.error=tr("后台任务异常，未发布结果"); }
        m_jobs.removeOne(watcher);watcher->deleteLater();
        if(frame>=0&&(frame>=m_frames.size()||m_frames.at(frame).id!=frameId)){emit changed(false);return;}
        finish(frame,kind,revision,referenceRevision,token,std::move(task));
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,std::move(work)));
}
void ScanGeometrySessionV2::releaseOutsideRound() {
    for(int i=0;i<m_frames.size();++i)if(i/6!=m_round) {
        auto &f=m_frames[i];f.index.reset();f.cloud={};f.display.clear();f.displayToFull.clear();f.displayLease.reset();
    }
}
void ScanGeometrySessionV2::loadRound(int round) {
    if(busy()||m_matching||m_frames.isEmpty())return;
    invalidateGeometry();
    m_round=qBound(0,round,int((m_frames.size()-1)/6));m_active=m_round*6;m_seeds.clear();
    releaseOutsideRound();m_loadQueue.clear();
    for(int i=m_round*6;i<qMin(int(m_frames.size()),m_round*6+6);++i)
        if(!m_frames.at(i).skipped && (m_config.frames.at(i).depthFrame
            ? !m_frames.at(i).depthReady : m_frames.at(i).cloud.fullPoints.isEmpty()))m_loadQueue.push_back(i);
    m_status=tr("读取当前轮源帧：第 %1 轮").arg(m_round+1);pump();emit changed(true);
}
void ScanGeometrySessionV2::dispatchLoad(int frame) {
    m_frames[frame].loading=true;m_frames[frame].status=tr("读取、转换与索引中");
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
        if(!input.depthFrame)task.sourceDigest=pcv::ui::loadedCloudDigestV2(task.loaded,token);
        const auto prepared=pcv::ui::prepareSelectedPlaneIndexV2(task.loaded.fullPoints,token,2,budget);
        if(!prepared.ok){task.ok=false;task.error=prepared.error;task.cancelled=prepared.cancelled;return task;}
        task.index=prepared.index;task.cancelled=token->load();task.ok=!task.cancelled;return task;
    });
}
void ScanGeometrySessionV2::processActive() {
    if(busy()||m_active<0||m_active>=m_frames.size()||m_matching)return;
    auto &frame=m_frames[m_active];
    if(!m_config.frames.at(m_active).depthFrame||!frame.depthReady||!frame.rawDepthFrame)return;
    if(!frame.cloud.fullPoints.isEmpty()&&frame.index)return;
    invalidateGeometry();m_loadQueue.clear();m_loadQueue.push_front(m_active);
    m_status=tr("正在物化当前深度帧并建立完整选面索引");pump();emit changed(true);
}
void ScanGeometrySessionV2::pump() {
    if(m_token->load())return;
    while(!m_loadQueue.empty()&&m_jobs.size()<2) {const int i=m_loadQueue.front();m_loadQueue.pop_front();dispatchLoad(i);}
    if(busy()||!m_loadQueue.empty()||!m_matching||m_paused)return;
    while(m_nextMatch<m_frames.size() && (m_nextMatch==m_reference||(m_frames.at(m_nextMatch).hasCandidate&&!m_frames.at(m_nextMatch).problem)||m_frames.at(m_nextMatch).skipped))++m_nextMatch;
    if(m_nextMatch>=m_frames.size()) {m_matching=false;m_status=tr("匹配结束；逐帧预览候选后确认全部候选");return;}
    const int i=m_nextMatch;
    if(m_frames.at(i).problem){pauseAt(i,m_frames.at(i).status);return;}
    if(m_frames.at(i).cloud.fullPoints.isEmpty()) {
        m_round=i/6;m_active=i;releaseOutsideRound();m_loadQueue.push_back(i);pump();return;
    }
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
    const bool dependsOnReference=kind==Task::Match||kind==Task::Fit||kind==Task::HighSideSearch||kind==Task::Stitch||kind==Task::SaveGeometry;
    if(revision!=m_revision||(dependsOnReference&&referenceRevision!=m_referenceRevision)||token!=m_token||token->load()) {
        if(kind==Task::Stitch&&task.stitched.success){m_status=tr("取消前事务已提交：%1；未发布会话结果").arg(task.stitched.outputPly);
            if(!task.stitched.outputPly.isEmpty())m_committedOutputs.append(task.stitched.outputPly);}
        if(kind==Task::SaveGeometry&&task.geometrySaved&&!task.savedGeometryPath.isEmpty()) {
            m_committedOutputs.append(task.savedGeometryPath);m_status=tr("取消前几何事务已提交：%1；未发布会话结果").arg(task.savedGeometryPath);
        }
        if(!busy()&&m_token->load()) {m_token=std::make_shared<std::atomic_bool>(false);m_progress=-1;}
        emit changed(false);return;
    }
    if(frame>=0&&frame<m_frames.size())m_frames[frame].loading=false;
    if(kind==Task::Stitch) {
        m_progress=-1;
        if(task.stitched.success) {
            m_result=std::move(task.stitched);m_hasResult=true;m_active=-1;++m_resultRevision;
            m_committedOutputs.append(m_result.outputPly);
            int fallback=0;for(const auto &p:m_result.pairs)if(p.poseFallback)++fallback;
            m_status=tr("事务提交完成 · 回退 %1 对 · 质量策略 warn_only（非严格质量通过）\n%2").arg(fallback).arg(m_result.outputPly);
            m_status+=tr("\n来源映射：%1\n报告：%2").arg(m_result.mappingFile,m_result.reportFile);
            for(int i=0;i<m_result.pairs.size();++i){const auto &pair=m_result.pairs.at(i);
                m_status+=tr("\n相邻对 %1：%2 · %3").arg(i+1).arg(pair.fineApplied?tr("精配准已应用"):tr("位姿回退/仅位姿")).arg(pair.reason);}
        } else m_status=tr("拼接未完成：%1 %2").arg(task.stitched.errorCode,task.stitched.message);
        emit changed(true);return;
    }
    if(kind==Task::SaveGeometry) {
        m_progress=-1;
        if(task.geometrySaved) {
            m_savedGeometryPath=task.savedGeometryPath;m_geometryConfirmed=true;
            m_status=tr("几何结果已保存：%1").arg(task.savedGeometryPath);m_committedOutputs.append(task.savedGeometryPath);
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
        if(kind==Task::LoadDisplay) {f.problem=true;f.status=task.cancelled?tr("已取消，可重试"):task.error;m_status=tr("第 %1 帧：%2").arg(frame+1).arg(f.status);if(m_matching)pauseAt(frame,f.status);}
        else {if(m_matching)pauseAt(frame,task.error);else {f.status=task.error;m_status=task.error;}}
    } else if(kind==Task::LoadDisplay) {
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
        m_status=tr("第 %1 帧已完成读取、坐标转换和完整索引").arg(frame+1);
        if(!f.digest.isEmpty()&&f.digest!=task.sourceDigest) {
            f.hasCandidate=false;f.candidate={};f.planeIndices.clear();f.interferenceIndices.clear();f.pendingInterference.clear();
            f.workpieceMask={};f.interferenceMask={};f.pendingInterferenceMask={};
            f.workpieceMaskError.clear();f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();f.reviewed=f.confirmed=false;
            f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();f.highSideChecking=false;
            invalidateResult();if(frame==m_reference){invalidateReferenceDependents();m_reference=-1;}
            m_status=tr("第 %1 帧来源/几何发生变化，旧候选已失效").arg(frame+1);
        }
        f.digest=task.sourceDigest;f.cloud=std::move(task.loaded);f.display=std::move(task.displayPoints);
        f.displayToFull=std::move(task.displayToFullIndices);f.displayLease=std::move(task.displayLease);f.index=task.index;
        f.highSideCandidates.clear();f.highSideError.clear();f.highSideChecking=false;
        f.problem=false;f.status=f.hasCandidate?tr("已重载，候选保留"):tr("已加载，等待选面");
        if(f.hasCandidate)dispatchHighSideSearch(frame);
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
    if(busy()||(m_matching&&!m_paused)||frame<0||frame>=m_frames.size()||frame/6!=m_round)return;
    m_active=frame;m_seeds.clear();
    if(m_frames.at(frame).cloud.fullPoints.isEmpty()&&!m_frames.at(frame).skipped
       && frame<m_config.frames.size()
       && (!m_config.frames.at(frame).depthFrame || !m_frames.at(frame).depthReady)) {
        if(!m_config.frames.at(frame).depthFrame)invalidateGeometry();
        m_loadQueue.push_back(frame);pump();
    }
    emit changed(true);
}
void ScanGeometrySessionV2::showResult() {if(!busy()&&m_hasResult){m_active=-1;m_seeds.clear();emit changed(true);}}
void ScanGeometrySessionV2::retry() {
    if(busy()||m_active<0||(m_matching&&!m_paused))return;
    auto &f=m_frames[m_active];if(f.skipped)invalidateResult();f.problem=false;f.skipped=false;
    f.cloud={};f.index.reset();f.display.clear();f.displayToFull.clear();f.displayLease.reset();
    m_seeds.clear();
    invalidateGeometry();
    m_loadQueue.push_back(m_active);m_status=tr("重新读取当前帧，核对原候选来源");pump();
    emit changed(false);
}
void ScanGeometrySessionV2::pick(int fullIndex,bool interference) {
    if(busy()||!activeFrame()||(m_matching&&!m_paused))return;
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
        Q_UNUSED(retained);return pcv::ui::fitFullPlaneV2(source,seeds,config.tolerance,config.maximumGap,!config.threePoint,index,token,4,budget);
    });emit changed(true);
}
void ScanGeometrySessionV2::setThreePoint(bool enabled) {
    if(busy())return;m_config.threePoint=enabled;m_seeds.clear();emit changed(false);
}
bool ScanGeometrySessionV2::confirmCandidate() {
    if(busy()||!activeFrame()||!activeFrame()->hasCandidate)return false;
    auto &f=m_frames[m_active];
    if(m_reference<0) {
        for(int i=0;i<m_active;++i)if(!m_frames.at(i).skipped){m_status=tr("请先确认首个未跳过帧作为参考面");emit changed(false);return false;}
        m_reference=m_active;invalidateReferenceDependents();
    } else if(m_reference==m_active)invalidateReferenceDependents();
    f.reviewed=true;f.confirmed=true;f.problem=false;f.status=m_active==m_reference?tr("已确认参考面"):tr("已确认");
    if(m_active<m_config.frames.size()&&m_config.frames.at(m_active).depthFrame) {
        f.cloud={};f.index.reset();f.display={};f.displayToFull={};f.displayLease.reset();
    }
    m_status=f.status;emit changed(true);return true;
}
bool ScanGeometrySessionV2::confirmInterference() {
    if(busy()||!activeFrame()||activeFrame()->pendingInterference.isEmpty())return false;
    auto &f=m_frames[m_active];
    if(conflicts(f.pendingInterference,f.planeIndices)) {m_status=tr("干涉面与工件面重叠；请重选干涉面，或先重置工件面");emit changed(false);return false;}
    f.interferenceIndices=std::move(f.pendingInterference);f.pendingInterference.clear();
    f.interferenceMask=std::move(f.pendingInterferenceMask);f.pendingInterferenceMask={};
    f.interferenceMaskError=std::move(f.pendingInterferenceMaskError);f.pendingInterferenceMaskError.clear();
    f.highSideCandidates.erase(std::remove_if(f.highSideCandidates.begin(),f.highSideCandidates.end(),[&](const auto &candidate) {
        return std::binary_search(f.interferenceIndices.cbegin(),f.interferenceIndices.cend(),candidate.seedIndex);
    }),f.highSideCandidates.end());
    invalidateGeometry();
    m_status=tr("人工干涉标注已确认，不参与拼接；请保存几何结果以持久化");emit changed(true);return true;
}
bool ScanGeometrySessionV2::selectHighSideCandidate(int seedIndex) {
    if(busy()||!activeFrame())return false;
    const auto &candidates=activeFrame()->highSideCandidates;
    if(std::none_of(candidates.cbegin(),candidates.cend(),[seedIndex](const auto &candidate){return candidate.seedIndex==seedIndex;}))return false;
    const auto source=activeFrame()->cloud;const auto retained=std::make_shared<const pointcloud::RobotBaseFrame>(source);
    const auto index=activeFrame()->index;const auto config=m_config;const auto token=m_token;const auto budget=m_budget;
    m_seeds.clear();m_seedInterference=true;m_status=tr("正在预览高侧候选平面");
    submit(m_active,Task::Fit,[source,retained,index,seedIndex,config,token,budget] {
        Q_UNUSED(retained);
        const QVector<int> seeds{seedIndex};
        return pcv::ui::fitFullPlaneV2(source,seeds,config.tolerance,config.maximumGap,true,index,token,4,budget);
    });
    emit changed(true);return true;
}
bool ScanGeometrySessionV2::ignoreHighSideCandidate(int seedIndex) {
    if(busy()||!activeFrame())return false;
    auto &f=m_frames[m_active];
    const auto it=std::find_if(f.highSideCandidates.begin(),f.highSideCandidates.end(),[seedIndex](const auto &candidate){return candidate.seedIndex==seedIndex;});
    if(it==f.highSideCandidates.end())return false;
    f.dismissedHighSideSeeds.insert(seedIndex);f.highSideCandidates.erase(it);
    m_status=tr("已忽略所选高侧提醒；不会影响拼接输入");emit changed(true);return true;
}
void ScanGeometrySessionV2::clearInterference() {
    if(busy()||!activeFrame())return;auto &f=m_frames[m_active];f.interferenceIndices.clear();f.pendingInterference.clear();
    f.interferenceMask={};f.pendingInterferenceMask={};f.interferenceMaskError.clear();f.pendingInterferenceMaskError.clear();invalidateGeometry();
    if(f.hasCandidate)dispatchHighSideSearch(m_active);emit changed(true);
}
void ScanGeometrySessionV2::clearPlane() {
    if(busy()||!activeFrame())return;
    if(m_active==m_reference){invalidateReferenceDependents();m_reference=-1;}
    auto &f=m_frames[m_active];f.candidate={};f.planeIndices.clear();f.workpieceMask={};f.workpieceMaskError.clear();f.hasCandidate=f.confirmed=f.reviewed=f.automatic=false;
    f.status=tr("等待重新选面");f.highSideCandidates.clear();f.dismissedHighSideSeeds.clear();f.highSideError.clear();
    m_seeds.clear();invalidateResult();emit changed(true);
}
void ScanGeometrySessionV2::matchRemaining() {
    if(busy()||!m_seeds.isEmpty()||m_reference<0||!m_frames.at(m_reference).confirmed)return;
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
        options.selection.parallelEnabled=true;options.selection.maxParallelTasks=4;options.selection.processingBudget=budget;
        options.selection.isCancelled=[token]{return token->load();};
        const auto match=matchReferencePlaneV2(source.fullPoints,index,plane,options);
        Task task;task.kind=Task::Match;task.matchStatus=match.status;task.error=match.error;
        task.cancelled=match.status==ReferencePlaneMatchStatusV2::Cancelled;
        if(match.status!=ReferencePlaneMatchStatusV2::Unique)return task;
        const int seed=match.candidates.at(0).seedIndex;
        task=pcv::ui::fitFullPlaneV2(source,{seed},config.tolerance,config.maximumGap,true,index,token,4,budget);
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
    if(!f.hasCandidate||f.cloud.fullPoints.isEmpty()||!f.index)return;
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
        options.selection.parallelEnabled=true;options.selection.maxParallelTasks=4;options.selection.processingBudget=budget;
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
    if(busy()||!m_seeds.isEmpty()||!m_paused||!activeFrame()
       ||(!activeFrame()->skipped&&!activeFrame()->reviewed))return;
    // A paused match can be resumed after the operator reviews or replaces
    // the failed frame. Keep an unresolved frame blocked until that review.
    if(activeFrame()->problem && !activeFrame()->hasCandidate)return;
    m_frames[m_active].problem=false;
    m_paused=false;m_matching=true;m_nextMatch=0;pump();emit changed(false);
}
void ScanGeometrySessionV2::skipActive() {
    if(busy()||!activeFrame())return;
    m_seeds.clear();
    if(m_active==m_reference){invalidateReferenceDependents();m_reference=-1;}
    auto &f=m_frames[m_active];f.skipped=true;f.problem=false;f.hasCandidate=f.confirmed=f.reviewed=false;
    f.candidate={};f.planeIndices.clear();f.workpieceMask={};f.workpieceMaskError.clear();
    f.pendingInterference.clear();f.pendingInterferenceMask={};f.pendingInterferenceMaskError.clear();
    f.highSideCandidates.clear();f.highSideError.clear();
    f.status=tr("用户明确跳过");invalidateResult();m_status=f.status;emit changed(true);
}
QString ScanGeometrySessionV2::summary() const {
    int keep=0,skip=0,problem=0,unreviewed=0;
    for(const auto &f:m_frames) {if(f.skipped)++skip;else if(f.hasCandidate)++keep;if(f.problem)++problem;if(!f.skipped&&(!f.hasCandidate||!f.reviewed))++unreviewed;}
    return tr("保留 %1 · 跳过 %2 · 异常 %3 · 未审核 %4").arg(keep).arg(skip).arg(problem).arg(unreviewed);
}
bool ScanGeometrySessionV2::canReviewAll() const {
    if(busy()||m_matching||!m_seeds.isEmpty()||m_reference<0)return false;int valid=0;
    for(const auto &f:m_frames)if(!f.skipped){if(f.problem||!f.hasCandidate||!f.reviewed
        ||conflicts(f.planeIndices,f.interferenceIndices)||conflicts(f.planeIndices,f.pendingInterference))return false;++valid;}
    return valid>=2;
}
void ScanGeometrySessionV2::confirmAll() {
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
    if(!canStitch())return;invalidateResult();
    pcv::stitching::SelectedPlaneStitchRequest request;
    for(const auto &f:m_frames)if(!f.skipped)request.frames.append(f.candidate);
    request.calibrationPath=m_config.calibration;request.options.fineEnabled=!m_config.poseOnly;
    request.options.maxParallelTasks=4;
    request.outputDirectory=QDir(m_config.outputRoot).filePath(QStringLiteral("selected_plane_%1_%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"),QUuid::createUuid().toString(QUuid::Id128)));
    const auto token=m_token;request.isCancelled=[token]{return token->load();};const auto revision=m_revision;
    request.progress=[this,token,revision](float value,const QString &stage) {
        QMetaObject::invokeMethod(this,[this,token,revision,value,stage] {
            if(token!=m_token||token->load()||revision!=m_revision)return;
            m_progress=qBound(0,qRound(value*100),100);m_status=stage;emit changed(false);
        },Qt::QueuedConnection);
    };
    m_status=tr("模块 30：所选平面拼接与事务输出");m_progress=-1;
    submit(-1,Task::Stitch,[request] {Task task;task.kind=Task::Stitch;task.stitched=pcv::stitching::stitchSelectedPlanes(request);return task;});emit changed(false);
}
void ScanGeometrySessionV2::erase(const QVector<int> &indices,quint64 resultRevision) {
    if(busy()||!m_hasResult||m_active!=-1||resultRevision!=m_resultRevision)return;auto next=m_removed;
    for(int i:indices)if(i>=0&&i<m_result.points.size())next.insert(i);
    if(next==m_removed)return;m_undo.append(m_removed);m_removed=std::move(next);invalidateGeometry();emit changed(true);
}
void ScanGeometrySessionV2::undo() {if(!busy()&&m_active==-1&&!m_undo.isEmpty()){m_removed=m_undo.takeLast();invalidateGeometry();emit changed(true);}}
void ScanGeometrySessionV2::restore() {if(!busy()&&m_active==-1&&!m_removed.isEmpty()){m_undo.append(m_removed);m_removed.clear();invalidateGeometry();emit changed(true);}}
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
    if(!canConfirmGeometry())return;
    if(!m_wobj1.valid&&!generateWobj1())return;
    GeometryOutputRequestV1 request;request.result=m_result;request.removed=m_removed;request.wobj1=m_wobj1;
    request.referenceFrame=m_reference;request.resultRevision=m_resultRevision;
    request.outputRoot=QFileInfo(m_result.outputPly).absolutePath();
    for(int i=0;i<m_frames.size();++i) {
        GeometryFrameOutputV1 frame;frame.id=m_frames.at(i).id;frame.sourceFile=m_config.frames.at(i).path;
        frame.scanJson=m_config.frames.at(i).scanJson;frame.interferenceIndices=m_frames.at(i).interferenceIndices;
        request.frames.append(frame);
    }
    const auto token=m_token;request.isCancelled=[token]{return token->load();};
    // Keep an extra shared immutable snapshot alive across all worker reads.
    const auto retained=std::make_shared<const GeometryOutputRequestV1>(request);
    m_status=tr("正在事务保存修剪结果、来源和人工干涉标注");
    submit(-1,Task::SaveGeometry,[request,retained] {
        Q_UNUSED(retained);const auto saved=saveSelectedPlaneGeometryV1(request);
        Task task;task.kind=Task::SaveGeometry;task.geometrySaved=saved.success;task.ok=saved.success;
        task.cancelled=saved.cancelled;task.error=saved.error;task.savedGeometryPath=saved.sidecar;return task;
    });emit changed(false);
}
void ScanGeometrySessionV2::confirmGeometry() {saveGeometry();}
} // namespace pcv::demo
