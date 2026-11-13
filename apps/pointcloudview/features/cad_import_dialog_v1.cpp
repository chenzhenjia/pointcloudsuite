#include "cad_import_dialog_v1.h"
#include <pcv/render/cad_mesh_canvas_v1.h>
#include <QTableWidget>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <algorithm>
#include <QJsonArray>
using namespace pcv::workpiece;
CadImportDialogV1::CadImportDialogV1(const QString &path,QWidget *parent):QDialog(parent),
    m_cancel(std::make_shared<std::atomic_bool>(false)),m_progress(std::make_shared<Progress>())
{
    setObjectName("cad_import_dialog_v1"); setWindowTitle(tr("从 STEP 生成模板点云")); resize(1150,740);
    auto *layout=new QVBoxLayout(this);
    m_info=new QLabel(path,this); m_info->setWordWrap(true); m_info->setTextFormat(Qt::PlainText); layout->addWidget(m_info);
    auto *body=new QHBoxLayout;
    m_canvas=new pcv::render::CadMeshCanvasV1(this); body->addWidget(m_canvas,2);
    auto *right=new QVBoxLayout;
    m_faces=new QTableWidget(0,3,this); m_faces->setObjectName("cad_faces");
    m_faces->setHorizontalHeaderLabels({tr("面"),tr("曲面类型"),tr("面积 mm²")});
    m_faces->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    right->addWidget(m_faces);
    auto *selection=new QHBoxLayout; m_all=new QPushButton(tr("全选"),this); m_clear=new QPushButton(tr("清空"),this);
    selection->addWidget(m_all); selection->addWidget(m_clear); right->addLayout(selection);
    right->addWidget(new QLabel(tr("目标点间距 mm（密度目标）"),this));
    m_spacing=new QDoubleSpinBox(this); m_spacing->setObjectName("cad_spacing_mm");
    m_spacing->setDecimals(6); m_spacing->setRange(.000001,1000000000); m_spacing->setValue(.5); right->addWidget(m_spacing);
    m_estimate=new QLabel(this); m_estimate->setWordWrap(true); right->addWidget(m_estimate);
    auto *note=new QLabel(tr("保留 CAD 原点与轴向；输出 cad_model / mm。\n只采样选中曲面，不额外补边界点。\n输出误差上限 0.01 mm；最多 1000 万点。"),this);
    note->setWordWrap(true); right->addWidget(note); body->addLayout(right,1); layout->addLayout(body,1);
    m_status=new QLabel(tr("读取 STEP…"),this); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
    auto *buttons=new QDialogButtonBox(this);
    m_generateButton=buttons->addButton(tr("生成并替换当前点云"),QDialogButtonBox::ActionRole); m_generateButton->setObjectName("cad_generate");
    m_cancelTask=buttons->addButton(tr("取消任务"),QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&CadImportDialogV1::reject);
    connect(m_generateButton,&QPushButton::clicked,this,&CadImportDialogV1::generate);
    connect(m_cancelTask,&QPushButton::clicked,this,[this]{m_cancel->store(true);m_status->setText(tr("取消中，等待 CAD 调用安全退出…"));});
    connect(m_spacing,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refreshSelection();});
    connect(m_faces,&QTableWidget::itemChanged,this,[this]{refreshSelection();});
    const auto choose=[this](Qt::CheckState state){
        if(m_busy) return;
        { const QSignalBlocker blocker(m_faces); for(int i=0;i<m_faces->rowCount();++i) m_faces->item(i,0)->setCheckState(state); }
        refreshSelection();
    };
    connect(m_all,&QPushButton::clicked,this,[choose]{choose(Qt::Checked);});
    connect(m_clear,&QPushButton::clicked,this,[choose]{choose(Qt::Unchecked);});
    m_canvas->faceClicked=[this](int id){
        if(m_busy || id<1 || id>m_faces->rowCount()) return;
        auto *item=m_faces->item(id-1,0); item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
        m_faces->scrollToItem(item);
    };
    m_timer=new QTimer(this); m_timer->setInterval(100);
    connect(m_timer,&QTimer::timeout,this,[this]{
        if(!m_busy || m_closing || m_cancel->load()) return;
        QString text;
        switch(m_progress->stage.load()) {
        case CadStage::Reading:text=tr("读取 STEP");break;
        case CadStage::Validating:text=tr("校验单位与单实体");break;
        case CadStage::Meshing:text=tr("三角化曲面");break;
        case CadStage::Sampling:text=tr("生成曲面点云");break;
        }
        const auto total=m_progress->total.load();
        if(total>0) text+=tr(" %1 / %2").arg(m_progress->done.load()).arg(total);
        m_status->setText(text+QStringLiteral("…"));
    }); m_timer->start();
    connect(&m_read,&QFutureWatcher<StepModelResult>::finished,this,[this]{
        if(m_closing) return;
        m_model=m_read.future().takeResult(); m_busy=false;
        if(m_model.success && !m_cancel->load()) {
            if(!m_canvas->setMesh(m_model.mesh)) { m_model.success=false; m_status->setText(tr("CAD 显示网格无效")); refreshSelection(); return; }
            const auto cad=m_model.source.value("cad").toObject();
            m_info->setText(tr("%1\n%2 → mm（倍率 %3） | cad_model | %4 个面\n包围盒 [%5, %6, %7] → [%8, %9, %10] mm")
                .arg(m_model.source.value("files").toArray().first().toString(),cad.value("original_units").toString())
                .arg(cad.value("unit_scale_to_mm").toDouble()).arg(m_model.faces.size())
                .arg(m_model.minimum.x()).arg(m_model.minimum.y()).arg(m_model.minimum.z())
                .arg(m_model.maximum.x()).arg(m_model.maximum.y()).arg(m_model.maximum.z()));
            const QSignalBlocker blocker(m_faces); m_faces->setRowCount(int(m_model.faces.size()));
            for(int i=0;i<m_model.faces.size();++i) {
                const auto &face=m_model.faces.at(i);
                auto *item=new QTableWidgetItem(QString::number(face.id));
                item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable); item->setCheckState(Qt::Unchecked); m_faces->setItem(i,0,item);
                for(int col=1;col<3;++col) { auto *value=new QTableWidgetItem(col==1?face.surfaceType:QString::number(face.areaMm2,'g',10)); value->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable); m_faces->setItem(i,col,value); }
            }
            m_status->setText(tr("实体校验完成，请选择要生成的 CAD 面。"));
        } else { m_model.success=false; m_status->setText(m_cancel->load()?tr("导入已取消，请关闭后重新导入"):m_model.message); }
        refreshSelection();
    });
    connect(&m_generate,&QFutureWatcher<CadCloudResult>::finished,this,[this]{
        if(m_closing) return;
        m_result=m_generate.future().takeResult(); m_busy=false;
        if(m_result.success && !m_cancel->load()) { accept(); return; }
        m_status->setText(m_cancel->load()?tr("生成已取消，可调整参数后重试"):m_result.message); refreshSelection();
    });
    ReadStepModelRequest request; request.file=path;
    request.isCancelled=[cancel=m_cancel]{return cancel->load();};
    request.progress=[p=m_progress](CadStage s,qint64 n,qint64 t){p->done.store(n);p->total.store(t);p->stage.store(s);};
    refreshSelection(); m_read.setFuture(QtConcurrent::run([request]{return readStepModel(request);}));
}
void CadImportDialogV1::refreshSelection()
{
    QSet<int> selected; double count=0;
    if(m_model.success) for(int i=0;i<m_faces->rowCount();++i) if(m_faces->item(i,0)->checkState()==Qt::Checked) {
        selected.insert(i+1); count+=std::max(1.0,std::ceil(m_model.faces.at(i).areaMm2/m_spacing->value()/m_spacing->value()));
    }
    m_canvas->setSelectedFaces(selected);
    m_estimate->setText(tr("已选 %1 面，预计 %2 点%3").arg(selected.size()).arg(count,0,'f',0).arg(count>kCadMaximumPoints?tr("；超限，请增大间距"):QString()));
    m_generateButton->setEnabled(!m_busy && m_model.success && !selected.isEmpty() && count<=kCadMaximumPoints);
    m_faces->setEnabled(!m_busy); m_spacing->setEnabled(!m_busy); m_all->setEnabled(!m_busy && m_model.success); m_clear->setEnabled(!m_busy && m_model.success); m_cancelTask->setEnabled(m_busy);
}
void CadImportDialogV1::generate()
{
    if(m_busy || !m_generateButton->isEnabled()) return;
    m_cancel->store(false); m_progress->stage.store(CadStage::Sampling); m_progress->total.store(0); m_busy=true; refreshSelection();
    GenerateCadTemplateRequest request; request.model=m_model.model; request.spacingMm=m_spacing->value();
    for(int i=0;i<m_faces->rowCount();++i) if(m_faces->item(i,0)->checkState()==Qt::Checked) request.faceIds.append(i+1);
    request.isCancelled=[cancel=m_cancel]{return cancel->load();};
    request.progress=[p=m_progress](CadStage s,qint64 n,qint64 t){p->done.store(n);p->total.store(t);p->stage.store(s);};
    m_generate.setFuture(QtConcurrent::run([request]{return generateCadTemplateCloud(request);}));
}
void CadImportDialogV1::cancelAndWait()
{
    m_closing=true; m_cancel->store(true); m_timer->stop();
    disconnect(&m_read,nullptr,this,nullptr); disconnect(&m_generate,nullptr,this,nullptr);
    m_read.waitForFinished(); m_generate.waitForFinished();
}
void CadImportDialogV1::reject() { cancelAndWait(); QDialog::reject(); }
CadImportDialogV1::~CadImportDialogV1() { cancelAndWait(); }
