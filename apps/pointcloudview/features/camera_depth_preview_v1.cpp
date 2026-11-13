#include "camera_depth_preview_v1.h"
#include <pcv/render/depth_image_canvas_v1.h>
#include <pcv/io/artifact_transaction.h>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
bool outsideSource(const QString &directory) {
    const auto path = QFileInfo(directory).canonicalFilePath();
    const auto source = QDir(QString::fromUtf8(PCV_CAPTURE_UI_SOURCE_ROOT)).canonicalPath();
    return !path.isEmpty() && path.compare(source, Qt::CaseInsensitive) != 0 && !path.startsWith(source+"/",Qt::CaseInsensitive);
}
QString newFolder(const QString &parent, const QString &prefix) {
    const auto stem = prefix+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    QString name = stem;
    for (int i = 1; QDir(parent).exists(name); ++i) name = stem+"_"+QString::number(i);
    return QDir(parent).filePath(name);
}
QString sourceFingerprint(const pcv::camera::CameraFrame &frame) {
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(frame.data);
    hash.addData(frame.intensityData);
    const QJsonObject meta{{"width",qint64(frame.width)},{"height",qint64(frame.height)},
        {"number",qint64(frame.frameNumber)},{"timestamp",frame.timeStamp},
        {"scale",QJsonArray{frame.xScale,frame.yScale,frame.zScale}},
        {"offset",QJsonArray{frame.xOffset,frame.yOffset,frame.zOffset}},
        {"sdk",frame.sdkVersion},{"configuration",frame.conversionFingerprint},
        {"reserved",QString::fromLatin1(frame.sdkReserved.toHex())}};
    hash.addData(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    // Auxiliary metadata is part of the source identity, not just its pixels.
    for (qint64 value : frame.exposureTimeStamps) hash.addData(QByteArray::number(value)+',');
    for (qint32 value : frame.encoders) hash.addData(QByteArray::number(value)+',');
    return QString::fromLatin1(hash.result().toHex());
}
bool inside(const QImage &mask, QPoint pixel) {
    return mask.rect().contains(pixel) && mask.constScanLine(pixel.y())[pixel.x()] != 0;
}
}
CameraDepthPreviewV1::CameraDepthPreviewV1(QWidget *parent) : QWidget(parent) {
    setObjectName("cameraDepthPreviewV1");
    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    m_select = new QPushButton(tr("固定当前帧选面")); m_confirm = new QPushButton(tr("确认平面"));
    m_save = new QPushButton(tr("保存选面"));
    auto *intensity = new QCheckBox(tr("显示亮度图"));
    toolbar->addWidget(m_select); toolbar->addWidget(m_confirm); toolbar->addWidget(m_save); toolbar->addWidget(intensity); toolbar->addStretch();
    layout->addLayout(toolbar); m_canvas = new pcv::render::DepthImageCanvasV1(this); layout->addWidget(m_canvas,1);
    auto *actions = new QHBoxLayout;
    m_full = new QPushButton(tr("整帧点云 → 主窗口")); m_plane = new QPushButton(tr("选面点云 → 主窗口"));
    m_exportFull = new QPushButton(tr("导出整帧 PLY")); m_exportPlane = new QPushButton(tr("导出选面 PLY"));
    for (auto *button : {m_full,m_plane,m_exportFull,m_exportPlane}) actions->addWidget(button);
    layout->addLayout(actions); m_status = new QLabel(tr("深度预览：等待完整帧")); m_status->setWordWrap(true); layout->addWidget(m_status);
    m_debounce = new QTimer(this); m_debounce->setSingleShot(true); m_debounce->setInterval(100);
    connect(m_debounce,&QTimer::timeout,this,[this]{schedule();});
    connect(m_select,&QPushButton::clicked,this,[this]{toggleSelection();});
    connect(intensity,&QCheckBox::toggled,this,[this](bool value){m_canvas->showIntensity(value);});
    m_canvas->hovered = [this](QPoint pixel){hover(pixel);};
    m_canvas->clicked = [this](QPoint pixel){
        if (!m_selecting || m_worker || !inside(m_candidateMask,pixel)) return;
        m_locked = true; m_debounce->stop();
        m_status->setText(tr("候选已锁定，请确认平面；再次点击“重新选面”可重选")); updateActions();
    };
    connect(m_confirm,&QPushButton::clicked,this,[this]{
        if (!m_locked || m_candidateMask.isNull() || m_worker) return;
        m_confirmedMask = m_candidateMask; m_confirmedModel = m_candidateModel;
        m_status->setText(tr("已确认完整平面，可保存选区、转换点云或导出；点云坐标为 camera/mm")); updateActions();
    });
    connect(m_save,&QPushButton::clicked,this,[this]{saveSelection();});
    connect(m_full,&QPushButton::clicked,this,[this]{convert(false,false);});
    connect(m_plane,&QPushButton::clicked,this,[this]{convert(true,false);});
    connect(m_exportFull,&QPushButton::clicked,this,[this]{convert(false,true);});
    connect(m_exportPlane,&QPushButton::clicked,this,[this]{convert(true,true);});
    updateActions();
}
CameraDepthPreviewV1::~CameraDepthPreviewV1() { cancelAndWait(); }
void CameraDepthPreviewV1::cancelAndWait() {
    m_closing = true; ++m_revision; m_debounce->stop();
    if (m_token) m_token->store(true);
    if (m_worker) { m_worker->disconnect(this); m_worker->waitForFinished(); delete m_worker; m_worker = nullptr; }
    releasePrepared(); m_frame.reset(); m_latest.reset(); m_candidateMask = {}; m_confirmedMask = {}; m_selecting = m_locked = false;
}
void CameraDepthPreviewV1::releasePrepared() { m_prepared.reset(); }
void CameraDepthPreviewV1::setFrame(std::shared_ptr<const pcv::camera::CameraFrame> frame, const QString &directory) {
    if (!frame || frame->frameType != pcv::camera::FrameType::DepthC16 || !frame->valid || !frame->completeScan) return;
    m_closing = false;
    m_latest = std::move(frame); m_latestDirectory = directory;
    if (m_selecting || m_worker) return;
    if (m_latest == m_frame) { m_directory = directory; return; }
    ++m_revision; releasePrepared(); m_candidateMask = {}; m_confirmedMask = {}; m_locked = false;
    m_frame = m_latest; m_directory = directory;
    m_canvas->setDepth({m_frame->width,m_frame->height,m_frame->data,m_frame->intensityData});
    m_status->setText(tr("深度帧 #%1：%2 × %3；camera/mm；未展开点云")
        .arg(m_frame->frameNumber).arg(m_frame->width).arg(m_frame->height)); updateActions();
}
void CameraDepthPreviewV1::toggleSelection() {
    if (!m_frame) return;
    if (m_locked) {
        m_locked = false; m_candidateMask = {}; m_canvas->setMask({});
        m_status->setText(tr("请悬停选择平面")); updateActions(); return;
    }
    ++m_revision; m_debounce->stop();
    if (m_token) m_token->store(true);
    m_selecting = !m_selecting; m_hover = {-1,-1}; m_candidateMask = {};
    m_canvas->setMask(m_selecting ? QImage() : m_confirmedMask);
    if (!m_selecting) {
        releasePrepared();
        if (!m_worker) setFrame(m_latest,m_latestDirectory);
    }
    m_status->setText(m_selecting ? tr("已固定当前帧，采集继续；悬停识别平面，左键锁定后确认") : tr("已退出选面，恢复最新深度帧"));
    updateActions();
}
void CameraDepthPreviewV1::hover(QPoint pixel) {
    if (!m_selecting || m_locked || pixel == m_hover) return;
    m_hover = pixel; ++m_hoverRevision;
    if (inside(m_candidateMask,pixel)) { m_debounce->stop(); return; }
    m_candidateMask = {}; m_canvas->setMask({}); m_hoverClock.restart();
    if (pixel.x()<0) { m_debounce->stop(); if (m_token && m_prepared) m_token->store(true); return; }
    if (m_token && m_worker && m_prepared) m_token->store(true);
    m_debounce->start();
}
void CameraDepthPreviewV1::schedule() {
    if (!m_worker && m_selecting && !m_locked && m_hover.x()>=0) run(!m_prepared,false,false,true);
}
void CameraDepthPreviewV1::convert(bool selected, bool exportPly) {
    if (!m_frame || m_worker || (selected && m_confirmedMask.isNull())) return;
    if (!exportPly && canPublish && !canPublish()) { m_status->setText(tr("主窗口有任务正在执行，请稍后转换")); return; }
    run(false,selected,exportPly,false);
}
void CameraDepthPreviewV1::run(bool prepareIndex, bool selected, bool exportPly, bool selectionJob) {
    const auto frame = m_frame; const auto prepared = m_prepared; const auto mask = m_confirmedMask;
    const auto token = std::make_shared<std::atomic_bool>(false); m_token = token;
    const auto revision = m_revision, hoverRevision = m_hoverRevision; const auto pixel = m_hover;
    QString output;
    if (exportPly) {
        const auto parent = QFileDialog::getExistingDirectory(this,tr("选择新版本 PLY 导出目录"));
        if (parent.isEmpty()) return;
        if (!outsideSource(parent)) { m_status->setText(tr("请选择源码树外的输出目录")); return; }
        output = newFolder(parent,selected ? "camera_plane_v1_" : "camera_frame_v1_");
    }
    m_worker = new QFutureWatcher<WorkResult>(this);
    auto *watcher = m_worker;
    connect(watcher,&QFutureWatcher<WorkResult>::finished,this,[this,watcher,revision,hoverRevision,selectionJob,prepareIndex,exportPly,output,token]{
        WorkResult result;
        try { result = watcher->result(); } catch (const std::exception &e) { result.error = QString::fromLocal8Bit(e.what()); }
        m_worker = nullptr; delete watcher;
        if (m_closing || revision != m_revision || token->load()) {
            if (!m_selecting) { releasePrepared(); setFrame(m_latest,m_latestDirectory); }
            updateActions(); if (m_selecting && m_hover.x()>=0) m_debounce->start(); return;
        }
        if (selectionJob && result.prepared) m_prepared = result.prepared;
        if (!result.error.isEmpty()) { m_status->setText(result.error); updateActions(); return; }
        if (selectionJob && prepareIndex) {
            m_conversionMs = result.conversionMs; m_indexMs = result.indexMs;
            qInfo() << "camera_depth_prepare conversion_ms=" << result.conversionMs << "index_ms=" << result.indexMs;
            if (!m_debounce->isActive()) schedule();
        } else if (selectionJob) {
            if (hoverRevision != m_hoverRevision) { m_debounce->start(); return; }
            m_candidateMask = result.mask; m_candidateModel = result.fit.model; m_canvas->setMask(m_candidateMask);
            m_selectionMs = result.selectMs;
            m_lastDiagnostics = result.fit;
            m_lastDiagnostics.planeIndices.clear();
            m_status->setText(tr("完整候选 %1 点；RMS %2 mm；本次耗时 %3 ms；左键锁定后确认")
                .arg(result.fit.planeIndices.size()).arg(result.fit.rmsMm,0,'f',4).arg(m_hoverClock.elapsed()));
            qInfo() << "camera_depth_highlight total_ms=" << m_hoverClock.elapsed() << "selection_ms=" << result.selectMs;
        } else if (exportPly) m_status->setText(tr("已保存 camera/mm 点云：%1").arg(output));
        else if (publishCloud && (!canPublish || canPublish())) {
            publishCloud(result.output); m_status->setText(tr("已发布 %1 个实测点，camera/mm").arg(result.output.size()));
        } else m_status->setText(tr("主窗口任务状态已变化，本次结果未发布"));
        updateActions();
    });
    m_worker->setFuture(QtConcurrent::run([frame,prepared,mask,pixel,token,prepareIndex,selected,selectionJob,output]{
        WorkResult result;
        const auto cancelled = [token]{return token->load();};
        auto active = prepared;
        if (!active) {
            QElapsedTimer timer; timer.start();
            auto converted = pcv::camera::materializeCameraFrame(*frame);
            result.conversionMs = timer.nsecsElapsed()/1e6;
            if (!converted.ok || cancelled()) { result.error = converted.error.isEmpty() ? QStringLiteral("cancelled") : converted.error; return result; }
            auto owned = std::make_shared<Prepared>(); owned->points = std::move(converted.points); owned->indices = std::move(converted.sourceIndices);
            owned->budget = std::make_shared<pcv::ProcessingBudget>(256u*1024u*1024u);
            if (prepareIndex) {
                pcv::planefitting::IndexPreparationOptions options;
                options.parallelEnabled = true; options.maxParallelTasks = 4; options.cachePointBuckets = true;
                options.processingBudget = owned->budget;
                timer.restart();
                const auto index = pcv::planefitting::preparePlaneSelectionIndex(owned->points,cancelled,options);
                result.indexMs = timer.nsecsElapsed()/1e6;
                if (!index.index) { result.error = index.error; return result; }
                owned->index = index.index;
            }
            active = owned;
        }
        if (selectionJob) {
            result.prepared = active;
            if (prepareIndex || cancelled()) return result;
            const qsizetype raster = qsizetype(pixel.y())*frame->width+pixel.x();
            const auto found = std::lower_bound(active->indices.cbegin(),active->indices.cend(),raster);
            if (found == active->indices.cend() || *found != raster) { result.error = QStringLiteral("鼠标位置没有有效实测点"); return result; }
            pcv::planefitting::SingleClickOptions options;
            options.parallelEnabled = true; options.maxParallelTasks = 4; options.isCancelled = cancelled;
            options.processingBudget = active->budget;
            QElapsedTimer timer; timer.start();
            result.fit = pcv::planefitting::selectPlaneAtPoint(active->points,active->index,int(found-active->indices.cbegin()),options);
            result.selectMs = timer.nsecsElapsed()/1e6;
            if (!result.fit.ok() || cancelled()) { result.error = result.fit.error; return result; }
            result.mask = QImage(int(frame->width),int(frame->height),QImage::Format_Grayscale8); result.mask.fill(0);
            for (int point : std::as_const(result.fit.planeIndices)) {
                const auto source = active->indices.at(point);
                result.mask.scanLine(int(source/frame->width))[source%frame->width] = 255;
            }
        } else {
            if (selected) {
                for (qsizetype i = 0; i < active->points.size(); ++i) {
                    const auto source = active->indices.at(i);
                    if (mask.constScanLine(int(source/frame->width))[source%frame->width]) result.output.push_back(active->points.at(i));
                }
            } else result.output = active->points;
            if (cancelled()) { result.error = QStringLiteral("cancelled"); return result; }
            if (!output.isEmpty()) {
                pcv::io::ArtifactTransaction tx(output);
                if (!tx.stageBinaryPly("camera.ply",result.output) || cancelled() || !tx.commit())
                    result.error = cancelled() ? QStringLiteral("cancelled") : tx.error();
                result.output.clear();
            }
        }
        return result;
    }));
    m_status->setText(selectionJob ? tr("正在准备完整选面…") : tr("正在转换完整实测数据…")); updateActions();
}
void CameraDepthPreviewV1::saveSelection() {
    if (!m_frame || m_confirmedMask.isNull() || m_worker) return;
    const auto parent = QFileDialog::getExistingDirectory(this,tr("选择新版本选面保存目录"));
    if (parent.isEmpty()) return;
    if (!outsideSource(parent)) { m_status->setText(tr("请选择源码树外的输出目录")); return; }
    saveSelectionTo(parent);
}
void CameraDepthPreviewV1::saveSelectionTo(const QString &parent) {
    if (!m_frame || m_confirmedMask.isNull() || m_worker || !outsideSource(parent)) return;
    const auto folder = newFolder(parent,"camera_plane_selection_v1_");
    const auto &model = m_confirmedModel;
    QJsonObject meta{{"schema","pcv-camera-plane-selection-v1"},{"source_fingerprint",sourceFingerprint(*m_frame)},
        {"source_directory",m_directory},{"width",qint64(m_frame->width)},{"height",qint64(m_frame->height)},
        {"coordinate_frame","camera"},{"units","mm"},{"mask","mask.png"},
        {"plane",QJsonArray{model.a,model.b,model.c,model.d}},
        {"surface_tolerance_mm",0.4},{"maximum_gap_mm",3.0},{"sdk_version",m_frame->sdkVersion},
        {"conversion_fingerprint",m_frame->conversionFingerprint}};
    pcv::io::ArtifactTransaction tx(folder);
    if (!tx.stageGrayscalePng("mask.png",m_confirmedMask)
        || !tx.stageJson("selection.json",QJsonDocument(meta).toJson()) || !tx.commit()) m_status->setText(tx.error());
    else m_status->setText(tr("已保存完整选面：%1").arg(folder));
}
void CameraDepthPreviewV1::updateActions() {
    const bool ready = bool(m_frame) && !m_closing, idle = !m_worker;
    m_select->setEnabled(ready); m_select->setText(m_locked ? tr("重新选面") : m_selecting ? tr("退出选面 / 恢复实时") : tr("固定当前帧选面"));
    m_confirm->setEnabled(ready && idle && m_locked); m_save->setEnabled(ready && idle && !m_confirmedMask.isNull());
    m_full->setEnabled(ready && idle); m_exportFull->setEnabled(ready && idle);
    m_plane->setEnabled(ready && idle && !m_confirmedMask.isNull()); m_exportPlane->setEnabled(m_plane->isEnabled());
}
