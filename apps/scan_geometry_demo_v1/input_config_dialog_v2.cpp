#include "input_config_dialog_v2.h"
#include "ui_input_config_dialog_v2.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QFileInfo>
#include <QDir>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <cmath>
namespace pcv::demo {
InputConfigDialogV2::InputConfigDialogV2(const InputConfigV2 &config,QWidget *parent)
    :QDialog(parent),ui(std::make_unique<Ui::InputConfigDialogV2>()),m_config(config) {
    ui->setupUi(this);
    ui->frames->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->frames->setSelectionBehavior(QAbstractItemView::SelectRows);ui->frames->setSelectionMode(QAbstractItemView::SingleSelection);
    for(const auto &frame:config.frames)append(frame,true);
    ui->calibration->setText(config.calibration);ui->outputRoot->setText(config.outputRoot);
    ui->tolerance->setValue(config.tolerance);ui->maximumGap->setValue(config.maximumGap);
    ui->distance->setValue(config.searchDistance);ui->angle->setValue(config.searchAngle);ui->seeds->setValue(config.maximumSeeds);
    ui->highSideHeight->setValue(config.highSideMinimumHeightMm);ui->highSidePoints->setValue(config.highSideMinimumPointCount);
    ui->poseOnly->setChecked(config.poseOnly);ui->threePoint->setChecked(config.threePoint);
    connect(ui->addFiles,&QPushButton::clicked,this,[this]{addFiles();});
    connect(ui->addDepthFrame,&QPushButton::clicked,this,[this]{addDepthFrame();});
    connect(ui->openScanJson,&QPushButton::clicked,this,[this]{addScanJson();});
    connect(ui->remove,&QPushButton::clicked,this,[this]{if(ui->frames->currentRow()>=0)ui->frames->removeRow(ui->frames->currentRow());});
    connect(ui->up,&QPushButton::clicked,this,[this]{moveRow(-1);});connect(ui->down,&QPushButton::clicked,this,[this]{moveRow(1);});
    connect(ui->browseXml,&QPushButton::clicked,this,[this]{const auto p=QFileDialog::getOpenFileName(this,tr("选择共用手眼 XML"),{},"XML (*.xml);;All files (*)");if(!p.isEmpty())ui->calibration->setText(p);});
    connect(ui->browseOutput,&QPushButton::clicked,this,[this]{const auto p=QFileDialog::getExistingDirectory(this,tr("选择输出根目录"));if(!p.isEmpty())ui->outputRoot->setText(p);});
    connect(ui->buttons,&QDialogButtonBox::accepted,this,[this]{acceptInput();});connect(ui->buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
InputConfigDialogV2::~InputConfigDialogV2()=default;
void InputConfigDialogV2::append(const InputFrameV2 &frame,bool provided) {
    const int r=ui->frames->rowCount();ui->frames->insertRow(r);ui->frames->setItem(r,0,new QTableWidgetItem(frame.path));
    auto *source=ui->frames->item(r,0);
    source->setData(Qt::UserRole,frame.calibration);
    source->setData(Qt::UserRole+1,frame.scanJson);
    source->setData(Qt::UserRole+2,frame.scanId);
    source->setData(Qt::UserRole+3,int(frame.layout));
    source->setData(Qt::UserRole+4,frame.depthFrame);
    source->setToolTip(frame.scanJson.isEmpty()?frame.path:frame.scanJson+"\nXML: "+frame.calibration+"\n"+frame.warning);
    int c=1;for(const auto &p:{frame.start,frame.end})for(double v:{p.x,p.y,p.z,p.rx,p.ry,p.rz})
        ui->frames->setItem(r,c++,new QTableWidgetItem(provided?QString::number(v,'g',16):QString()));
}
void InputConfigDialogV2::addFiles() {
    const auto files=QFileDialog::getOpenFileNames(this,tr("添加有序 PLY（添加后可上下调整）"),{},"PLY (*.ply);;All files (*)");
    for(const auto &path:files){InputFrameV2 frame;frame.path=path;append(frame,false);}
}
void InputConfigDialogV2::addDepthFrame() {
    const auto frameJson=QFileDialog::getOpenFileName(this,tr("选择SDK深度帧 frame.json"),{},"frame.json (frame.json);;JSON (*.json)");
    if(frameJson.isEmpty())return;
    const auto scanPath=QFileDialog::getOpenFileName(this,tr("选择该帧对应的扫描 JSON"),{},"JSON (*.json)");
    if(scanPath.isEmpty())return;
    InputFrameV2 frame;QString error;
    if(!pcv::ui::parseSelectedPlaneScanJsonV2(scanPath,&frame,&error)) {
        QMessageBox::warning(this,tr("扫描 JSON 无效"),scanPath+"\n"+error);return;
    }
    frame.path=QFileInfo(frameJson).absolutePath();
    frame.depthFrame=true;frame.scanJson=QFileInfo(scanPath).absoluteFilePath();
    frame.calibration.clear();
    frame.layout=pointcloud::DepthPointLayout::FullXyz;
    frame.warning=tr("深度帧按SDK转换，扫描位姿按栅格行插值");
    append(frame,true);
}
void InputConfigDialogV2::addScanJson() {
    const auto paths=QFileDialog::getOpenFileNames(this,tr("打开扫描 JSON（每个文件一帧，导入后可调整顺序）"),{},"JSON (*.json)");
    QVector<InputFrameV2> parsed;
    for(const auto &path:paths) {
        InputFrameV2 frame;QString error;
        if(!pcv::ui::parseSelectedPlaneScanJsonV2(path,&frame,&error)) {
            QMessageBox::warning(this,tr("扫描 JSON 无效"),path+"\n"+error);return;
        }
        parsed.append(frame);
    }
    for(const auto &frame:parsed)append(frame,true);
}
void InputConfigDialogV2::moveRow(int delta) {
    const int r=ui->frames->currentRow(),other=r+delta;if(r<0||other<0||other>=ui->frames->rowCount())return;
    for(int c=0;c<13;++c){auto *a=ui->frames->takeItem(r,c);auto *b=ui->frames->takeItem(other,c);ui->frames->setItem(r,c,b);ui->frames->setItem(other,c,a);}
    ui->frames->setCurrentCell(other,0);
}
void InputConfigDialogV2::acceptInput() {
    InputConfigV2 result;result.calibration=ui->calibration->text().trimmed();result.outputRoot=ui->outputRoot->text().trimmed();
    if(ui->frames->rowCount()<2){QMessageBox::warning(this,tr("输入不足"),tr("至少添加两帧输入"));return;}
    for(int r=0;r<ui->frames->rowCount();++r) {
        InputFrameV2 frame;frame.path=ui->frames->item(r,0)?ui->frames->item(r,0)->text().trimmed():QString();
        const auto *source=ui->frames->item(r,0);
        if(source) {
            frame.calibration=source->data(Qt::UserRole).toString();
            frame.scanJson=source->data(Qt::UserRole+1).toString();
            frame.scanId=source->data(Qt::UserRole+2).toString();
            if(source->data(Qt::UserRole+3).isValid())frame.layout=pointcloud::DepthPointLayout(source->data(Qt::UserRole+3).toInt());
            frame.depthFrame=source->data(Qt::UserRole+4).toBool();
        }
        int c=1;for(auto *p:{&frame.start,&frame.end})for(double *v:{&p->x,&p->y,&p->z,&p->rx,&p->ry,&p->rz}) {
            const auto *item=ui->frames->item(r,c);bool ok=false;*v=item?item->text().trimmed().toDouble(&ok):0;
            if(!ok||!std::isfinite(*v)){QMessageBox::warning(this,tr("位姿无效"),tr("第 %1 行，第 %2 列必须输入有限数字，单位 mm/deg").arg(r+1).arg(c+1));ui->frames->setCurrentCell(r,c);return;}++c;
        }
        const QFileInfo inputInfo(frame.path);
        if(frame.depthFrame ? (!inputInfo.isDir()||!QFileInfo(QDir(inputInfo.absoluteFilePath()).filePath("frame.json")).isFile()) : !inputInfo.isFile()) {
            QMessageBox::warning(this,tr("输入不存在"),frame.path);return;
        }
        result.frames.append(frame);
    }
    for(const auto &frame:result.frames)if(!QFileInfo(frame.calibration.isEmpty()?result.calibration:frame.calibration).isFile()) {
        QMessageBox::warning(this,tr("配置不完整"),tr("请选择有效的手眼 XML；JSON 帧使用各自的 XML"));return;
    }
    if(result.outputRoot.isEmpty()){QMessageBox::warning(this,tr("配置不完整"),tr("请选择输出根目录"));return;}
    result.tolerance=ui->tolerance->value();result.maximumGap=ui->maximumGap->value();result.searchDistance=ui->distance->value();
    result.searchAngle=ui->angle->value();result.maximumSeeds=ui->seeds->value();result.poseOnly=ui->poseOnly->isChecked();result.threePoint=ui->threePoint->isChecked();
    result.highSideMinimumHeightMm=ui->highSideHeight->value();result.highSideMinimumPointCount=ui->highSidePoints->value();
    m_config=result;accept();
}
}
