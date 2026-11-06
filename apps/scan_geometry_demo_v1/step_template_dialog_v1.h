#pragma once
#include "scan_geometry_session_v2.h"
#include <pcv/workpiece/template_package_v1.h>
#include <pcv/matching/template_match_v1.h>
#include <QDialog>
#include <QFutureWatcher>
#include <QThreadPool>
#include <atomic>
namespace Ui {class StepTemplateDialogV1;}
class PointCloudCanvas;
namespace pcv::render {class CadMeshCanvasV1;}
namespace pcv::demo {
class StepTemplateDialogV1 final : public QDialog {
public:
    explicit StepTemplateDialogV1(ScanGeometrySessionV2 *session,QWidget *parent=nullptr);
    ~StepTemplateDialogV1() override;
protected:
    void closeEvent(QCloseEvent *) override;
    void reject() override;
private:
    friend struct StepTemplateViewTestAccessV1;
    struct Outcome {
        pcv::workpiece::StepModelResult model;
        pcv::workpiece::TemplatePackageV1 package;
        pcv::matching::TemplateMatchResultV1 match;
        QVector<pointcloud::Point3D> display;
        QVector<int> mapping;
        bool ok=false,committed=false,publishFailedSearch=false;
        QString error,path,targetIdentity;
        QJsonObject timings;
    };
    void run(QString kind,std::function<Outcome(const std::function<bool()> &)> work,
             std::function<void(Outcome)> accept,bool dependsOnTarget=false);
    void updateControls();
    void importStep(bool reopen);
    void chooseFace(int id);
    void invalidateTemplate();
    void invalidateMatch();
    void preview();
    void refreshFaces();
    void loadPackage(bool directory);
    void generate();
    void savePackage();
    void match();
    void saveMatch();
    void log(QString event,QJsonObject fields={});
    void shutdown();
    pcv::matching::TemplateMatchParametersV1 parameters() const;
    std::unique_ptr<Ui::StepTemplateDialogV1> ui;
    ScanGeometrySessionV2 *m_session;
    pcv::render::CadMeshCanvasV1 *m_cad=nullptr;
    PointCloudCanvas *m_canvas=nullptr;
    pcv::workpiece::StepModelResult m_model;
    pcv::workpiece::TemplatePackageV1 m_package;
    pcv::matching::TemplateMatchResultV1 m_match;
    QSet<int> m_work,m_interference,m_targetRemoved;
    quint64 m_templateRevision=0,m_parameterRevision=0,m_targetRevision=0,m_taskSerial=0;
    quint64 m_resultTemplateRevision=0,m_resultParameterRevision=0,m_resultTargetRevision=0;
    quint64 m_targetTrimRevision=0,m_resultTrimRevision=0;
    int m_confirmed=-1;
    QString m_targetIdentity,m_logPath;
    bool m_busy=false,m_closing=false;
    bool m_targetTask=false;
    QThreadPool m_pool;
    QFutureWatcher<Outcome> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
