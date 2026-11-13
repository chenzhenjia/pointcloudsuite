#pragma once
#include "step_template_builder_session_v1.h"
#include <pcv/render/pointcloud_canvas.h>
#include <QMainWindow>
#include <QRubberBand>
#include <memory>
namespace Ui { class StepTemplateBuilderWindowV1; }
namespace pcv::stepbuilder {
class StepTemplateBuilderWindowV1 final : public QMainWindow {
public:
    explicit StepTemplateBuilderWindowV1(QWidget *parent=nullptr);
    ~StepTemplateBuilderWindowV1() override;
    void openStepFile(const QString &file);
protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;
private:
    friend struct StepTemplateBuilderViewTestAccessV1;
    void updateUi();
    void updateDisplay();
    void updateColors();
    void refreshFaces();
    void pickFace(int faceId);
    void setPage(int page);
    void clearRectangle();
    void setTool(int tool);
    std::unique_ptr<Ui::StepTemplateBuilderWindowV1> ui;
    StepTemplateBuilderSessionV1 m_session;
    PointCloudCanvas *m_canvas=nullptr;
    QRubberBand *m_rubberBand=nullptr;
    QPoint m_start;
    QRect m_rectangle;
    QSet<int> m_coloredWork,m_coloredInterference;
    quint64 m_coloredDisplayRevision=0;
    bool m_haveColors=false;
    bool m_dragging=false;
    int m_page=0,m_tool=0;
};
}
