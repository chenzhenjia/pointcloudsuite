#include "selectedplanedialog.h"
#include "selected_plane_launcher.h"
#include "features/workpiece_template_dialog_v1.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/render/pointcloud_canvas.h>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace {

// Display-only sampling limit. Selection, snapshots, stitching and template
// saving always keep the complete point cloud.
constexpr qsizetype kDenseDisplayLimit = 300000;
constexpr int kPanelsPerRound = 6;
constexpr int kMaximumActiveJobs = 2;
constexpr int kWorkersWithTwoJobs = 2;
constexpr int kWorkersWithOneJob = 4;
constexpr qsizetype kCancellationCheckStride = 4096;
// Shared by current-round display mappings and every active task's temporary
// acceleration buffer. It never limits the six complete frames themselves.
constexpr size_t kProcessingBudgetBytes = size_t(256) * 1024 * 1024;

template<class Work>
SelectedPlaneTask measureWorker(const Work &work) {
    QElapsedTimer timer;timer.start();
    auto task=work();
    task.startedAt=timer;
    task.workerMs=timer.elapsed();
    task.completedAt.start();
    return task;
}

int roundCountOf(int frameCount)
{
    return frameCount <= 0 ? 0 : (frameCount + kPanelsPerRound - 1) / kPanelsPerRound;
}

int firstRowOfRound(int round) { return round * kPanelsPerRound; }

int lastRowOfRound(int round, int frameCount)
{
    return qMin(frameCount, firstRowOfRound(round) + kPanelsPerRound);
}

bool parsePose(QString text, pointcloud::RobotPose *pose)
{
    text.replace(',', ' ');
    const auto fields = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (fields.size() != 6) return false;
    double values[6]{};
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        values[i] = fields.at(i).toDouble(&ok);
        if (!ok || !std::isfinite(values[i])) return false;
    }
    pose->x = values[0];
    pose->y = values[1];
    pose->z = values[2];
    pose->rx = values[3];
    pose->ry = values[4];
    pose->rz = values[5];
    return true;
}

QString selectionMethodText(pcv::stitching::PlaneSelectionMethod method)
{
    return method == pcv::stitching::PlaneSelectionMethod::SingleClick
        ? QStringLiteral("single_click_plane_selection")
        : QStringLiteral("three_point_plane_fit");
}

QString baseNameOf(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

// Runs on a worker thread. Every input is an immutable snapshot; the result is
// private and only published after all futures joined.
SelectedPlaneTask fitFullPlane(const pointcloud::RobotBaseFrame &source, const QVector<int> &seeds,
    float tolerance, float maximumGap, bool singleClick,
    const pcv::planefitting::PlaneSelectionIndexPtr &spatialIndex,
    const std::shared_ptr<std::atomic_bool> &token, int workers,
    const std::shared_ptr<pcv::ProcessingBudget> &budget) {
    return pcv::ui::fitFullPlaneV2(source,seeds,tolerance,maximumGap,singleClick,spatialIndex,token,workers,budget);
}

} // namespace

void showSelectedPlaneDialog(float surfaceToleranceMm, QWidget *parent) {
    SelectedPlaneDialog dialog(surfaceToleranceMm,parent);
    dialog.exec();
}

QDialog *createSelectedPlanePage(float surfaceToleranceMm, QWidget *parent) {
    return new SelectedPlaneDialog(surfaceToleranceMm, parent);
}

void cancelSelectedPlanePage(QDialog *page) {
    if (page) static_cast<SelectedPlaneDialog *>(page)->cancelAndWait();
}

