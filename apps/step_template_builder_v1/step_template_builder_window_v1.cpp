#include "step_template_builder_window_v1.h"
#include "ui_step_template_builder_window_v1.h"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <algorithm>

// Referencing this symbol keeps resources linked from the static view library.
static void initializeBuilderResources(){Q_INIT_RESOURCE(step_template_builder_v1);}
namespace pcv::stepbuilder {
namespace {
bool stepAvailable(){
#ifdef PCV_BUILD_STEP_IMPORT
    return true;
#else
    return false;
#endif
}
QString extent(const QVector3D &minimum,const QVector3D &maximum){
    const auto d=maximum-minimum;return QStringLiteral("%1 × %2 × %3 mm").arg(d.x(),0,'f',2).arg(d.y(),0,'f',2).arg(d.z(),0,'f',2);
}
QString regionExtent(const pcv::workpiece::TemplatePackageV1 &package){
    if(package.points.isEmpty())return QStringLiteral("不适用");
    QVector3D minimum(package.points.first().x,package.points.first().y,package.points.first().z),maximum=minimum;
    // Region meshes bound the selected, trimmed planes without rescanning millions of points on the GUI thread.
    for(const auto &plane:package.planes)if(!plane.interference)for(const auto &v:plane.region.vertices)
        for(int k=0;k<3;++k){minimum[k]=std::min(minimum[k],v[k]);maximum[k]=std::max(maximum[k],v[k]);}
    return extent(minimum,maximum);
}
}
StepTemplateBuilderWindowV1::StepTemplateBuilderWindowV1(QWidget *parent):QMainWindow(parent),
    ui(std::make_unique<Ui::StepTemplateBuilderWindowV1>()){
    initializeBuilderResources();ui->setupUi(this);
    QFile style(":/step_template_builder_v1/builder.qss");if(style.open(QIODevice::ReadOnly))setStyleSheet(QString::fromUtf8(style.readAll()));
    ui->canvasHostLayout->setContentsMargins(0,0,0,0);
    m_canvas=new PointCloudCanvas(ui->canvasHost);ui->canvasHostLayout->addWidget(m_canvas);
    m_rubberBand=new QRubberBand(QRubberBand::Rectangle,m_canvas);
    m_canvas->setAppearance(QColor("#eef1f4"),QColor("#1675e8"));m_canvas->setPointSize(2);
    m_canvas->clearUniformPointColor();m_canvas->setDisplayOptions(3,1,0,1);
    auto options=m_canvas->overlayOptions();options.showColorBar=false;options.showCoordinateAxes=false;options.showStatistics=true;options.showOrientation=true;m_canvas->setOverlayOptions(options);
    m_canvas->installEventFilter(this);
    const QList<QToolButton*> iconButtons{ui->toolWorkpiece,ui->toolInterference,ui->toolTrim,ui->clearRoles,ui->undoTrim,ui->restoreTrim};
    const QStringList icons{"face","interference","rectangle","reset-planes","undo","restore"};
    for(int i=0;i<iconButtons.size();++i){iconButtons.at(i)->setIcon(QIcon(":/step_template_builder_v1/icons/"+icons.at(i)+".svg"));iconButtons.at(i)->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);}
    connect(&m_session,&StepTemplateBuilderSessionV1::changed,this,[this]{refreshFaces();updateColors();updateUi();});
    connect(&m_session,&StepTemplateBuilderSessionV1::displayChanged,this,[this]{updateDisplay();});
    connect(&m_session,&StepTemplateBuilderSessionV1::savedVersion,this,[this](const QString &path){ui->saveStatus->setText(tr("已保存新版本：%1\n%2").arg(m_session.saved().info.versionId,path));});
    connect(ui->importStep,&QPushButton::clicked,this,[this]{
        const QString file=QFileDialog::getOpenFileName(this,tr("导入 STEP"),{},tr("STEP 文件 (*.step *.stp);;所有文件 (*)"));
        if(!file.isEmpty())openStepFile(file);
    });
    connect(ui->spacing,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{clearRectangle();m_session.invalidateSampling();});
    connect(ui->processCloud,&QPushButton::clicked,&m_session,&StepTemplateBuilderSessionV1::process);
    connect(ui->confirmGeometry,&QPushButton::clicked,this,[this]{m_session.confirm();});
    connect(ui->saveDraft,&QPushButton::clicked,this,[this]{
        if(!ui->draftReview->isChecked()||!m_session.confirmed())return;
        const QString root=QFileDialog::getExistingDirectory(this,tr("选择源码树外输出目录"));
        if(!root.isEmpty())m_session.save(ui->workpieceName->text(),ui->material->currentText(),root);
    });
    connect(ui->openDirectory,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(m_session.saved().outputDirectory));});
    connect(ui->cancelTask,&QPushButton::clicked,&m_session,&StepTemplateBuilderSessionV1::cancel);
    connect(ui->previous,&QPushButton::clicked,this,[this]{setPage(m_page-1);});
    connect(ui->next,&QPushButton::clicked,this,[this]{setPage(m_page+1);});
    const QList<QToolButton*> steps{ui->step0,ui->step1,ui->step2};
    for(int i=0;i<steps.size();++i)connect(steps.at(i),&QToolButton::clicked,this,[this,i]{setPage(i);});
    const QList<QPushButton*> presets{ui->preset0,ui->preset1,ui->preset2,ui->preset3,ui->preset4,ui->preset5,ui->preset6,ui->preset7};
    for(auto *preset:presets)connect(preset,&QPushButton::clicked,this,[this,preset]{ui->workpieceName->setText(preset->text());});
    connect(ui->workpieceName,&QLineEdit::textChanged,this,[this]{ui->draftReview->setChecked(false);m_session.metadataChanged();updateUi();});
    connect(ui->material,&QComboBox::currentTextChanged,this,[this]{ui->draftReview->setChecked(false);m_session.metadataChanged();updateUi();});
    connect(ui->draftReview,&QCheckBox::toggled,this,[this]{updateUi();});
    connect(ui->toolWorkpiece,&QToolButton::clicked,this,[this](bool checked){setTool(checked?1:0);});
    connect(ui->toolInterference,&QToolButton::clicked,this,[this](bool checked){setTool(checked?2:0);});
    connect(ui->toolTrim,&QToolButton::clicked,this,[this](bool checked){setTool(checked?3:0);});
    connect(ui->clearRoles,&QToolButton::clicked,this,[this]{clearRectangle();m_session.clearRoles();});
    connect(ui->undoTrim,&QToolButton::clicked,this,[this]{clearRectangle();m_session.undo();});
    connect(ui->restoreTrim,&QToolButton::clicked,this,[this]{clearRectangle();m_session.restore();});
    connect(ui->eraseSelection,&QPushButton::clicked,this,[this]{m_rubberBand->hide();m_session.erasePending();});
    connect(ui->fitView,&QPushButton::clicked,this,[this]{clearRectangle();m_canvas->fitView();});
    connect(ui->topView,&QPushButton::clicked,this,[this]{clearRectangle();m_canvas->setStandardView(2);});
    connect(ui->faceList,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){pickFace(item->data(Qt::UserRole).toInt());});
    m_canvas->pointPicked=[this](int index){
        const auto &ranges=m_session.cloud().faceRanges;
        for(const auto &range:ranges)if(index>=range.first&&index<range.first+range.count){pickFace(range.faceId);break;}
    };
    setTool(0);updateUi();
}
void StepTemplateBuilderWindowV1::openStepFile(const QString &file){
    if(m_session.busy()||!stepAvailable())return;
    if(ui->workpieceName->text().trimmed().isEmpty())ui->workpieceName->setText(QFileInfo(file).completeBaseName());
    setPage(1);clearRectangle();ui->draftReview->setChecked(false);m_session.importStep(file,ui->spacing->value());
}
StepTemplateBuilderWindowV1::~StepTemplateBuilderWindowV1(){m_session.shutdown();}
void StepTemplateBuilderWindowV1::closeEvent(QCloseEvent *event){m_session.shutdown();event->accept();}
void StepTemplateBuilderWindowV1::keyPressEvent(QKeyEvent *event){
    if(event->key()==Qt::Key_Escape){if(m_session.busy())m_session.cancel();else setTool(0);event->accept();return;}
    QMainWindow::keyPressEvent(event);
}
void StepTemplateBuilderWindowV1::clearRectangle(){m_dragging=false;m_rectangle={};m_rubberBand->hide();m_session.clearPending();}
void StepTemplateBuilderWindowV1::setTool(int tool){
    if(m_session.busy())return;clearRectangle();m_tool=tool;
    ui->toolWorkpiece->setChecked(tool==1);ui->toolInterference->setChecked(tool==2);ui->toolTrim->setChecked(tool==3);
    m_canvas->setSelectionMode(tool==1||tool==2);m_canvas->setCursor(tool==0?Qt::ArrowCursor:Qt::CrossCursor);
}
bool StepTemplateBuilderWindowV1::eventFilter(QObject *object,QEvent *event){
    if(object!=m_canvas)return QMainWindow::eventFilter(object,event);
    if(m_session.busy()&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::Wheel))return true;
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){
        if(m_session.busy())m_session.cancel();else setTool(0);return true;
    }
    if(event->type()==QEvent::Resize||event->type()==QEvent::Wheel){
        if(m_session.busy()&&m_tool==3)m_session.cancel();
        clearRectangle();
    }
    if(m_tool==3){
        if(event->type()==QEvent::MouseButtonPress){
            auto *mouse=static_cast<QMouseEvent*>(event);clearRectangle();
            if(mouse->button()==Qt::LeftButton){m_start=mouse->position().toPoint();m_dragging=true;m_canvas->setFocus();return true;}
        }else if(event->type()==QEvent::MouseMove&&m_dragging){
            m_rectangle=QRect(m_start,static_cast<QMouseEvent*>(event)->position().toPoint()).normalized().intersected(m_canvas->rect());
            m_rubberBand->setGeometry(m_rectangle);m_rubberBand->show();return true;
        }else if(event->type()==QEvent::MouseButtonRelease&&m_dragging){
            if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){
                m_dragging=false;m_rectangle=QRect(m_start,static_cast<QMouseEvent*>(event)->position().toPoint()).normalized().intersected(m_canvas->rect());
                if(m_rectangle.width()<3||m_rectangle.height()<3){clearRectangle();return true;}
                m_rubberBand->setGeometry(m_rectangle);m_rubberBand->show();
                m_session.selectRectangle(m_canvas->projectionSnapshot(),m_rectangle);return true;
            }
        }
    }
    return QMainWindow::eventFilter(object,event);
}
void StepTemplateBuilderWindowV1::pickFace(int id){
    if(m_tool!=1&&m_tool!=2)return;clearRectangle();ui->draftReview->setChecked(false);m_session.toggleFace(id,m_tool==2);
}
void StepTemplateBuilderWindowV1::updateDisplay(){
    const bool fit=m_session.removedCount()==0;
    m_canvas->setDisplayCloud(m_session.displayPoints(),m_session.displayMap(),m_session.cloud().points.size());
    m_canvas->setEmptyMessage(m_session.cloud().success?tr("当前没有保留点，可撤销或恢复修剪"):tr("导入 STEP 后显示完整数模点云"));
    updateColors();if(fit)m_canvas->fitView();
}
void StepTemplateBuilderWindowV1::updateColors(){
    const auto displayRevision=m_canvas->displayCloudRevision();
    if(m_haveColors&&m_coloredDisplayRevision==displayRevision&&m_coloredWork==m_session.workFaces()
        &&m_coloredInterference==m_session.interferenceFaces())return;
    pcv::render::PointColorOverlay overlay;
    const auto &mapping=m_session.displayMap();overlay.colors.reserve(mapping.size());overlay.valid.fill(1,mapping.size());
    qsizetype face=0;const auto &ranges=m_session.cloud().faceRanges;
    for(int index:mapping){
        while(face<ranges.size()&&index>=ranges.at(face).first+ranges.at(face).count)++face;
        const int id=face<ranges.size()?ranges.at(face).faceId:0;
        overlay.colors.append(m_session.workFaces().contains(id)?QVector4D(.09f,.46f,.91f,1):
            m_session.interferenceFaces().contains(id)?QVector4D(.94f,.22f,.28f,1):QVector4D(.36f,.40f,.44f,1));
    }
    if(m_canvas->setDisplayPointColorOverlay(overlay)){
        m_coloredDisplayRevision=displayRevision;m_coloredWork=m_session.workFaces();
        m_coloredInterference=m_session.interferenceFaces();m_haveColors=true;
    }
}
void StepTemplateBuilderWindowV1::refreshFaces(){
    const QSignalBlocker blocker(ui->faceList);ui->faceList->clear();
    for(const auto &face:m_session.model().faces){
        const bool work=m_session.workFaces().contains(face.id),interference=m_session.interferenceFaces().contains(face.id);
        auto *item=new QListWidgetItem(tr("%1面 %2 · %3").arg(work?tr("[工件] "):interference?tr("[干涉] "):QString()).arg(face.id).arg(face.surfaceType),ui->faceList);
        item->setData(Qt::UserRole,face.id);item->setToolTip(tr("面积 %1 mm²；选择模式后单击可切换该面角色").arg(face.areaMm2,0,'f',2));
        item->setForeground(work?QColor("#1769c2"):interference?QColor("#c53b43"):QColor("#59636f"));
    }
}
void StepTemplateBuilderWindowV1::setPage(int page){
    if(m_session.busy()||page<0||page>2)return;
    if(page>0&&(!stepAvailable()||ui->workpieceName->text().trimmed().isEmpty()))return;
    if(page==2&&!m_session.confirmed())return;
    clearRectangle();m_page=page;ui->pages->setCurrentIndex(page);updateUi();
}
void StepTemplateBuilderWindowV1::updateUi(){
    const bool busy=m_session.busy(),named=!ui->workpieceName->text().trimmed().isEmpty();
    const auto &package=m_session.package();
    if(!busy&&!m_dragging&&m_session.pendingCount()==0)m_rubberBand->hide();
    ui->workpieceName->setEnabled(!busy);ui->material->setEnabled(!busy);ui->spacing->setEnabled(!busy);
    for(auto *button:{ui->preset0,ui->preset1,ui->preset2,ui->preset3,ui->preset4,ui->preset5,ui->preset6,ui->preset7})button->setEnabled(!busy);
    ui->step0->setEnabled(!busy);ui->step1->setEnabled(!busy&&named&&stepAvailable());ui->step2->setEnabled(!busy&&named&&m_session.confirmed());
    ui->step0->setChecked(m_page==0);ui->step1->setChecked(m_page==1);ui->step2->setChecked(m_page==2);
    ui->previous->setEnabled(!busy&&m_page>0);ui->next->setEnabled(!busy&&((m_page==0&&named&&stepAvailable())||(m_page==1&&m_session.confirmed())));
    ui->next->setVisible(m_page<2);ui->progress->setText(tr("步骤 %1 / 3").arg(m_page+1));
    ui->importStep->setEnabled(!busy&&stepAvailable());
    for(auto *button:{ui->toolWorkpiece,ui->toolInterference,ui->toolTrim,ui->clearRoles})button->setEnabled(!busy&&m_session.cloud().success);
    ui->undoTrim->setEnabled(!busy&&m_session.canUndo());ui->restoreTrim->setEnabled(!busy&&m_session.removedCount()>0);
    ui->faceList->setEnabled(!busy);ui->fitView->setEnabled(!busy);ui->topView->setEnabled(!busy);
    ui->processCloud->setEnabled(m_session.canProcess());ui->confirmGeometry->setEnabled(!busy&&package.ok&&!m_session.confirmed());
    ui->confirmGeometry->setText(m_session.confirmed()?tr("几何已确认"):tr("确认几何结果"));
    if(!m_session.confirmed())ui->draftReview->setChecked(false);
    ui->draftReview->setEnabled(!busy&&m_session.confirmed());
    ui->saveDraft->setEnabled(!busy&&named&&m_session.confirmed()&&ui->draftReview->isChecked());
    ui->openDirectory->setEnabled(!busy&&m_session.saved().success);
    ui->eraseSelection->setVisible(m_session.pendingCount()>0);ui->eraseSelection->setEnabled(!busy);ui->eraseSelection->setText(tr("擦除框选的 %1 点").arg(m_session.pendingCount()));
    ui->cancelTask->setVisible(busy);ui->taskProgress->setVisible(busy);
    ui->taskStatus->setText(stepAvailable()?m_session.status():tr("当前构建未启用 STEP 导入，请使用 PCV_BUILD_STEP_IMPORT=ON 配置"));
    ui->workpieceId->setText(tr("工件编号：%1").arg(m_session.workpieceId()));
    ui->identityGateStatus->setText(named?tr("工件信息已填写"):tr("待填写工件名称"));
    ui->roleSummary->setText(tr("工件面 %1 · 干涉面 %2\n已修剪 %3 点").arg(m_session.workFaces().size()).arg(m_session.interferenceFaces().size()).arg(m_session.removedCount()));
    ui->geometrySummary->setText(package.ok?tr("工件 %1 点 · 干涉 %2 点\n区域尺寸：%3\n%4").arg(package.points.size()).arg(package.interferencePoints.size()).arg(regionExtent(package)).arg(m_session.confirmed()?tr("几何已确认"):tr("请复核后确认")):tr("选择工件解析面后处理；至少保留一个有效工件点区域。"));
    const auto &model=m_session.model();
    ui->sourceSummary->setText(m_session.cloud().success?tr("cad_model / mm · 完整 %1 点 · 展示 %2 点\n模型尺寸：%3").arg(m_session.cloud().points.size()).arg(m_session.displayPoints().size()).arg(extent(model.minimum,model.maximum)):tr("尚未导入有效 STEP"));
    ui->draftGateStatus->setText(m_session.confirmed()?tr("几何已确认，可复核创建"):tr("待确认几何"));
    int workCount=0,interferenceCount=0;for(const auto &plane:package.planes)(plane.interference?interferenceCount:workCount)++;
    ui->draftSummary->setPlainText(package.ok?tr("工件名称：%1\n材料：%2\n工件编号：%3\n坐标 / 单位：cad_model / mm\n工件点：%4\n干涉点：%5\n工件面：%6\n干涉面：%7\n区域尺寸：%8\n像素尺寸：不适用\n保存版本：%9\n来源：\n%10")
        .arg(ui->workpieceName->text(),ui->material->currentText(),m_session.workpieceId())
        .arg(package.points.size()).arg(package.interferencePoints.size()).arg(workCount).arg(interferenceCount)
        .arg(regionExtent(package),m_session.saved().success?m_session.saved().info.versionId:tr("保存后生成"),tr("STEP：%1\nSHA256：%2\n原始单位：%3\n采样间距：%4 mm").arg(package.source.value("cad").toObject().value("original_step").toString(),package.source.value("cad").toObject().value("sha256").toString(),package.source.value("cad").toObject().value("original_units").toString()).arg(package.source.value("cad").toObject().value("spacing_mm").toDouble())):tr("尚未生成有效模板"));
    if(!m_session.saved().success)ui->saveStatus->setText(tr("尚未保存当前结果"));
}
}
