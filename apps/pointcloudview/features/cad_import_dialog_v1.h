#pragma once
#include <pcv/workpiece/cad_template_v1.h>
#include <QDialog>
#include <QFutureWatcher>
#include <atomic>
class QTableWidget;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;
namespace pcv::render { class CadMeshCanvasV1; }
class CadImportDialogV1 final : public QDialog {
public:
    CadImportDialogV1(const QString &path,QWidget *parent=nullptr);
    ~CadImportDialogV1() override;
    const pcv::workpiece::CadCloudResult &result() const { return m_result; }
    void cancelAndWait();
    void reject() override;
private:
    friend struct CadImportDialogTestAccess;
    void refreshSelection();
    void generate();
    std::shared_ptr<std::atomic_bool> m_cancel;
    struct Progress {
        std::atomic<pcv::workpiece::CadStage> stage{pcv::workpiece::CadStage::Reading};
        std::atomic<qint64> done{0},total{0};
    };
    std::shared_ptr<Progress> m_progress;
    QFutureWatcher<pcv::workpiece::StepModelResult> m_read;
    QFutureWatcher<pcv::workpiece::CadCloudResult> m_generate;
    pcv::workpiece::StepModelResult m_model;
    pcv::workpiece::CadCloudResult m_result;
    pcv::render::CadMeshCanvasV1 *m_canvas;
    QTableWidget *m_faces;
    QDoubleSpinBox *m_spacing;
    QLabel *m_info,*m_estimate,*m_status;
    QPushButton *m_generateButton,*m_all,*m_clear,*m_cancelTask;
    QTimer *m_timer;
    bool m_busy=true,m_closing=false;
};