SelectedPlaneDialog::SelectedPlaneDialog(float tolerance, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("多段所选平面拼接：六分屏选面（当前会话）"));
    resize(1480,940);
    m_budget=std::make_shared<pcv::ProcessingBudget>(kProcessingBudgetBytes);

    auto *root=new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    root->setSpacing(6);
    // Keep the six canvases dominant at the compact 1280x720 target size.
    // Parameters and diagnostics remain available without occupying the page.
    auto *configToggle=new QToolButton(this);
    configToggle->setText(tr("帧配置 / 文件列表"));
    configToggle->setCheckable(true);
    configToggle->setChecked(false);
    configToggle->setArrowType(Qt::RightArrow);
    configToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    configToggle->setObjectName(QStringLiteral("selected_plane_config_toggle"));
    root->addWidget(configToggle);
    auto *configPanel=new QWidget(this);
    configPanel->setObjectName(QStringLiteral("selected_plane_config_panel"));
    configPanel->hide();
    auto *configLayout=new QVBoxLayout(configPanel);
    configLayout->setContentsMargins(0,0,0,0);
    configLayout->setSpacing(4);
    root->addWidget(configPanel);
    connect(configToggle,&QToolButton::toggled,this,[configToggle,configPanel](bool expanded) {
        configPanel->setVisible(expanded);
        configToggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
    });

    auto *form=new QFormLayout;
    form->setSpacing(4);
    m_calibration=new QLineEdit(this);
    m_calibration->setObjectName(QStringLiteral("selected_plane_calibration"));
    m_output=new QLineEdit(this);
    m_output->setObjectName(QStringLiteral("selected_plane_output_root"));
    form->addRow(tr("共用手眼 XML"),m_calibration);
    form->addRow(tr("输出根目录（自动新建版本子目录）"),m_output);
    m_method=new QComboBox(this);
    m_method->setObjectName(QStringLiteral("selected_plane_method"));
    m_method->addItems({tr("单击选面"),tr("三点选面（备用）")});
    form->addRow(tr("选面方式"),m_method);
    m_tolerance=new QDoubleSpinBox(this);
    m_tolerance->setObjectName(QStringLiteral("selected_plane_tolerance"));
    m_tolerance->setRange(0.01,10);m_tolerance->setDecimals(3);m_tolerance->setValue(tolerance);m_tolerance->setSuffix(" mm");
    form->addRow(tr("选面垂直距离容差"),m_tolerance);
    m_maximumGap=new QDoubleSpinBox(this);
    m_maximumGap->setObjectName(QStringLiteral("selected_plane_maximum_gap"));
    m_maximumGap->setRange(0,50);m_maximumGap->setDecimals(3);m_maximumGap->setValue(3);m_maximumGap->setSuffix(" mm");
    m_maximumGap->setToolTip(tr("按平面内两侧有效点的最短距离跨越缺失带。0关闭额外跨带；不会生成缺失点。邻近共面物体也可能被并入，请检查预览。"));
    form->addRow(tr("最大跨缺失距离（0关闭）"),m_maximumGap);
    configLayout->addLayout(form);

    auto *actions=new QHBoxLayout;
    const auto actionButton=[this,actions](const QString &name,auto slot) {
        auto *button=new QPushButton(name,this);actions->addWidget(button);
        connect(button,&QPushButton::clicked,this,slot);m_editControls.push_back(button);return button;
    };
    actionButton(tr("手眼 XML"),[this] {
        const auto path=QFileDialog::getOpenFileName(this,tr("选择手眼"),{},QStringLiteral("XML (*.xml)"));
        if(!path.isEmpty())m_calibration->setText(path);
    });
    actionButton(tr("输出目录"),[this] {
        const auto path=QFileDialog::getExistingDirectory(this,tr("输出根目录"));
        if(!path.isEmpty())m_output->setText(path);
    });
    actionButton(tr("添加 PLY"),[this] {addFiles();});
    actionButton(tr("上移"),[this] {moveFrame(-1);});
    actionButton(tr("下移"),[this] {moveFrame(1);});
    actionButton(tr("移除"),[this] {removeFrame();});
    configLayout->addLayout(actions);

    m_table=new QTableWidget(0,5,this);
    m_table->setObjectName(QStringLiteral("selected_plane_file_list"));
    m_table->setHorizontalHeaderLabels({tr("序号"),tr("PLY"),QStringLiteral("Start [X Y Z A B C]"),QStringLiteral("End [X Y Z A B C]"),tr("状态")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    m_table->setMaximumHeight(150);
    configLayout->addWidget(m_table);

    auto *roundBar=new QHBoxLayout;
    m_roundLabel=new QLabel(tr("尚未添加任何帧"),this);
    roundBar->addWidget(m_roundLabel,1);
    m_previous=new QPushButton(tr("返回上一轮"),this);
    m_previous->setObjectName(QStringLiteral("selected_plane_previous_round"));
    roundBar->addWidget(m_previous);
    m_loadRound=new QPushButton(tr("一键加载本轮 / 重载"),this);
    roundBar->addWidget(m_loadRound);
    m_confirm=new QPushButton(tr("确认本轮 / 下一轮"),this);
    m_confirm->setObjectName(QStringLiteral("selected_plane_confirm_round"));
    roundBar->addWidget(m_confirm);
    configLayout->addLayout(roundBar);

    m_confirm->setToolTip(tr("每格确认候选或跳过后提交本轮；跳过可撤销，原文件保留。"));

    m_views=new QStackedWidget(this);
    auto *gridPage=new QWidget(m_views);
    m_grid=new QGridLayout(gridPage);
    m_grid->setSpacing(4);
    for(int row=0;row<2;++row)m_grid->setRowStretch(row,1);
    for(int column=0;column<3;++column)m_grid->setColumnStretch(column,1);
    for(int index=0;index<kPanelsPerRound;++index) {
        Panel &panel=m_panels.at(size_t(index));
        auto *cell=new QWidget(gridPage);
        auto *cellLayout=new QVBoxLayout(cell);
        cellLayout->setContentsMargins(2,2,2,2);cellLayout->setSpacing(2);
        panel.label=new QLabel(cell);panel.label->setWordWrap(true);
        cellLayout->addWidget(panel.label);
        panel.canvas=new PointCloudCanvas(cell);
        panel.canvas->setMinimumSize(220,160);
        panel.canvas->installEventFilter(this);
        panel.canvas->pointPicked=[this,index](int fullIndex) {picked(index,fullIndex);};
        panel.canvas->pointPickingStarted=[this,index] {
            const FramePtr frame=frameAtPanel(index);
            if(frame&&!frame->running)frame->clickTimer.start();
        };
        connect(panel.canvas,&QOpenGLWidget::frameSwapped,this,[this,index] {
            const FramePtr frame=frameAtPanel(index);
            if(!frame)return;
            Panel &ui=m_panels.at(size_t(index));
            if(!ui.canvas->hasRenderedCurrentCloud())return;
            if(frame->highlightPending) {
                frame->highlightPending=false;
                log(tr("第 %1 格候选首帧高亮：%2 ms（点击至首帧，含排队、识别、快照与绘制）")
                    .arg(index+1).arg(frame->clickTimer.isValid()?frame->clickTimer.elapsed():0));
            }
            if(frame->displayPending) {
                frame->displayPending=false;
                log(tr("第 %1 格首次展示：%2 ms（含读取/转换/抽样/上传与绘制），完整点数 %3")
                    .arg(index+1).arg(frame->loadTimer.isValid()?frame->loadTimer.elapsed():0).arg(frame->loaded.fullPoints.size()));
            }
        });
        cellLayout->addWidget(panel.canvas,1);
        auto *buttons=new QHBoxLayout;
        buttons->setSpacing(2);
        panel.skip=new QPushButton(tr("跳过本帧"),cell);
        panel.retry=new QPushButton(tr("重试加载"),cell);
        panel.discard=new QPushButton(tr("取消候选"),cell);
        panel.cancel=new QPushButton(tr("取消本格任务"),cell);
        auto *enlargeButton=new QPushButton(tr("放大/返回"),cell);
        for(auto *button:{panel.skip,panel.retry,panel.discard,panel.cancel,enlargeButton})buttons->addWidget(button);
        connect(panel.skip,&QPushButton::clicked,this,[this,index] {toggleSkip(index);});
        connect(panel.retry,&QPushButton::clicked,this,[this,index] {retryFrame(index);});
        connect(panel.discard,&QPushButton::clicked,this,[this,index] {discardPreview(index);});
        connect(panel.cancel,&QPushButton::clicked,this,[this,index] {cancelFrame(index);});
        connect(enlargeButton,&QPushButton::clicked,this,[this,index] {enlarge(index);});
        cellLayout->addLayout(buttons);
        panel.widget=cell;
        cell->hide();
        m_grid->addWidget(cell,index/3,index%3);
    }
    m_views->addWidget(gridPage);

    auto *resultPage=new QWidget(m_views);
    auto *resultLayout=new QVBoxLayout(resultPage);
    auto *resultBar=new QHBoxLayout;
    auto *back=new QPushButton(tr("返回选面"),resultPage);
    resultBar->addWidget(back);
    resultBar->addStretch(1);
    resultLayout->addLayout(resultBar);
    m_resultCanvas=new PointCloudCanvas(resultPage);
    resultLayout->addWidget(m_resultCanvas,1);
    m_views->addWidget(resultPage);
    root->addWidget(m_views,1);

    auto *run=new QHBoxLayout;
    m_poseOnly=new QCheckBox(tr("位姿直拼对照（不精配准）"),this);
    run->addWidget(m_poseOnly);
    run->addStretch(1);
    m_start=new QPushButton(tr("拼接所选平面"),this);
    m_start->setObjectName(QStringLiteral("selected_plane_stitch"));
    m_saveTemplate=new QPushButton(tr("保存为模板"),this);
    m_saveTemplate->setObjectName(QStringLiteral("selected_plane_save_template"));
    run->addWidget(m_start);
    run->addWidget(m_saveTemplate);
    root->addLayout(run);

    auto *logToggle=new QToolButton(this);
    logToggle->setText(tr("运行日志"));
    logToggle->setCheckable(true);
    logToggle->setChecked(false);
    logToggle->setArrowType(Qt::RightArrow);
    logToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    logToggle->setObjectName(QStringLiteral("selected_plane_log_toggle"));
    root->addWidget(logToggle);
    auto *logPanel=new QWidget(this);
    logPanel->setObjectName(QStringLiteral("selected_plane_log_panel"));
    logPanel->hide();
    auto *logLayout=new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0,0,0,0);
    root->addWidget(logPanel);
    connect(logToggle,&QToolButton::toggled,this,[logToggle,logPanel](bool expanded) {
        logPanel->setVisible(expanded);
        logToggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
    });

    m_log=new QPlainTextEdit(logPanel);
    m_log->setObjectName(QStringLiteral("selected_plane_log"));
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(120);
    logLayout->addWidget(m_log);

    m_globalWatcher=new QFutureWatcher<SelectedPlaneTask>(this);
    connect(m_globalWatcher,&QFutureWatcher<SelectedPlaneTask>::finished,this,[this] {finishGlobal();});

    connect(m_loadRound,&QPushButton::clicked,this,[this] {loadRound(qMax(0,m_round));});
    connect(m_confirm,&QPushButton::clicked,this,[this] {confirmRound();});
    connect(m_previous,&QPushButton::clicked,this,[this] {if(m_round>0)loadRound(m_round-1);});
    connect(m_start,&QPushButton::clicked,this,[this] {stitch();});
    connect(m_saveTemplate,&QPushButton::clicked,this,[this] {saveTemplate();});
    connect(back,&QPushButton::clicked,this,[this] {m_views->setCurrentIndex(0);});
    auto *cancelAll=new QPushButton(tr("取消全部任务"),this);
    run->addWidget(cancelAll);
    connect(cancelAll,&QPushButton::clicked,this,[this] {
        for(int panel=0;panel<kPanelsPerRound;++panel)cancelFrame(panel);
        if(m_globalToken)m_globalToken->store(true);
    });
    connect(m_poseOnly,&QCheckBox::toggled,this,[this] {++m_revision;invalidateResult();updateActions();});

    connect(m_tolerance,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double) {parametersChanged();});
    connect(m_maximumGap,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double) {parametersChanged();});
    connect(m_method,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int) {parametersChanged();});
    connect(m_calibration,&QLineEdit::textChanged,this,[this] {
        for(const FramePtr &frame:std::as_const(m_frames))invalidateFrame(frame);
        ++m_revision;invalidateResult();
        log(tr("共用手眼 XML 已变化：全部帧完整数据、索引与选择失效"));
        updateActions();
    });
    connect(m_table,&QTableWidget::cellChanged,this,[this](int row,int column) {
        if(column!=2&&column!=3)return;
        if(row<0||row>=int(m_frames.size()))return;
        invalidateFrame(m_frames.at(row));
        ++m_revision;
        log(tr("第 %1 帧位姿已变化：该帧完整数据、索引与选择失效").arg(row+1));
        updateActions();
    });
    connect(m_table,&QTableWidget::cellClicked,this,[this](int row,int) {
        const FramePtr frame=(row>=0&&row<int(m_frames.size()))?m_frames.at(row):FramePtr{};
        const int panel=frame?panelOf(frame):-1;
        if(panel>=0)activatePanel(panel);
    });

    log(tr("六分屏批量选面：添加 PLY 与位姿后按列表顺序每轮加载最多 %1 帧。每格展示最多 %2 个确定性均匀抽取点，选面、候选快照、拼接与模板均使用完整点云。")
        .arg(kPanelsPerRound).arg(kDenseDisplayLimit));
    updateActions();
}

SelectedPlaneDialog::~SelectedPlaneDialog() {
    m_closing=true;
    cancelAndWait();
}

void SelectedPlaneDialog::closeEvent(QCloseEvent *event) {
    m_closing=true;
    cancelAndWait();
    QDialog::closeEvent(event);
}

void SelectedPlaneDialog::cancelAndWait() {
    for(Job &job:m_jobs)if(job.token)job.token->store(true);
    if(m_globalToken)m_globalToken->store(true);
    for(Job &job:m_jobs)if(job.watcher&&job.watcher->isRunning())job.watcher->waitForFinished();
    if(m_globalWatcher&&m_globalWatcher->isRunning())m_globalWatcher->waitForFinished();
    ++m_revision;
}

void SelectedPlaneDialog::log(const QString &text) {
    m_log->appendPlainText(text);
}

SelectedPlaneDialog::FramePtr SelectedPlaneDialog::frameAtPanel(int panel) const {
    if(panel<0||panel>=kPanelsPerRound||m_round<0)return {};
    const int row=firstRowOfRound(m_round)+panel;
    if(row<0||row>=lastRowOfRound(m_round,int(m_frames.size())))return {};
    return m_frames.at(row);
}

int SelectedPlaneDialog::rowOf(const FramePtr &frame) const {
    for(int row=0;row<int(m_frames.size());++row)if(m_frames.at(row)==frame)return row;
    return -1;
}

int SelectedPlaneDialog::panelOf(const FramePtr &frame) const {
    const int row=rowOf(frame);
    if(row<0||m_round<0)return -1;
    const int panel=row-firstRowOfRound(m_round);
    return (panel>=0&&panel<kPanelsPerRound)?panel:-1;
}

int SelectedPlaneDialog::jobSlotOfFrame(const FramePtr &frame) const {
    for(int slot=0;slot<kMaximumActiveJobs;++slot)if(m_jobs.at(size_t(slot)).frame==frame)return slot;
    return -1;
}

