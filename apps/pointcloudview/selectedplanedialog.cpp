#include "selectedplanedialog.h"
#include "selected_plane_launcher.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/render/pointcloud_canvas.h>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace {
bool parsePose(QString text,pointcloud::RobotPose *pose) {
    text.replace(',', ' ');
    const auto fields=text.split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts);
    if(fields.size()!=6) return false;
    double values[6]{};
    for(int i=0;i<6;++i) { bool ok=false;values[i]=fields.at(i).toDouble(&ok);if(!ok||!std::isfinite(values[i])) return false; }
    pose->x=values[0];pose->y=values[1];pose->z=values[2];pose->rx=values[3];pose->ry=values[4];pose->rz=values[5];return true;
}
}

void showSelectedPlaneDialog(float surfaceToleranceMm, QWidget *parent) {
    SelectedPlaneDialog dialog(surfaceToleranceMm,parent);
    dialog.exec();
}

SelectedPlaneDialog::SelectedPlaneDialog(float tolerance,QWidget *parent):QDialog(parent) {
    setWindowTitle(tr("所选平面拼接（当前会话）"));resize(1250,850);
    auto *root=new QVBoxLayout(this);
    auto *form=new QFormLayout;
    m_calibration=new QLineEdit(this);m_output=new QLineEdit(this);
    form->addRow(tr("共用手眼 XML"),m_calibration);form->addRow(tr("输出根目录（自动新建版本子目录）"),m_output);
    m_tolerance=new QDoubleSpinBox(this);m_tolerance->setRange(0.01,10);m_tolerance->setDecimals(3);m_tolerance->setValue(tolerance);m_tolerance->setSuffix(" mm");
    m_method=new QComboBox(this);m_method->addItems({tr("单击选面"),tr("三点选面（备用）")});
    form->addRow(tr("选面方式"),m_method);
    m_maximumGap=new QDoubleSpinBox(this);m_maximumGap->setRange(0,50);m_maximumGap->setDecimals(3);
    m_maximumGap->setValue(3);m_maximumGap->setSuffix(" mm");
    m_maximumGap->setToolTip(tr("按平面内两侧有效点的最短距离跨越缺失带。0关闭额外跨带；不会生成缺失点。邻近共面物体也可能被并入，请检查预览。"));
    form->addRow(tr("最大跨缺失距离（0关闭）"),m_maximumGap);
    form->addRow(tr("选面垂直距离容差"),m_tolerance);root->addLayout(form);
    auto *actions=new QHBoxLayout;
    const auto button=[this,actions](const QString &name,auto action) {
        auto *b=new QPushButton(name,this);actions->addWidget(b);m_controls.push_back(b);connect(b,&QPushButton::clicked,this,action);return b;
    };
    button(tr("手眼 XML"),[this] { const auto p=QFileDialog::getOpenFileName(this,tr("选择手眼"),{},"XML (*.xml)");if(!p.isEmpty())m_calibration->setText(p); });
    button(tr("输出目录"),[this] { const auto p=QFileDialog::getExistingDirectory(this,tr("输出根目录"));if(!p.isEmpty())m_output->setText(p); });
    button(tr("添加 PLY"),[this] {addFiles();});button(tr("上移"),[this] {moveFrame(-1);});button(tr("下移"),[this] {moveFrame(1);});
    button(tr("移除"),[this] {removeFrame();});button(tr("加载当前帧 / 重选"),[this] {loadFrame();});
    button(tr("撤销取点 / 移除选择"),[this] {
        if(m_hasCandidate) {discardPreview();return;}
        if(!m_seeds.isEmpty()) {m_seeds.removeLast();m_canvas->setSelectedIndices(m_seeds);updateActions();}
        else {invalidateSelection(m_table->currentRow());m_canvas->clearPlaneResult();updateActions();}
    });
    root->addLayout(actions);
    root->addWidget(new QLabel(tr("每帧点击同一物理平面的内部，检查高亮后确认加入，自动加载下一未完成帧。仅修正法向位置和倾角，平面内位置依赖机器人位姿。"),this));
    auto *split=new QSplitter(Qt::Vertical,this);
    m_table=new QTableWidget(0,4,this);m_table->setHorizontalHeaderLabels({tr("PLY"),"Start [X Y Z A B C]","End [X Y Z A B C]",tr("所选平面")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);split->addWidget(m_table);
    m_canvas=new PointCloudCanvas(this);split->addWidget(m_canvas);split->setSizes({180,420});root->addWidget(split,1);
    m_previewLabel=new QLabel(tr("尚无候选平面"),this);root->addWidget(m_previewLabel);
    m_log=new QPlainTextEdit(this);m_log->setReadOnly(true);m_log->setMaximumHeight(130);root->addWidget(m_log);
    auto *run=new QHBoxLayout;m_poseOnly=new QCheckBox(tr("位姿直拼对照（不精配准）"),this);run->addWidget(m_poseOnly);
    m_confirm=new QPushButton(tr("确认加入 / 下一帧"),this);m_controls.push_back(m_confirm);run->addWidget(m_confirm);
    auto *discard=new QPushButton(tr("取消预览"),this);m_controls.push_back(discard);run->addWidget(discard);
    m_start=new QPushButton(tr("拼接所选平面"),this);m_controls.push_back(m_start);run->addWidget(m_start);
    auto *cancel=new QPushButton(tr("取消处理中任务"),this);run->addWidget(cancel);root->addLayout(run);
    connect(m_start,&QPushButton::clicked,this,[this] {stitch();});
    connect(m_confirm,&QPushButton::clicked,this,[this] {confirmPreview();});
    connect(discard,&QPushButton::clicked,this,[this] {discardPreview();});
    connect(cancel,&QPushButton::clicked,this,[this] {if(m_token)m_token->store(true);});
    m_canvas->pointPicked=[this](int index) {picked(index);};
    m_canvas->pointPickingStarted=[this] {if(!m_busy&&m_loadedRow>=0)m_previewTimer.start();};
    connect(m_canvas,&QOpenGLWidget::frameSwapped,this,[this] {
        if(!m_previewRenderedPending||!m_hasCandidate||m_closing)return;
        m_previewRenderedPending=false;
        const double ms=m_previewTimer.nsecsElapsed()/1e6;
        m_candidate.selectionParameters.insert("click_to_preview_ms",ms);
        m_candidate.selectionParameters.insert("display_timing_scope","mouse_release_before_pick_to_first_highlight_frame_swapped");
        log(tr("点击至完整预览：%1 ms（含拾取、排队、识别、快照和首帧绘制）").arg(ms,0,'f',2));
    });
    connect(m_tolerance,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this] {++m_revision;discardPreview();});
    connect(m_maximumGap,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this] {++m_revision;discardPreview();});
    connect(m_method,qOverload<int>(&QComboBox::currentIndexChanged),this,[this] {++m_revision;discardPreview();});
    connect(m_table,&QTableWidget::cellChanged,this,[this](int row,int column) {
        if(column==1||column==2) {invalidateSelection(row);if(row==m_loadedRow)invalidateLoaded();}
    });
    connect(m_calibration,&QLineEdit::textChanged,this,[this] {
        for(int row=0;row<m_selections.size();++row)invalidateSelection(row);invalidateLoaded();
    });
    connect(m_table,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int) {
        if(!m_busy&&row!=m_loadedRow)invalidateLoaded();
    });
    m_watcher=new QFutureWatcher<SelectedPlaneTask>(this);
    connect(m_watcher,&QFutureWatcher<SelectedPlaneTask>::finished,this,[this] {finishTask();});
    updateActions();
}
SelectedPlaneDialog::~SelectedPlaneDialog() {m_closing=true;disconnect(m_watcher,nullptr,this,nullptr);cancelAndWait();}
void SelectedPlaneDialog::closeEvent(QCloseEvent *event) {m_closing=true;cancelAndWait();QDialog::closeEvent(event);}
void SelectedPlaneDialog::cancelAndWait() {++m_revision;if(m_token)m_token->store(true);if(m_watcher&&m_watcher->isRunning())m_watcher->waitForFinished();}
void SelectedPlaneDialog::log(const QString &s) {m_log->appendPlainText(s);}
void SelectedPlaneDialog::busy(bool value) {
    m_busy=value;for(auto *b:m_controls)b->setEnabled(!value);
    m_table->setEnabled(!value);m_calibration->setEnabled(!value);m_output->setEnabled(!value);m_tolerance->setEnabled(!value);m_poseOnly->setEnabled(!value);
    m_method->setEnabled(!value);updateActions();
    setWindowTitle(value?tr("所选平面拼接：处理中，可取消"):tr("所选平面拼接（当前会话）"));
}
void SelectedPlaneDialog::updateActions() {
    m_maximumGap->setEnabled(!m_busy&&m_method->currentIndex()==0);
    m_confirm->setEnabled(!m_busy&&m_hasCandidate);
    bool ready=m_selections.size()>=2;
    for(const auto &selected:std::as_const(m_selections))if(selected.cloud.fullPoints.isEmpty())ready=false;
    m_start->setEnabled(!m_busy&&!m_hasCandidate&&m_seeds.isEmpty()&&ready);
    m_canvas->setSelectionMode(!m_busy&&m_loadedRow>=0&&m_method->currentIndex()==1);
    if(m_method->currentIndex()==0)m_canvas->setToolTip(tr("单击选面，左键拖动旋转，右键平移，滚轮缩放"));
}
void SelectedPlaneDialog::discardPreview() {
    m_candidate={};m_hasCandidate=false;m_previewIndices.clear();m_previewRenderedPending=false;m_seeds.clear();
    m_previewLabel->setText(tr("尚无候选平面；已确认选择保留"));m_canvas->setSelectedIndices({});
    if(m_loadedRow>=0&&m_loadedRow<m_confirmedIndices.size())m_canvas->setExtractedPlane(m_confirmedIndices.at(m_loadedRow));
    else m_canvas->clearPlaneResult();
    updateActions();
}
void SelectedPlaneDialog::confirmPreview() {
    if(m_busy||!m_hasCandidate||m_loadedRow<0)return;
    const int row=m_loadedRow;m_selections[row]=m_candidate;m_confirmedIndices[row]=m_previewIndices;
    {QSignalBlocker block(m_table);m_table->item(row,3)->setText(tr("已确认：%1 点，RMS %2 mm").arg(m_candidate.cloud.fullPoints.size()).arg(m_candidate.rmsMm,0,'g',5));}
    log(tr("第 %1 帧所选平面已确认加入").arg(row+1));discardPreview();
    for(int offset=1;offset<m_selections.size();++offset) {
        const int next=(row+offset)%int(m_selections.size());
        if(m_selections.at(next).cloud.fullPoints.isEmpty()) {m_table->setCurrentCell(next,0);loadFrame();return;}
    }
    log(tr("全部帧已确认，可以点击“拼接所选平面”"));
}
void SelectedPlaneDialog::invalidateLoaded() {++m_revision;m_loadedRow=-1;discardPreview();m_index.reset();m_loaded={};m_canvas->setCloud({});updateActions();}
void SelectedPlaneDialog::invalidateSelection(int row) {
    if(row<0||row>=m_selections.size())return;
    ++m_revision;m_selections[row]={};if(row<m_confirmedIndices.size())m_confirmedIndices[row].clear();
    if(row==m_loadedRow)discardPreview();
    QSignalBlocker block(m_table);m_table->item(row,3)->setText(tr("未选择"));updateActions();
}
void SelectedPlaneDialog::addFiles() {
    const auto files=QFileDialog::getOpenFileNames(this,tr("按扫描顺序添加 PLY"),{},"PLY (*.ply)");
    QSignalBlocker block(m_table);
    for(const auto &path:files) {
        bool duplicate=false;for(int row=0;row<m_table->rowCount();++row)if(m_table->item(row,0)->text()==path)duplicate=true;
        if(duplicate)continue;
        int row=m_table->rowCount();m_table->insertRow(row);
        for(int c=0;c<4;++c) {auto *item=new QTableWidgetItem(c==0?path:c==3?tr("未选择"):QString());if(c==0||c==3)item->setFlags(item->flags()&~Qt::ItemIsEditable);m_table->setItem(row,c,item);}
        m_selections.push_back({});
        m_confirmedIndices.push_back({});
    }
    ++m_revision;updateActions();
}
void SelectedPlaneDialog::moveFrame(int offset) {
    const int row=m_table->currentRow(),other=row+offset;if(row<0||other<0||other>=m_table->rowCount())return;
    invalidateLoaded();QSignalBlocker block(m_table);
    for(int c=0;c<4;++c) {auto *a=m_table->takeItem(row,c),*b=m_table->takeItem(other,c);m_table->setItem(row,c,b);m_table->setItem(other,c,a);}
    m_selections.swapItemsAt(row,other);m_confirmedIndices.swapItemsAt(row,other);m_table->setCurrentCell(other,0);log(tr("顺序已更新；之前输出不再代表当前顺序"));updateActions();
}
void SelectedPlaneDialog::removeFrame() {
    int row=m_table->currentRow();if(row<0)return;invalidateLoaded();QSignalBlocker block(m_table);m_table->removeRow(row);m_selections.removeAt(row);m_confirmedIndices.removeAt(row);updateActions();
}
void SelectedPlaneDialog::loadFrame() {
    int row=m_table->currentRow();if(m_busy||row<0)return;
    pointcloud::RobotPose start,end;
    if(!parsePose(m_table->item(row,1)->text(),&start)||!parsePose(m_table->item(row,2)->text(),&end)) {log(tr("Start/End 必须各包含六个有限数：[X Y Z A B C]"));return;}
    const QString path=m_table->item(row,0)->text(),calibration=m_calibration->text();
    invalidateLoaded();m_taskRow=row;m_taskRevision=m_revision;m_token=std::make_shared<std::atomic_bool>(false);const auto token=m_token;busy(true);
    m_loadTimer.start();
    m_watcher->setFuture(QtConcurrent::run([path,calibration,start,end,token] {
        SelectedPlaneTask task;task.kind=SelectedPlaneTask::Load;
        try {
        const auto cancelled=[token] {return token->load();};
        pointcloud::HandEyeCalibration handEye;
        if(!pointcloud::loadHandEyeCalibration(calibration,&handEye,&task.error))return task;
        pcv::detail::io::PlyReadOptions readOptions;readOptions.isCancelled=cancelled;
        const auto raw=pcv::detail::io::readPlyCached(path,{},readOptions);
        if(!raw.ok) {task.error=raw.error;task.cancelled=raw.cancelled;return task;}
        pointcloud::CloudTransformOptions transformOptions;transformOptions.isCancelled=cancelled;
        auto cloud=pointcloud::transformLineScanToRobotBase(raw.points,handEye,start,end,transformOptions);
        if(!cloud.ok) {task.error=cloud.error;task.cancelled=cloud.cancelled;return task;}
        task.loaded.sourceFile=path;task.loaded.fullPoints=std::move(cloud.points);task.loaded.sourceIndices=std::move(cloud.sourceIndices);task.loaded.scanRatios=std::move(cloud.scanRatios);
        task.loaded.declaredCount=raw.declaredPointCount;task.loaded.startBaseFromFlange=pointcloud::robotPoseToMatrix(start);task.loaded.endBaseFromFlange=pointcloud::robotPoseToMatrix(end);
        task.loaded.flangeFromDepth=handEye.flangeFromDepth;task.loaded.rejectedInvalid=cloud.rejectedInvalid;task.loaded.rejectedRange=cloud.rejectedRange;
        task.loaded.signedTravel=cloud.signedTravel;task.loaded.dominantTravelAxis=cloud.dominantTravelAxis;task.loaded.inputYMinimum=cloud.inputYMinimum;task.loaded.inputYMaximum=cloud.inputYMaximum;
        const auto prepared=pcv::planefitting::preparePlaneSelectionIndex(task.loaded.fullPoints,cancelled);
        if(prepared.cancelled||token->load()){task.cancelled=true;return task;}
        task.index=prepared.index;task.preparationMs=prepared.preparationMs;task.error=prepared.error;
        task.ok=true;return task;
        } catch(const std::bad_alloc &) {task.error=QStringLiteral("内存不足，帧未加载");return task;}
    }));
}
void SelectedPlaneDialog::picked(int index) {
    if(m_busy||m_loadedRow<0||index<0||index>=m_loaded.fullPoints.size()||m_seeds.contains(index))return;
    const bool singleClick=m_method->currentIndex()==0;
    if(singleClick&&!m_index){log(tr("当前帧单击索引未就绪，请重新加载或切换三点选面"));return;}
    if(m_hasCandidate)discardPreview();
    m_seeds.push_back(index);m_canvas->setSelectedIndices(m_seeds);
    updateActions();
    if(!singleClick&&m_seeds.size()!=3)return;
    if(!m_previewTimer.isValid())m_previewTimer.start();
    m_taskRow=m_loadedRow;m_taskRevision=m_revision;
    const auto source=m_loaded;const auto seeds=m_seeds;const float tolerance=m_tolerance->value();
    const auto spatialIndex=m_index;
    const float maximumGap=m_maximumGap->value();
    m_token=std::make_shared<std::atomic_bool>(false);const auto token=m_token;busy(true);
    m_watcher->setFuture(QtConcurrent::run([source,seeds,tolerance,token,singleClick,spatialIndex,maximumGap] {
        SelectedPlaneTask task;task.kind=SelectedPlaneTask::Fit;QElapsedTimer timer;timer.start();
        try {
        pcv::planefitting::PlaneModel model;float rms=0;
        auto &parameters=task.selected.selectionParameters;
        parameters.insert("surface_tolerance_mm",tolerance);
        parameters.insert("selection_timing_scope","worker_fit_classification_and_snapshot_excludes_pick_queue_and_display");
        if(singleClick) {
            pcv::planefitting::SingleClickOptions options;options.surfaceToleranceMm=tolerance;
            options.maximumGapMm=maximumGap;
            options.parallelEnabled=source.fullPoints.size()>=100000;options.maxParallelTasks=4;options.isCancelled=[token]{return token->load();};
            const auto fit=pcv::planefitting::selectPlaneAtPoint(source.fullPoints,spatialIndex,seeds.at(0),options);
            if(!fit.ok()||token->load()){task.error=fit.error;task.cancelled=fit.status==pcv::planefitting::SingleClickStatus::Cancelled||token->load();return task;}
            model=fit.model;rms=fit.rmsMm;task.planeIndices=fit.planeIndices;
            task.selected.selectionMethod=pcv::stitching::PlaneSelectionMethod::SingleClick;
            parameters.insert("grid_size_mm",fit.gridSizeMm);parameters.insert("local_spacing_mm",fit.localSpacingMm);
            parameters.insert("algorithm_version","single_click_plane_selection_v2");
            parameters.insert("maximum_gap_mm",fit.maximumGapMm);
            parameters.insert("sampling_spacing_u_mm",fit.samplingSpacingUMm);parameters.insert("sampling_spacing_v_mm",fit.samplingSpacingVMm);
            parameters.insert("connectivity_radius_u_mm",fit.connectivityRadiusUMm);parameters.insert("connectivity_radius_v_mm",fit.connectivityRadiusVMm);
            parameters.insert("merged_region_count",fit.mergedRegionCount);parameters.insert("refinement_iterations",fit.refinementIterations);
            parameters.insert("refinement_converged",fit.refinementConverged);parameters.insert("gap_merge_ms",fit.gapMergeMs);
            parameters.insert("connectivity_radius_mm",fit.connectivityRadiusMm);parameters.insert("local_support_ratio",fit.localSupportRatio);
            parameters.insert("algorithm_ms",fit.timings.totalMs);
            parameters.insert("local_fit_ms",fit.timings.ransacMs);parameters.insert("grid_growth_ms",fit.timings.candidateMs);
            parameters.insert("classification_ms",fit.timings.classificationMs);parameters.insert("connectivity_ms",fit.timings.connectivityMs);
            parameters.insert("pca_ms",fit.timings.pcaMs);
        } else {
            pcv::planefitting::Options options;options.initialTolerance=1;options.surfaceTolerance=tolerance;options.useZAxisResidual=false;
            options.parallelEnabled=source.fullPoints.size()>=100000;options.maxParallelTasks=4;options.isCancelled=[token] {return token->load();};
            const auto fit=pcv::planefitting::fit(source.fullPoints,seeds,options);
            if(!fit.ok||fit.cancelled||token->load()) {task.error=fit.error;task.cancelled=fit.cancelled||token->load();return task;}
            model=fit.model;rms=fit.rmsError;task.planeIndices=fit.planeIndices;
            parameters.insert("initial_tolerance_mm",1.0);parameters.insert("algorithm_ms",fit.timings.totalMs);
        }
        auto &selected=task.selected;selected.cloud=source;
        selected.cloud.fullPoints.clear();selected.cloud.samplePoints.clear();selected.cloud.sourceIndices.clear();selected.cloud.scanRatios.clear();
        std::sort(task.planeIndices.begin(),task.planeIndices.end());
        for(int i:task.planeIndices) {if(token->load()){task.cancelled=true;return task;}selected.cloud.fullPoints.push_back(source.fullPoints.at(i));selected.cloud.sourceIndices.push_back(source.sourceIndices.at(i));selected.cloud.scanRatios.push_back(source.scanRatios.at(i));}
        selected.normal=QVector3D(model.a,model.b,model.c).normalized();
        const auto center=pcv::planefitting::calculateBoundsCenter(source.fullPoints,task.planeIndices);
        if(!center.ok){task.error=center.error;return task;}
        selected.center=center.center-selected.normal*(QVector3D::dotProduct(selected.normal,center.center)+model.d);
        selected.rmsMm=rms;selected.originalValidPointCount=source.fullPoints.size();selected.selectionMs=timer.elapsed();
        for(int i:seeds)selected.seedSourceIndices.push_back(source.sourceIndices.at(i));
        task.ok=true;return task;
        } catch(const std::bad_alloc &) {task.error=QStringLiteral("内存不足，保留原选择");return task;}
    }));
}
void SelectedPlaneDialog::stitch() {
    if(m_busy||m_hasCandidate||!m_seeds.isEmpty()){log(tr("请先确认或取消当前预览/取点"));return;}
    if(m_selections.size()<2){log(tr("至少需要两帧"));return;}
    for(int i=0;i<m_selections.size();++i)if(m_selections.at(i).cloud.fullPoints.isEmpty()){log(tr("第 %1 帧尚未选择平面").arg(i+1));return;}
    if(m_output->text().trimmed().isEmpty()){log(tr("请选择输出根目录"));return;}
    pcv::stitching::SelectedPlaneStitchRequest request;request.frames=m_selections;request.calibrationPath=m_calibration->text();request.options.fineEnabled=!m_poseOnly->isChecked();
    request.outputDirectory=QDir(m_output->text()).filePath(QStringLiteral("selected_plane_v1_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")));
    int version=2;const QString base=request.outputDirectory;while(QFileInfo::exists(request.outputDirectory))request.outputDirectory=base+QStringLiteral("_%1").arg(version++);
    m_token=std::make_shared<std::atomic_bool>(false);const auto token=m_token;request.isCancelled=[token]{return token->load();};m_taskRevision=m_revision;busy(true);
    log(tr("输出目录：%1；拼接对象只有所选点").arg(request.outputDirectory));
    m_watcher->setFuture(QtConcurrent::run([request] {SelectedPlaneTask task;task.kind=SelectedPlaneTask::Stitch;task.stitched=pcv::stitching::stitchSelectedPlanes(request);task.ok=task.stitched.success;task.cancelled=task.stitched.cancelled;task.error=task.stitched.message;return task;}));
}
void SelectedPlaneDialog::finishTask() {
    if(m_closing)return;
    const auto task=m_watcher->result();busy(false);
    if(m_taskRevision!=m_revision){log(tr("已丢弃过期结果"));discardPreview();return;}
    if(task.cancelled||(m_token&&m_token->load())) {log(tr("已取消，原选择保留"));discardPreview();return;}
    if(!task.ok){log(tr("失败：%1；原选择保留").arg(task.error));discardPreview();return;}
    if(task.kind==SelectedPlaneTask::Load) {
        m_loaded=task.loaded;m_index=task.index;m_loadedRow=m_taskRow;m_canvas->setCloud(m_loaded.fullPoints);discardPreview();
        log(tr("已加载第 %1 帧 robot_base 点云；索引准备 %2 ms，加载/转换/准备共 %3 ms。点击平面内部并确认。").arg(m_loadedRow+1).arg(task.preparationMs,0,'f',2).arg(m_loadTimer.elapsed()));
        if(!task.error.isEmpty())log(tr("单击索引未就绪：%1；可使用三点方式").arg(task.error));
    } else if(task.kind==SelectedPlaneTask::Fit) {
        m_candidate=task.selected;m_previewIndices=task.planeIndices;m_hasCandidate=true;
        m_canvas->setExtractedPlane(task.planeIndices);m_seeds.clear();m_canvas->setSelectedIndices({});
        m_previewLabel->setText(tr("待确认：%1 点，RMS %2 mm，容差 %3 mm，后台处理 %4 ms").arg(task.selected.cloud.fullPoints.size()).arg(task.selected.rmsMm,0,'g',5).arg(m_tolerance->value()).arg(task.selected.selectionMs));
        m_previewRenderedPending=true;updateActions();
        log(tr("第 %1 帧候选就绪；检查高亮后点击“确认加入 / 下一帧”，再次点击可重选。").arg(m_taskRow+1));
    } else {
        invalidateLoaded();m_canvas->setCloud(task.stitched.points);log(task.stitched.message+"\n"+task.stitched.outputPly);
        for(int i=0;i<task.stitched.pairs.size();++i){const auto &p=task.stitched.pairs.at(i);log(tr("相邻对 %1：%2；%3；对应 %4，RMSE %5 mm，层数 %6").arg(i+1).arg(p.fineApplied?tr("精配准完成，不可观测方向保持位姿"):tr("退回位姿直拼")).arg(p.reason).arg(p.correspondences).arg(p.rmseMm).arg(p.completedLevels));}
    }
    updateActions();
}
