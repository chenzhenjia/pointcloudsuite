#pragma once
#include "scan_geometry_session_v2.h"
#include <QMainWindow>
#include <QVector>
#include <memory>
namespace Ui { class ScanGeometryWindowV1; }
class QToolButton;
namespace pcv::demo {
class ScanGeometryCanvasV2;
class ScanGeometryWindowV1 final : public QMainWindow {
public:
    explicit ScanGeometryWindowV1(QWidget *parent=nullptr);
    ~ScanGeometryWindowV1() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend struct ScanGeometryViewTestAccessV2;
    void refresh(bool publish);
    void configureInputs(bool scanJson=false);
    bool discardSession(const QString &reason);
    void setWorkflowStage(int stage);
    int maximumWorkflowStage() const;
    std::unique_ptr<Ui::ScanGeometryWindowV1> ui;
    ScanGeometrySessionV2 m_session;
    ScanGeometryCanvasV2 *m_view=nullptr;
    int m_lastActive=-2;
    const pointcloud::Point3D *m_lastCloud=nullptr;
    QVector<pointcloud::Point3D> m_resultDisplay;
    QVector<int> m_resultMapping;
    QSet<int> m_resultRemoved;
    quint64 m_cachedResultRevision=0;
    int m_workflowStage=0;
    quint64 m_lastWorkflowResultRevision=0;
    QVector<QToolButton *> m_modelSteps;
};
}