int SelectedPlaneDialog::activeJobCount() const {
    int count=0;
    for(const Job &job:m_jobs)if(job.frame)++count;
    return count;
}

QVector<int> SelectedPlaneDialog::roundRows(int round) const {
    QVector<int> rows;
    if(round<0)return rows;
    const int first=firstRowOfRound(round);
    for(int row=first;row<lastRowOfRound(round,int(m_frames.size()));++row)rows.push_back(row);
    return rows;
}

void SelectedPlaneDialog::appendFiles(const QStringList &files) {
    if(m_closing||m_globalBusy||activeJobCount()>0||files.isEmpty())return;
    {
        const QSignalBlocker block(m_table);
        for(const QString &path:files) {
            bool duplicate=false;
            for(int row=0;row<m_table->rowCount();++row)
                if(m_table->item(row,1)&&m_table->item(row,1)->text()==path)duplicate=true;
            if(duplicate)continue;
            const int row=m_table->rowCount();
            m_table->insertRow(row);
            for(int column=0;column<5;++column) {
                auto *item=new QTableWidgetItem;
                if(column==0) {item->setText(QString::number(row+1));item->setFlags(item->flags()&~Qt::ItemIsEditable);}
                else if(column==1) {item->setText(path);item->setFlags(item->flags()&~Qt::ItemIsEditable);}
                else if(column==4) {item->setText(tr("未加载"));item->setFlags(item->flags()&~Qt::ItemIsEditable);}
                m_table->setItem(row,column,item);
            }
            FramePtr frame=std::make_shared<Frame>();
            frame->id=m_nextId++;
            m_frames.push_back(frame);
        }
    }
    ++m_revision;
    invalidateResult();
    if(m_round<0)m_round=0;
    refreshPanelVisibility();
    log(tr("当前列表共 %1 帧，分 %2 轮；每轮最多 %3 帧").arg(m_frames.size()).arg(roundCountOf(int(m_frames.size()))).arg(kPanelsPerRound));
    updateActions();
}

void SelectedPlaneDialog::addFiles() {
    const QStringList files=QFileDialog::getOpenFileNames(this,tr("按扫描顺序添加 PLY"),{},QStringLiteral("PLY (*.ply)"));
    appendFiles(files);
}

void SelectedPlaneDialog::moveFrame(int offset) {
    if(m_closing||m_globalBusy||activeJobCount()>0)return;
    const int row=m_table->currentRow(),other=row+offset;
    if(row<0||other<0||other>=int(m_frames.size()))return;
    {
        const QSignalBlocker block(m_table);
        for(int column=0;column<5;++column) {
            auto *first=m_table->takeItem(row,column),*second=m_table->takeItem(other,column);
            m_table->setItem(row,column,second);
            m_table->setItem(other,column,first);
        }
        for(int index=0;index<m_table->rowCount();++index)
            if(m_table->item(index,0))m_table->item(index,0)->setText(QString::number(index+1));
    }
    m_frames.swapItemsAt(row,other);
    for(const FramePtr &frame:std::as_const(m_frames))releaseFrameData(frame);
    ++m_revision;
    invalidateResult();
    loadRound(m_round);
    log(tr("顺序已更新；本轮加载数据已释放，之前的输出与结果不再代表当前顺序"));
    updateActions();
}

void SelectedPlaneDialog::removeFrame() {
    if(m_closing||m_globalBusy||activeJobCount()>0)return;
    const int row=m_table->currentRow();
    if(row<0||row>=int(m_frames.size()))return;
    releaseFrameData(m_frames.at(row));
    {
        const QSignalBlocker block(m_table);
        m_table->removeRow(row);
        for(int index=0;index<m_table->rowCount();++index)
            if(m_table->item(index,0))m_table->item(index,0)->setText(QString::number(index+1));
    }
    m_frames.removeAt(row);
    ++m_revision;
    invalidateResult();
    const int count=int(m_frames.size());
    if(count==0) {releaseRound();m_round=-1;refreshPanelVisibility();updateActions();log(tr("列表已为空"));return;}
    m_round=qBound(0,m_round,roundCountOf(count)-1);
    loadRound(m_round);
    updateActions();
}

void SelectedPlaneDialog::releaseFrameData(const FramePtr &frame) {
    if(!frame)return;
    frame->loaded={};
    frame->index.reset();
    frame->displayLease.reset();
    frame->candidate={};
    frame->candidateIndices.clear();
    frame->seeds.clear();
    frame->hasCandidate=false;
    frame->highlightPending=false;
    frame->displayPending=false;
    frame->fitQueued=false;
    frame->loadQueued=false;
    frame->indexQueued=false;
    // Only the confirmed plane snapshot and the skip state survive a round
    // change; a confirmed frame therefore stays confirmed (not dirty).
    frame->skip=frame->committedSkip;
    frame->dirty=!frame->processed;
    frame->status.clear();
}

void SelectedPlaneDialog::releaseRound() {
    for(Job &job:m_jobs)if(job.token)job.token->store(true);
    for(const FramePtr &frame:std::as_const(m_frames)) {
        const int slot=jobSlotOfFrame(frame);
        if(slot>=0) {
            Job &job=m_jobs.at(size_t(slot));
            if(job.token)job.token->store(true);
            if(job.watcher) {
                disconnect(job.watcher,nullptr,this,nullptr);
                if(job.watcher->isRunning())job.watcher->waitForFinished();
                job.watcher->deleteLater();job.watcher=nullptr;
            }
            ++frame->revision;
            job.frame.reset();
            job.token.reset();
        }
        releaseFrameData(frame);
        frame->running=false;
    }
    for(Panel &panel:m_panels) {
        if(panel.canvas) {
            panel.canvas->setSelectedIndices({});
            panel.canvas->clearPlaneResult();
            panel.canvas->setCloud({});
        }
    }
    m_enlarged=-1;
    m_activePanel=-1;
}

void SelectedPlaneDialog::loadRound(int round) {
    if(m_closing||m_globalBusy)return;
    const int count=int(m_frames.size());
    if(count==0) {
        releaseRound();
        m_round=-1;
        m_roundLabel->setText(tr("尚未添加任何帧"));
        refreshPanelVisibility();
        updateActions();
        return;
    }
    round=qBound(0,round,roundCountOf(count)-1);
    bool drafts=false;
    for(int row:roundRows(m_round)) {
        const FramePtr &frame=m_frames.at(row);
        if(frame->hasCandidate||!frame->seeds.isEmpty()||frame->skip!=frame->committedSkip)drafts=true;
    }
    if(drafts&&QMessageBox::question(this,tr("离开当前草稿"),
        tr("当前轮还有未确认的候选、取点或跳过修改。放弃这些修改并重新加载目标轮次？已确认快照保留。"),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    releaseRound();
    m_round=round;
    m_views->setCurrentIndex(0);
    m_memoryBlocked=false;
    ++m_revision;
    const QVector<int> rows=roundRows(round);
    for(int row:rows) {
        const FramePtr &frame=m_frames.at(row);
        if(!frame->committedSkip) {frame->loadQueued=true;frame->loadTimer.start();frame->queueTimer.start();}
        else if(frame->confirmed.cloud.fullPoints.isEmpty()&&frame->committedSkip)frame->status=tr("已跳过（可撤销）");
    }
    for(int panel=0;panel<kPanelsPerRound;++panel)refreshPanel(panel);
    refreshPanelVisibility();
    m_roundLabel->setText(tr("第 %1 / %2 轮 · 本轮 %3 帧 · 列表共 %4 帧").arg(round+1).arg(roundCountOf(count)).arg(rows.size()).arg(count));
    log(tr("进入第 %1/%2 轮，共 %3 帧").arg(round+1).arg(roundCountOf(count)).arg(rows.size()));
    schedule();
    updateActions();
}

void SelectedPlaneDialog::retryFrame(int panel) {
    const FramePtr frame=frameAtPanel(panel);
    if(!frame||m_globalBusy)return;
    if(frame->running) {log(tr("第 %1 格任务处理中，请等待或取消本格任务").arg(panel+1));return;}
    m_memoryBlocked=false;
    if(frame->loaded.fullPoints.isEmpty()) {frame->loadQueued=true;log(tr("第 %1 格重新加载完整点云").arg(panel+1));}
    else if(!frame->index) {frame->indexQueued=true;log(tr("第 %1 格重新建立完整选面索引").arg(panel+1));}
    else {
        discardPreview(panel);releaseFrameData(frame);
        m_panels.at(size_t(panel)).canvas->setCloud({});
        frame->loadQueued=true;log(tr("第 %1 格重新加载完整点云并重建索引").arg(panel+1));
    }
    frame->skip=false;
    frame->queueTimer.start();
    if(frame->loadQueued)frame->loadTimer.start();
    schedule();
    refreshPanel(panel);
    updateActions();
}

void SelectedPlaneDialog::requestFit(const FramePtr &frame) {
    if(!frame)return;
    frame->fitQueued=true;
    frame->queueTimer.start();
    schedule();
}

void SelectedPlaneDialog::picked(int panel, int fullIndex) {
    if(m_closing||m_globalBusy)return;
    const FramePtr frame=frameAtPanel(panel);
    if(!frame)return;
    activatePanel(panel);
    if(frame->skip) {log(tr("第 %1 格已跳过；请先撤销跳过再选面").arg(panel+1));return;}
    if(frame->running||frame->fitQueued) {log(tr("第 %1 格任务处理中，请稍候或取消本格任务").arg(panel+1));return;}
    if(frame->loaded.fullPoints.isEmpty()) {log(tr("第 %1 格完整点云尚未就绪").arg(panel+1));return;}
    if(fullIndex<0||fullIndex>=int(frame->loaded.fullPoints.size()))return;
    const bool singleClick=m_method->currentIndex()==0;
    if(singleClick&&!frame->index) {log(tr("第 %1 格完整选面索引尚未就绪；可稍候或切换三点选面").arg(panel+1));return;}
    if(frame->hasCandidate)discardPreview(panel);
    if(frame->seeds.contains(fullIndex))return;
    frame->dirty=true;
    invalidateResult();
    frame->seeds.push_back(fullIndex);
    m_panels.at(size_t(panel)).canvas->setSelectedIndices(frame->seeds);
    if(!frame->clickTimer.isValid())frame->clickTimer.start();
    if(!singleClick&&frame->seeds.size()!=3) {
        log(tr("第 %1 格已取 %2 / 3 个控制点").arg(panel+1).arg(frame->seeds.size()));
        refreshPanel(panel);
        updateActions();
        return;
    }
    requestFit(frame);
    refreshPanel(panel);
    updateActions();
}

void SelectedPlaneDialog::discardPreview(int panel) {
    const FramePtr frame=frameAtPanel(panel);
    if(!frame)return;
    const int slot=jobSlotOfFrame(frame);
    if(slot>=0&&m_jobs.at(size_t(slot)).kind==SelectedPlaneTask::Fit&&m_jobs.at(size_t(slot)).token)
        m_jobs.at(size_t(slot)).token->store(true);
    ++frame->revision;
    frame->hasCandidate=false;
    frame->candidate={};
    frame->candidateIndices.clear();
    frame->seeds.clear();
    frame->fitQueued=false;
    frame->highlightPending=false;
    Panel &ui=m_panels.at(size_t(panel));
    ui.canvas->setSelectedIndices({});
    if(!frame->confirmed.cloud.fullPoints.isEmpty())ui.canvas->setExtractedPlane(frame->confirmedIndices);
    else ui.canvas->clearPlaneResult();
    frame->dirty=!frame->processed||frame->skip!=frame->committedSkip;
    frame->status=frame->confirmed.cloud.fullPoints.isEmpty()?tr("尚无候选平面"):tr("已确认选择保留，可重新选面");
    refreshPanel(panel);
    updateActions();
}

void SelectedPlaneDialog::toggleSkip(int panel) {
    const FramePtr frame=frameAtPanel(panel);
    if(!frame||m_globalBusy)return;
    if(frame->running) {log(tr("第 %1 格任务处理中，不能跳过；请先取消或等待结束").arg(panel+1));return;}
    frame->skip=!frame->skip;
    frame->highlightPending=false;
    frame->dirty=true;
    m_memoryBlocked=false;
    Panel &ui=m_panels.at(size_t(panel));
    if(frame->skip) {
        frame->hasCandidate=false;
        frame->candidate={};
        frame->candidateIndices.clear();
        frame->seeds.clear();
        frame->fitQueued=false;
        frame->loadQueued=false;
        frame->indexQueued=false;
        ui.canvas->setSelectedIndices({});
        ui.canvas->clearPlaneResult();
        frame->status=tr("本轮确认时提交跳过");
        log(tr("第 %1 格标记跳过；旧确认快照保留至本轮确认，可撤销").arg(panel+1));
    } else {
        frame->loadQueued=frame->loaded.fullPoints.isEmpty();
        frame->indexQueued=!frame->loaded.fullPoints.isEmpty()&&!frame->index;
        frame->queueTimer.start();if(frame->loadQueued)frame->loadTimer.start();
        if(!frame->confirmed.cloud.fullPoints.isEmpty()) {
            ui.canvas->setExtractedPlane(frame->confirmedIndices);
            frame->status=tr("跳过已撤销；原确认保留，可重新选面");
        } else {
            frame->status=tr("跳过已撤销；请检查或重新选面");
        }
        log(tr("第 %1 格跳过已撤销").arg(panel+1));
    }
    invalidateResult();
    refreshPanel(panel);
    schedule();
    updateActions();
}

void SelectedPlaneDialog::cancelFrame(int panel) {
    const FramePtr frame=frameAtPanel(panel);
    if(!frame)return;
    const int slot=jobSlotOfFrame(frame);
    frame->loadQueued=false;frame->indexQueued=false;frame->fitQueued=false;
    frame->seeds.clear();
    m_panels.at(size_t(panel)).canvas->setSelectedIndices({});
    if(!frame->hasCandidate)frame->dirty=!frame->processed||frame->skip!=frame->committedSkip;
    if(slot<0) {updateActions();return;}
    ++frame->revision;
    if(m_jobs.at(size_t(slot)).token)m_jobs.at(size_t(slot)).token->store(true);
    frame->status=tr("正在取消本格任务…");
    log(tr("第 %1 格已请求取消；其他格不受影响").arg(panel+1));
    refreshPanel(panel);
    updateActions();
}

void SelectedPlaneDialog::confirmRound() {
    if(!roundReady())return;
    const QVector<int> rows=roundRows(m_round);
    int committed=0,skipped=0;
    for(int row:rows) {
        const FramePtr &frame=m_frames.at(row);
        if(frame->skip) {
            frame->committedSkip=true;
            frame->processed=true;
            frame->confirmed={};frame->confirmedIndices.clear();
            frame->hasCandidate=false;
            frame->candidate={};
            frame->candidateIndices.clear();
            frame->seeds.clear();
            frame->fitQueued=false;
            frame->dirty=false;
            ++skipped;
            releaseFrameData(frame);
            const int panel=panelOf(frame);
            if(panel>=0)m_panels.at(size_t(panel)).canvas->setCloud({});
            frame->status=tr("已确认跳过；撤销后重新加载并选面");
            continue;
        }
        frame->committedSkip=false;
        if(frame->hasCandidate) {
            frame->confirmed=frame->candidate;
            frame->confirmedIndices=frame->candidateIndices;
            frame->hasCandidate=false;
            frame->candidate={};
            frame->candidateIndices.clear();
            frame->seeds.clear();
            frame->processed=true;
            ++committed;
        } else if(!frame->confirmed.cloud.fullPoints.isEmpty()) {
            frame->processed=true;
        }
        frame->dirty=false;
    }
    invalidateResult();
    const int rounds=roundCountOf(int(m_frames.size()));
    log(tr("第 %1/%2 轮已确认：新提交 %3 帧，跳过 %4 帧").arg(m_round+1).arg(rounds).arg(committed).arg(skipped));
    if(m_round+1<rounds)loadRound(m_round+1);
    else {
        QStringList pending;
        for(int index=0;index<int(m_frames.size());++index) {
            const FramePtr &frame=m_frames.at(index);
            if(frame->committedSkip||frame->skip)continue;
            if(frame->dirty||frame->confirmed.cloud.fullPoints.isEmpty())
                pending.append(QString::number(index+1));
        }
        if(pending.isEmpty()&&stitchReady())
            log(tr("全部轮次已确认，无未确认修改：可以点击“拼接所选平面”。"));
        else if(pending.isEmpty())log(tr("本轮已确认；至少保留两帧未跳过的已确认平面才能拼接。"));
        else
            log(tr("本轮已确认，但仍有未确认或已失效的帧（列表序号：%1）；请用“返回上一轮”回到对应轮处理后再次确认本轮，之后才能拼接。")
                .arg(pending.isEmpty()?tr("无"):pending.join(QStringLiteral(", "))));
        updateActions();
    }
}

bool SelectedPlaneDialog::roundReady() const {
    if(m_closing||m_globalBusy||m_round<0)return false;
    const QVector<int> rows=roundRows(m_round);
    if(rows.isEmpty())return false;
    for(int row:rows) {
        const FramePtr &frame=m_frames.at(row);
        if(frame->running||frame->loadQueued||frame->indexQueued||frame->fitQueued||!frame->seeds.isEmpty())return false;
        if(frame->hasCandidate||frame->skip)continue;
        if(!frame->confirmed.cloud.fullPoints.isEmpty())continue;
        return false;
    }
    return true;
}

bool SelectedPlaneDialog::stitchReady() const {
    if(m_closing||m_globalBusy||m_round<0)return false;
    const int count=int(m_frames.size());
    if(count<2)return false;
    int selected=0;
    for(const FramePtr &frame:std::as_const(m_frames)) {
        if(frame->running||frame->loadQueued||frame->indexQueued||frame->fitQueued||!frame->seeds.isEmpty())return false;
        if(frame->hasCandidate||frame->dirty||!frame->processed)return false;
        if(frame->committedSkip||frame->skip)continue;
        if(frame->confirmed.cloud.fullPoints.isEmpty())return false;
        ++selected;
    }
    return selected>=2;
}

void SelectedPlaneDialog::parametersChanged() {
    if(m_closing)return;
    ++m_revision;
    for(int row:roundRows(m_round)) {
        const FramePtr &frame=m_frames.at(row);
        const int panel=panelOf(frame);
        const int slot=jobSlotOfFrame(frame);
        if(slot>=0&&m_jobs.at(size_t(slot)).kind==SelectedPlaneTask::Fit) {
            if(m_jobs.at(size_t(slot)).token)m_jobs.at(size_t(slot)).token->store(true);
            ++frame->revision; // A running fit can no longer publish a valid candidate.
        }
        if(frame->hasCandidate||frame->fitQueued||!frame->seeds.isEmpty()||
           (slot>=0&&m_jobs.at(size_t(slot)).kind==SelectedPlaneTask::Fit)) {
            frame->hasCandidate=false;
            frame->candidate={};
            frame->candidateIndices.clear();
            frame->seeds.clear();
            frame->fitQueued=false;
            frame->highlightPending=false;
            if(panel>=0) {
                m_panels.at(size_t(panel)).canvas->setSelectedIndices({});
                if(!frame->confirmed.cloud.fullPoints.isEmpty())m_panels.at(size_t(panel)).canvas->setExtractedPlane(frame->confirmedIndices);
                else m_panels.at(size_t(panel)).canvas->clearPlaneResult();
            }
        }
        frame->dirty=!frame->processed||frame->skip!=frame->committedSkip;
        if(panel>=0)refreshPanel(panel);
    }
    invalidateResult();
    log(tr("选面参数已变化：当前轮未确认候选与排队识别已取消，已确认快照保留"));
    schedule();
    updateActions();
}

void SelectedPlaneDialog::invalidateFrame(const FramePtr &frame) {
    if(!frame)return;
    ++frame->revision;
    const int slot=jobSlotOfFrame(frame);
    if(slot>=0&&m_jobs.at(size_t(slot)).token)m_jobs.at(size_t(slot)).token->store(true);
    frame->loaded={};
    frame->index.reset();
    frame->displayLease.reset();
    frame->candidate={};
    frame->candidateIndices.clear();
    frame->confirmed={};
    frame->confirmedIndices.clear();
    frame->seeds.clear();
    frame->hasCandidate=false;
    frame->committedSkip=false;
    frame->skip=false;
    frame->processed=false;
    frame->dirty=true;
    frame->highlightPending=false;
    frame->displayPending=false;
    frame->fitQueued=false;
    frame->loadQueued=false;
    frame->indexQueued=false;
    frame->status=tr("数据已失效，需要重新加载");
    const int panel=panelOf(frame);
    if(panel>=0) {
        Panel &ui=m_panels.at(size_t(panel));
        ui.canvas->setSelectedIndices({});
        ui.canvas->clearPlaneResult();
        ui.canvas->setCloud({});
        refreshPanel(panel);
    }
    const int row=rowOf(frame);
    if(row>=0&&m_table->item(row,4)) {
        const QSignalBlocker block(m_table);
        m_table->item(row,4)->setText(tr("数据已失效"));
    }
    invalidateResult();
}

void SelectedPlaneDialog::invalidateResult() {
    const bool hadResult=m_resultValid;
    m_resultValid=false;
    if(!m_result.points.isEmpty())m_result={};
    if(m_resultCanvas)m_resultCanvas->setCloud({});
    if(m_views&&m_views->currentIndex()!=0)m_views->setCurrentIndex(0);
    if(hadResult)log(tr("选面输入已变化：之前的拼接结果已过期，不能再按当前结果保存模板；已保存的模板版本保留"));
}

void SelectedPlaneDialog::enlarge(int panel) {
    if(!frameAtPanel(panel))return;
    m_enlarged=(m_enlarged==panel)?-1:panel;
    activatePanel(panel);
    refreshPanelVisibility();
    refreshPanel(panel);
}

void SelectedPlaneDialog::refreshPanelVisibility() {
    for(int index=0;index<kPanelsPerRound;++index) {
        Panel &panel=m_panels.at(size_t(index));
        const bool bound=frameAtPanel(index)!=nullptr;
        m_grid->removeWidget(panel.widget);
        if(m_enlarged==index)m_grid->addWidget(panel.widget,0,0,2,3);
        else m_grid->addWidget(panel.widget,index/3,index%3);
        panel.widget->setVisible(bound&&(m_enlarged<0||m_enlarged==index));
    }
}

void SelectedPlaneDialog::refreshPanel(int panel) {
    if(panel<0||panel>=kPanelsPerRound)return;
    Panel &ui=m_panels.at(size_t(panel));
    const FramePtr frame=frameAtPanel(panel);
    if(!frame) {
        ui.label->setText(QString());
        ui.skip->setEnabled(false);
        ui.retry->setEnabled(false);
        ui.discard->setEnabled(false);
        ui.cancel->setEnabled(false);
        ui.canvas->setEnabled(false);
        return;
    }
    ui.canvas->setEnabled(true);
    const int row=rowOf(frame);
    const QString path=(row>=0&&m_table->item(row,1))?m_table->item(row,1)->text():frame->loaded.sourceFile;
    const qsizetype fullCount=frame->loaded.fullPoints.size();
    const qsizetype displayCount=qMin<qsizetype>(fullCount,kDenseDisplayLimit);
    QString status=frame->status;
    if(frame->skip)status=tr("已跳过（本轮确认时提交，可撤销）");
    else if(frame->running)status=tr("处理中…");
    else if(frame->hasCandidate)status=tr("候选 %1 点 · RMS %2 mm（待确认）").arg(frame->candidateIndices.size()).arg(frame->candidate.rmsMm,0,'g',5);
    else if(!frame->confirmed.cloud.fullPoints.isEmpty()&&!frame->dirty)
        status=tr("已确认 %1 点 · RMS %2 mm").arg(frame->confirmedIndices.size()).arg(frame->confirmed.rmsMm,0,'g',5);
    else if(!status.isEmpty()) {}
    else if(frame->loadQueued)status=tr("排队等待加载完整点云…");
    else if(fullCount>0&&!frame->index)status=tr("展示就绪，正在后台建立完整选面索引…");
    else if(fullCount>0)status=tr("可选面（%1）").arg(m_method->currentIndex()==0?tr("单击"):tr("三点，已取 %1/3").arg(frame->seeds.size()));
    else status=tr("未加载");
    ui.label->setText(tr("格 %1 · 列表序号 %2 · %3\n展示 %4 / 完整 %5\n%6")
        .arg(panel+1).arg(row>=0?row+1:0).arg(baseNameOf(path)).arg(displayCount).arg(fullCount).arg(status));
    ui.skip->setText(frame->skip?tr("撤销跳过"):tr("跳过本帧"));
    ui.skip->setEnabled(!frame->running&&!m_globalBusy);
    ui.retry->setEnabled(!frame->running&&!m_globalBusy);
    ui.discard->setEnabled((frame->hasCandidate||!frame->seeds.isEmpty())&&!frame->running&&!m_globalBusy);
    ui.cancel->setEnabled((frame->running||frame->loadQueued||frame->indexQueued||frame->fitQueued)&&!m_globalBusy);
    ui.canvas->setSelectionMode(!m_globalBusy&&!frame->running&&!frame->skip&&m_method->currentIndex()==1);
    if(row>=0&&m_table->item(row,4)) {
        const QSignalBlocker block(m_table);
        m_table->item(row,4)->setText(status.left(60));
    }
}

bool SelectedPlaneDialog::eventFilter(QObject *object, QEvent *event) {
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Wheel) {
        for(int panel=0;panel<kPanelsPerRound;++panel)
            if(object==m_panels.at(size_t(panel)).canvas) {activatePanel(panel);break;}
    }
    return QDialog::eventFilter(object,event);
}

void SelectedPlaneDialog::activatePanel(int panel) {
    if(!frameAtPanel(panel))return;
    m_activePanel=panel;
    for(int index=0;index<kPanelsPerRound;++index) {
        auto *widget=m_panels.at(size_t(index)).widget;
        widget->setObjectName(QStringLiteral("selectedPlanePanel%1").arg(index));
        widget->setStyleSheet(QStringLiteral("QWidget#selectedPlanePanel%1 { border: 2px solid %2; }")
            .arg(index).arg(index==panel?QStringLiteral("#4488cc"):QStringLiteral("transparent")));
    }
    schedule();
}

SelectedPlaneDialog::FramePtr SelectedPlaneDialog::nextQueuedFrame(SelectedPlaneTask::Kind *kind) const {
    const auto active=frameAtPanel(m_activePanel);
    const auto eligible=[](const FramePtr &frame) {return frame&&!frame->running&&!frame->skip;};
    if(eligible(active)&&active->fitQueued&&!active->seeds.isEmpty()) {*kind=SelectedPlaneTask::Fit;return active;}
    const auto rows=roundRows(m_round);
    for(int row:rows) {
        const auto &frame=m_frames.at(row);
        if(eligible(frame)&&frame->fitQueued&&!frame->seeds.isEmpty()) {*kind=SelectedPlaneTask::Fit;return frame;}
    }
    if(eligible(active)) {
        if(active->loadQueued) {*kind=SelectedPlaneTask::LoadDisplay;return active;}
        if(active->indexQueued&&!active->loaded.fullPoints.isEmpty()&&!active->index) {*kind=SelectedPlaneTask::PrepareIndex;return active;}
    }
    for(int row:rows) {
        const auto &frame=m_frames.at(row);
        if(eligible(frame)&&frame->loadQueued) {*kind=SelectedPlaneTask::LoadDisplay;return frame;}
    }
    for(int row:rows) {
        const auto &frame=m_frames.at(row);
        if(eligible(frame)&&frame->indexQueued&&!frame->loaded.fullPoints.isEmpty()&&!frame->index) {*kind=SelectedPlaneTask::PrepareIndex;return frame;}
    }
    return {};
}

void SelectedPlaneDialog::schedule() {
    if(m_closing||m_globalBusy||m_round<0) {
        updateActions();
        return;
    }
    if(m_memoryBlocked) {
        // Memory pressure: no new heavy task until the user retries a frame or
        // skips it. Completed selections and formal data stay untouched.
        updateActions();
        return;
    }
    while(activeJobCount()<kMaximumActiveJobs) {
        // A running four-worker job keeps its quota until completion. Queue a
        // newly clicked frame rather than oversubscribing it with a second job.
        bool exclusive=false;
        for(const Job &running:m_jobs)if(running.frame&&running.workers>2)exclusive=true;
        if(exclusive)break;
        SelectedPlaneTask::Kind kind=SelectedPlaneTask::LoadDisplay;
        const FramePtr target=nextQueuedFrame(&kind);
        const QVector<int> rows=roundRows(m_round);
        if(!target)break;
        int slot=-1;
        for(int index=0;index<kMaximumActiveJobs;++index)if(!m_jobs.at(size_t(index)).frame) {slot=index;break;}
        if(slot<0)break;
        // Worker quota: four workers only while this is the single pending heavy
        // task; as soon as another task runs or is about to run, both stay at two.
        int pendingOthers=0;
        for(int row:rows) {
            const FramePtr &frame=m_frames.at(row);
            if(frame==target||frame->running||frame->skip)continue;
            if(frame->loadQueued||frame->indexQueued||frame->fitQueued)++pendingOthers;
        }
        const bool singleTask=activeJobCount()==0&&pendingOthers==0;
        const int workers=singleTask?kWorkersWithOneJob:kWorkersWithTwoJobs;
        startJob(m_jobs.at(size_t(slot)),target,kind,workers);
    }
    updateActions();
}

void SelectedPlaneDialog::startJob(Job &job, const FramePtr &frame, SelectedPlaneTask::Kind kind, int workers) {
    const int row=rowOf(frame);
    const QString path=(row>=0&&m_table->item(row,1))?m_table->item(row,1)->text():frame->loaded.sourceFile;
    const QString calibration=m_calibration->text();
    pointcloud::RobotPose start,end;
    if(kind==SelectedPlaneTask::LoadDisplay) {
        const QString startText=(row>=0&&m_table->item(row,2))?m_table->item(row,2)->text():QString();
        const QString endText=(row>=0&&m_table->item(row,3))?m_table->item(row,3)->text():QString();
        if(!parsePose(startText,&start)||!parsePose(endText,&end)) {
            frame->loadQueued=false;
            frame->status=tr("Start/End 必须各包含六个有限数：[X Y Z A B C]");
            log(tr("第 %1 帧位姿无效：Start/End 必须各包含六个有限数").arg(row>=0?row+1:0));
            refreshPanel(panelOf(frame));
            return;
        }
        if(calibration.trimmed().isEmpty()) {
            frame->loadQueued=false;
            frame->status=tr("请先选择共用手眼 XML");
            log(tr("请先选择共用手眼 XML 再加载帧"));
            refreshPanel(panelOf(frame));
            return;
        }
        frame->loadQueued=false;
        frame->status=tr("正在读取并转换完整点云…");
    } else if(kind==SelectedPlaneTask::PrepareIndex) {
        frame->indexQueued=false;
        frame->status=tr("正在建立完整选面索引…");
    } else {
        frame->fitQueued=false;
        frame->status=tr("正在完整点云上识别平面…");
    }

    auto token=std::make_shared<std::atomic_bool>(false);
    job.frame=frame;
    job.token=token;
    job.frameId=frame->id;
    job.revision=frame->revision;
    job.queueMs=frame->queueTimer.isValid()?frame->queueTimer.elapsed():0;
    job.queuedAt=frame->queueTimer;
    if(!job.queuedAt.isValid())job.queuedAt.start();
    job.kind=kind;
    job.workers=qBound(1,workers,kWorkersWithOneJob);
    job.watcher=new QFutureWatcher<SelectedPlaneTask>(this);
    QFutureWatcher<SelectedPlaneTask> *watcher=job.watcher;
    connect(watcher,&QFutureWatcher<SelectedPlaneTask>::finished,this,[this,watcher] {
        for(int index=0;index<kMaximumActiveJobs;++index)
            if(m_jobs.at(size_t(index)).watcher==watcher) {finishJob(index);return;}
        watcher->deleteLater();
    });
    frame->running=true;
    if(kind==SelectedPlaneTask::LoadDisplay&&!frame->loadTimer.isValid())frame->loadTimer.start();
    log(tr("列表第 %1 帧，任务 %2：派发前排队 %3 ms，计算 worker 上限 %4")
        .arg(row+1).arg(kind==SelectedPlaneTask::LoadDisplay?tr("读取/转换/展示"):kind==SelectedPlaneTask::PrepareIndex?tr("完整索引"):tr("完整选面"))
        .arg(job.queueMs).arg(job.workers));
    refreshPanel(panelOf(frame));

    if(kind==SelectedPlaneTask::LoadDisplay) {
        const auto budget=m_budget;
        const int workerCount=job.workers;
        const auto work=[path,calibration,start,end,token,budget,workerCount] {
            return pcv::ui::loadSelectedPlaneFrameV2(path,calibration,start,end,token,workerCount,budget);
        };
        watcher->setFuture(QtConcurrent::run([work] {return measureWorker(work);}));
        return;
    }
    if(kind==SelectedPlaneTask::PrepareIndex) {
        // One extra shared copy: the worker only reads it through const views.
        const QVector<pointcloud::Point3D> points=frame->loaded.fullPoints;
        const auto budget=m_budget;
        const int workerCount=job.workers;
        const auto work=[points,token,budget,workerCount] {
            return pcv::ui::prepareSelectedPlaneIndexV2(points,token,workerCount,budget);
        };
        watcher->setFuture(QtConcurrent::run([work] {return measureWorker(work);}));
        return;
    }
    const pointcloud::RobotBaseFrame source=frame->loaded;
    const QVector<int> seeds=frame->seeds;
    const float tolerance=float(m_tolerance->value());
    const float maximumGap=float(m_maximumGap->value());
    const bool singleClick=m_method->currentIndex()==0;
    const auto spatialIndex=frame->index;
    const auto budget=m_budget;
    const int workerCount=job.workers;
    const auto work=[source,seeds,tolerance,maximumGap,singleClick,spatialIndex,token,budget,workerCount] {
        return fitFullPlane(source,seeds,tolerance,maximumGap,singleClick,spatialIndex,token,workerCount,budget);
    };
    watcher->setFuture(QtConcurrent::run([work] {return measureWorker(work);}));
}

void SelectedPlaneDialog::finishJob(int slot) {
    if(slot<0||slot>=kMaximumActiveJobs)return;
    Job &job=m_jobs.at(size_t(slot));
    QFutureWatcher<SelectedPlaneTask> *watcher=job.watcher;
    if(m_closing) {
        if(watcher)watcher->deleteLater();
        job.watcher=nullptr;
        if(job.frame)job.frame->running=false;
        job.frame.reset();
        job.token.reset();
        return;
    }
    if(!watcher||!job.frame) {
        if(watcher) {job.watcher=nullptr;watcher->deleteLater();}
        job.frame.reset();
        job.token.reset();
        return;
    }
    const FramePtr frame=job.frame;
    const SelectedPlaneTask task=watcher->result();
    const qint64 queueMs=job.queuedAt.isValid()&&task.startedAt.isValid()
        ?qMax<qint64>(0,job.queuedAt.msecsTo(task.startedAt)):job.queueMs;
    const bool stale=job.frameId!=frame->id||job.revision!=frame->revision
        ||rowOf(frame)<0||panelOf(frame)<0||task.kind!=job.kind
        ||!job.token||job.token->load();
    watcher->deleteLater();
    job.watcher=nullptr;
    job.frame.reset();
    job.token.reset();
    frame->running=false;
    const int panel=panelOf(frame);
    const QString where=panel>=0?tr("第 %1 格").arg(panel+1):tr("非本轮帧");
    // Retire the job, but never apply an old payload to replacement requests.
    if(stale) {
        log(tr("%1已丢弃过期或取消的任务结果；当前候选、排队请求和内存状态保持").arg(where));
        refreshPanel(panel);schedule();updateActions();return;
    }
    const qint64 publicationWaitMs=task.completedAt.isValid()?task.completedAt.elapsed():0;
    log(tr("%1任务计时：实际排队 %2 ms，worker %3 ms，GUI发布等待 %4 ms")
        .arg(where).arg(queueMs).arg(task.workerMs).arg(publicationWaitMs));
    if(task.outOfMemory) {
        m_memoryBlocked=true;
        log(tr("%1内存不足：已停止新增后台任务；请重试该格或标记跳过，已完成的选择保留，正式数据不减少").arg(where));
    }

    if(task.kind==SelectedPlaneTask::LoadDisplay) {
        frame->loadQueued=false;
        if(task.cancelled||stale) {
            frame->status=stale?tr("已丢弃过期加载结果"):tr("已取消加载，原状态保留");
            log(tr("%1加载%2").arg(where).arg(stale?tr("结果已过期并丢弃"):tr("已取消")));
        } else if(!task.ok) {
            frame->status=task.outOfMemory?tr("内存不足，帧未加载"):tr("加载失败：%1").arg(task.error);
            log(tr("%1加载失败：%2；可重试或跳过本格").arg(where).arg(task.error));
        } else {
            frame->index.reset();frame->loaded=task.loaded;
            if(panel>=0)publishDisplay(panel,frame,task);
            frame->indexQueued=!frame->skip;frame->queueTimer.start();
            frame->status=tr("展示就绪，正在后台建立完整选面索引…");
            if(panel>=0&&!frame->confirmed.cloud.fullPoints.isEmpty())
                m_panels.at(size_t(panel)).canvas->setExtractedPlane(frame->confirmedIndices);
            log(tr("%1完整点云就绪：完整 %2 点，展示 %3 点；PLY读取 %4 ms（指纹 %5 / 缓存读 %6 / 缓存写 %7%8），完整转换 %9 ms，展示抽样 %10 ms")
                .arg(where).arg(frame->loaded.fullPoints.size()).arg(task.displayPoints.size())
                .arg(task.plyReadMs).arg(task.fingerprintMs).arg(task.cacheReadMs).arg(task.cacheWriteMs)
                .arg(task.usedCache?tr("，命中读取缓存"):QString())
                .arg(task.transformMs).arg(task.displaySamplingMs));
            log(tr("%1源PLY解析 %2 ms；转换归并 %3 ms，实际转换worker %4")
                .arg(where).arg(task.plyParseMs).arg(task.transformMergeMs,0,'f',3).arg(task.transformWorkers));
        }
    } else if(task.kind==SelectedPlaneTask::PrepareIndex) {
        frame->indexQueued=false;
        if(task.cancelled||stale) {
            frame->status=tr("完整选面索引未就绪；可重试或使用三点选面");
            log(tr("%1索引准备%2").arg(where).arg(stale?tr("结果已过期并丢弃"):tr("已取消")));
        } else if(!task.ok) {
            frame->status=task.outOfMemory?tr("内存不足，索引未建立"):tr("索引失败：%1；可使用三点选面").arg(task.error);
            log(tr("%1完整选面索引失败：%2；可重试或切换三点选面").arg(where).arg(task.error));
        } else {
            frame->index=task.index;
            frame->status=tr("可选面（单击）");
            log(tr("%1从加载开始至可选面：%2 ms；当前优化存储预留 %3 MiB（上限256 MiB，不含完整业务数据及原有索引）")
                .arg(where).arg(frame->loadTimer.isValid()?frame->loadTimer.elapsed():0)
                .arg(double(m_budget->used())/(1024*1024),0,'f',1));
            log(tr("%1完整选面索引就绪：%2 ms（网格 %3 ms、法向 %4 ms%5）")
                .arg(where).arg(task.preparationMs).arg(task.gridMs).arg(task.normalsMs)
                .arg(task.optimizationBytes?tr("，point→bucket 缓存 %1 MiB").arg(double(task.optimizationBytes)/(1024.0*1024.0),0,'f',1):QString()));
        }
    } else if(task.kind==SelectedPlaneTask::Fit) {
        frame->fitQueued=false;frame->seeds.clear();
        if(panel>=0)m_panels.at(size_t(panel)).canvas->setSelectedIndices({});
        if(task.cancelled||stale) {
            frame->dirty=!frame->processed||frame->skip!=frame->committedSkip;
            frame->status=tr("候选已取消，已确认选择保留");
            log(tr("%1候选%2，已确认快照保留").arg(where).arg(stale?tr("结果已过期并丢弃"):tr("已取消")));
        } else if(!task.ok) {
            frame->dirty=!frame->processed||frame->skip!=frame->committedSkip;
            frame->status=task.outOfMemory?tr("内存不足，保留原选择"):tr("识别失败：%1").arg(task.error);
            log(tr("%1平面识别失败：%2").arg(where).arg(task.error));
        } else {
            frame->candidate=task.selected;
            frame->candidateIndices=task.planeIndices;
            frame->hasCandidate=true;frame->dirty=true;
            frame->seeds.clear();
            if(panel>=0) {
                Panel &ui=m_panels.at(size_t(panel));
                ui.canvas->setSelectedIndices({});
                ui.canvas->setExtractedPlane(task.planeIndices);
            }
            frame->highlightPending=true;
            frame->status=tr("候选 %1 点 · RMS %2 mm（待确认）").arg(task.planeIndices.size()).arg(task.selected.rmsMm,0,'g',5);
            invalidateResult();
            log(tr("%1完整候选就绪：%2 点，RMS %3 mm；点击至候选 %4 ms（算法及快照 %5 ms，实际排队 %6 ms）。再次点击可替换候选，或点击“取消候选”。")
                .arg(where).arg(task.planeIndices.size()).arg(task.selected.rmsMm,0,'g',5)
                .arg(frame->clickTimer.isValid()?frame->clickTimer.elapsed():0)
                .arg(task.selected.selectionMs)
                .arg(queueMs));
            log(tr("%1投影 %2 ms，普通连通 %3 ms，跨带连接 %4 ms，完整快照 %5 ms")
                .arg(where).arg(task.projectionMs,0,'f',3).arg(task.normalConnectivityMs,0,'f',3)
                .arg(task.gapMs,0,'f',3).arg(task.snapshotMs,0,'f',3));
            const auto &timings=task.selected.selectionParameters;
            log(tr("%1局部模型 %2 ms，完整分类 %3 ms，PCA %4 ms；迭代 %5，跨带合并 %6 区域")
                .arg(where).arg(timings.value("local_fit_ms").toDouble(),0,'f',3)
                .arg(timings.value("classification_ms").toDouble(),0,'f',3)
                .arg(timings.value("pca_ms").toDouble(),0,'f',3)
                .arg(timings.value("refinement_iterations").toInt()).arg(timings.value("merged_region_count").toInt()));
        }
    }
    refreshPanel(panel);
    schedule();
    updateActions();
}

void SelectedPlaneDialog::publishDisplay(int panel, const FramePtr &frame, const SelectedPlaneTask &task) {
    if(panel<0||panel>=kPanelsPerRound)return;
    Panel &ui=m_panels.at(size_t(panel));
    frame->displayLease=task.displayLease;
    ui.canvas->setDisplayCloud(task.displayPoints,task.displayToFullIndices,frame->loaded.fullPoints.size());
    frame->displayPending=true;
}

void SelectedPlaneDialog::updateActions() {
    const bool busy=m_globalBusy||activeJobCount()>0;
    for(QPushButton *control:std::as_const(m_editControls))control->setEnabled(!busy);
    const QSignalBlocker tableSignals(m_table);
    for(int row=0;row<m_table->rowCount();++row)for(int column:{2,3}) {
        auto *item=m_table->item(row,column);
        if(!item)continue;
        Qt::ItemFlags flags=item->flags();
        if(busy)flags&=~Qt::ItemIsEditable;
        else flags|=Qt::ItemIsEditable;
        item->setFlags(flags);
    }
    m_confirm->setEnabled(roundReady());
    m_previous->setEnabled(m_round>0&&!m_globalBusy);
    m_loadRound->setEnabled(!busy&&!m_frames.isEmpty());
    m_calibration->setEnabled(!busy);
    m_poseOnly->setEnabled(!m_globalBusy);
    m_method->setEnabled(!m_globalBusy);
    m_tolerance->setEnabled(!m_globalBusy);
    m_start->setEnabled(stitchReady());
    m_saveTemplate->setEnabled(m_resultValid&&!m_result.points.isEmpty()&&!busy);
    m_maximumGap->setEnabled(!m_globalBusy&&m_method->currentIndex()==0);
    for(int panel=0;panel<kPanelsPerRound;++panel)refreshPanel(panel);
    refreshPanelVisibility();
    const int rounds=roundCountOf(int(m_frames.size()));
    if(m_round>=0&&rounds>0) {
        int selected=0;
        for(const FramePtr &frame:std::as_const(m_frames))
            if(!frame->committedSkip&&!frame->skip&&!frame->confirmed.cloud.fullPoints.isEmpty())++selected;
        m_roundLabel->setText(tr("第 %1 / %2 轮 · 本轮 %3 帧 · 列表共 %4 帧 · 已确认未跳过 %5 帧%6%7")
            .arg(m_round+1).arg(rounds).arg(roundRows(m_round).size()).arg(m_frames.size()).arg(selected)
            .arg(m_globalBusy?tr(" · 后台任务处理中"):QString())
            .arg(m_memoryBlocked?tr(" · 内存不足已暂停新增任务（可重试或跳过）"):QString()));
    }
}

void SelectedPlaneDialog::stitch() {
    if(!stitchReady())return;
    const QString outputRoot=m_output->text().trimmed();
    if(outputRoot.isEmpty()) {log(tr("请选择输出根目录"));return;}
    const QString calibration=m_calibration->text().trimmed();
    if(calibration.isEmpty()) {log(tr("请选择共用手眼 XML"));return;}
    pcv::stitching::SelectedPlaneStitchRequest request;
    QStringList skipped;
    for(int row=0;row<int(m_frames.size());++row) {
        const FramePtr &frame=m_frames.at(row);
        const QString name=baseNameOf(m_table->item(row,1)?m_table->item(row,1)->text():frame->loaded.sourceFile);
        if(frame->committedSkip||frame->skip) {skipped.append(name);continue;}
        if(frame->confirmed.cloud.fullPoints.isEmpty()) {log(tr("第 %1 帧尚未确认平面，无法拼接").arg(row+1));return;}
        request.frames.push_back(frame->confirmed);
    }
    if(request.frames.size()<2) {log(tr("至少需要两帧未跳过的已确认平面"));return;}
    request.calibrationPath=calibration;
    request.options.fineEnabled=!m_poseOnly->isChecked();
    request.outputDirectory=QDir(outputRoot).filePath(QStringLiteral("selected_plane_v1_%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
    {
        int version=2;
        const QString base=request.outputDirectory;
        while(QFileInfo::exists(request.outputDirectory))
            request.outputDirectory=base+QStringLiteral("_%1").arg(version++);
    }
    m_globalBusy=true;
    m_globalToken=std::make_shared<std::atomic_bool>(false);
    const auto token=m_globalToken;
    request.isCancelled=[token] {return token->load();};
    log(tr("开始拼接 %1 帧完整确认快照（精配准 %2）；跳过 %3 帧%4")
        .arg(request.frames.size()).arg(m_poseOnly->isChecked()?tr("关闭"):tr("开启")).arg(skipped.size())
        .arg(skipped.isEmpty()?QString():tr("：")+skipped.join(QStringLiteral(", "))));
    log(tr("输出目录：%1").arg(request.outputDirectory));
    ++m_revision;
    m_globalRevision=m_revision;
    m_globalWatcher->setFuture(QtConcurrent::run([request] {
        SelectedPlaneTask task;task.kind=SelectedPlaneTask::Stitch;
        try {
        task.stitched=pcv::stitching::stitchSelectedPlanes(request);
        task.ok=task.stitched.success;
        task.cancelled=task.stitched.cancelled;
        if(task.ok) {
            task.templateSummary.pointCount=task.stitched.points.size();
            bool first=true;
            for(const auto &point:std::as_const(task.stitched.points)) {
                const QVector3D p(point.x,point.y,point.z);
                if(first){task.templateSummary.minimum=task.templateSummary.maximum=p;first=false;}
                else for(int axis=0;axis<3;++axis) {
                    task.templateSummary.minimum[axis]=qMin(task.templateSummary.minimum[axis],p[axis]);
                    task.templateSummary.maximum[axis]=qMax(task.templateSummary.maximum[axis],p[axis]);
                }
            }
        }
        if(!task.ok)task.error=task.stitched.errorCode.isEmpty()?task.stitched.message:task.stitched.errorCode+QStringLiteral(": ")+task.stitched.message;
        } catch(const std::bad_alloc &) {task.ok=false;task.error=QStringLiteral("拼接内存不足，已提交文件保留");}
        return task;
    }));
    updateActions();
}

QJsonObject SelectedPlaneDialog::buildResultSource() const {
    QJsonArray files,skipped,steps;
    for(int row=0;row<int(m_frames.size());++row) {
        const FramePtr &frame=m_frames.at(row);
        const QString name=m_table->item(row,1)?m_table->item(row,1)->text():frame->confirmed.cloud.sourceFile;
        if(frame->committedSkip||frame->skip) {skipped.append(name);continue;}
        if(frame->confirmed.cloud.fullPoints.isEmpty())continue;
        files.append(name);
        steps.append(selectionMethodText(frame->confirmed.selectionMethod));
    }
    steps.append(QStringLiteral("full_data_handeye_conversion_and_selected_plane_stitch"));
    return QJsonObject{
        {QStringLiteral("entry"),QStringLiteral("selected_plane_stitch")},
        {QStringLiteral("files"),files},
        {QStringLiteral("manifest"),m_result.reportFile},
        {QStringLiteral("processing_steps"),steps},
        {QStringLiteral("skipped_files"),skipped},
        {QStringLiteral("coordinate_frame"),QStringLiteral("robot_base")},
        {QStringLiteral("stitch_ply"),m_result.outputPly},
        {QStringLiteral("mapping_file"),m_result.mappingFile},
        {QStringLiteral("note"),QStringLiteral("skipped files are listed here only; no per-point mapping is embedded")}};
}

void SelectedPlaneDialog::saveTemplate() {
    if(m_closing||m_globalBusy||!m_resultValid||m_result.points.isEmpty())return;
    if(activeJobCount()>0) {log(tr("仍有帧任务处理中；请等待结束后再保存模板"));return;}
    if(m_templateForm&&m_templateFormRevision==m_revision) {
        m_views->setCurrentWidget(m_templateForm);m_templateForm->show();return;
    }
    if(m_templateForm) {
        m_views->removeWidget(m_templateForm);
        m_templateForm->deleteLater();
    }
    pcv::workpiece::WorkpieceTemplateInfo summary;
    summary.name=QStringLiteral("selected_plane_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    summary.pointCount=m_result.points.size();
    summary.coordinateFrame=QStringLiteral("robot_base");
    summary.units=QStringLiteral("mm");
    summary.source=m_resultSource;
    summary.minimum=m_templateSummary.minimum;summary.maximum=m_templateSummary.maximum;
    const quint64 revision=m_revision;
    auto *form=new WorkpieceTemplateDialogV1(summary,pcv::workpiece::defaultWorkpieceTemplateRoot(),m_views);
    form->setWindowFlags(Qt::Widget);form->setModal(false);
    m_templateForm=form;m_templateFormRevision=revision;
    m_views->addWidget(form);m_views->setCurrentWidget(form);form->show();
    connect(form,&QDialog::rejected,this,[this] {
        m_views->setCurrentIndex(m_resultValid?1:0);
        log(tr("模板保存已取消；完整结果保留，可重试"));
    });
    connect(form,&QDialog::accepted,this,[this,form,revision] {submitTemplateSave(form,revision);});
}

void SelectedPlaneDialog::submitTemplateSave(WorkpieceTemplateDialogV1 *form, quint64 revision) {
    m_views->setCurrentIndex(m_resultValid?1:0);
    if(m_closing||m_globalBusy||activeJobCount()>0||!m_resultValid||m_result.points.isEmpty()||revision!=m_revision) {
        log(tr("结果已过期或会话已关闭，未提交模板保存"));
        return;
    }
    if(form->templateName().trimmed().isEmpty()||form->outputRoot().isEmpty()) {log(tr("模板名称与保存根目录都不能为空"));return;}
    pcv::workpiece::SaveWorkpieceTemplateRequest request;
    request.name=form->templateName().trimmed();
    request.outputRoot=form->outputRoot();
    request.coordinateFrame=QStringLiteral("robot_base");
    request.points=m_result.points;
    request.source=m_resultSource;
    m_globalBusy=true;
    m_globalToken=std::make_shared<std::atomic_bool>(false);
    const auto token=m_globalToken;
    request.isCancelled=[token] {return token->load();};
    log(tr("开始后台事务保存工件模板：%1 点，robot_base / mm，根目录 %2").arg(request.points.size()).arg(request.outputRoot));
    m_globalWatcher->setFuture(QtConcurrent::run([request] {
        SelectedPlaneTask task;task.kind=SelectedPlaneTask::SaveTemplate;
        try {
        task.savedTemplate=pcv::workpiece::saveWorkpieceTemplate(request);
        task.ok=task.savedTemplate.success;
        task.cancelled=task.savedTemplate.cancelled;
        task.templateSummary=task.savedTemplate.info;
        if(!task.ok)task.error=task.savedTemplate.errorCode+QStringLiteral(": ")+task.savedTemplate.message;
        } catch(const std::bad_alloc &) {task.ok=false;task.error=QStringLiteral("模板保存内存不足，完整拼接结果保留");}
        return task;
    }));
    updateActions();
}

void SelectedPlaneDialog::finishGlobal() {
    if(m_closing)return;
    if(!m_globalWatcher||m_globalWatcher->future().resultCount()==0)return;
    const SelectedPlaneTask task=m_globalWatcher->result();
    m_globalWatcher->setFuture(QFuture<SelectedPlaneTask>());
    m_globalBusy=false;
    m_globalToken.reset();
    if(task.kind==SelectedPlaneTask::Stitch&&m_globalRevision!=m_revision) {
        log(tr("已丢弃过期拼接结果；事务已提交的文件保留"));updateActions();return;
    }
    if(task.kind==SelectedPlaneTask::Stitch) {
        if(task.cancelled) {
            log(tr("拼接已取消；未发布部分结果，原选择保留"));
        } else if(!task.ok) {
            m_resultValid=false;
            log(tr("拼接失败：%1；未发布部分结果，可重试").arg(task.error));
        } else {
            m_result=task.stitched;
            m_templateSummary=task.templateSummary;
            m_resultValid=true;
            m_resultSource=buildResultSource();
            m_resultCanvas->setCloud(m_result.points);
            m_views->setCurrentIndex(1);
            log(tr("完整拼接完成：%1 点；%2").arg(m_result.points.size()).arg(m_result.message));
            log(tr("完整 PLY：%1\nPCVMAP：%2\n拼接报告：%3").arg(m_result.outputPly,m_result.mappingFile,m_result.reportFile));
            for(int index=0;index<m_result.pairs.size();++index) {
                const auto &pair=m_result.pairs.at(index);
                log(tr("相邻对 %1：%2；%3；对应 %4，RMSE %5 mm，层数 %6")
                    .arg(index+1).arg(pair.fineApplied?tr("精配准完成，不可观测方向保持位姿"):tr("退回位姿直拼"))
                    .arg(pair.reason).arg(pair.correspondences).arg(pair.rmseMm).arg(pair.completedLevels));
            }
            log(tr("请检查完整结果；确认无误后点击“保存为模板”。"));
        }
    } else if(task.kind==SelectedPlaneTask::SaveTemplate) {
        if(task.cancelled) {
            log(tr("模板保存已取消；已提交的拼接文件与完整结果保留，可重试"));
        } else if(!task.ok) {
            log(tr("模板保存失败：%1；已提交的拼接文件与完整结果保留，可重试").arg(task.error));
        } else {
            m_templateSummary=task.templateSummary;
            log(tr("工件模板已保存：%1\nJSON：%2\n点数：%3，robot_base / mm")
                .arg(task.savedTemplate.outputDirectory,task.savedTemplate.infoFile)
                .arg(task.templateSummary.pointCount));
            QMessageBox::information(this,tr("工件模板已保存"),
                tr("版本目录：%1\nJSON：%2").arg(task.savedTemplate.outputDirectory,task.savedTemplate.infoFile));
        }
    }
    updateActions();
}

bool SelectedPlaneDialog::prepareDisplayCloud(SelectedPlaneTask *task, const std::function<bool()> &cancelled) {
    return pcv::ui::prepareDisplayCloudV2(task,cancelled);
}
