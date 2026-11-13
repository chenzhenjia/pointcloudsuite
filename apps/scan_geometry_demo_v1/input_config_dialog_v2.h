#pragma once
#include "scan_geometry_session_v2.h"
#include <QDialog>
#include <memory>
class QGroupBox;
class QLineEdit;
class QDoubleSpinBox;
namespace Ui {class InputConfigDialogV2;}
namespace pcv::demo {
class InputConfigDialogV2 final : public QDialog {
public:
    explicit InputConfigDialogV2(const InputConfigV2 &config,QWidget *parent=nullptr);
    ~InputConfigDialogV2() override;
    InputConfigV2 configuration() const {return m_config;}
    void addScanJson();
private:
    void addFiles();
    void addDepthFrame();
    void moveRow(int delta);
    void acceptInput();
    void append(const InputFrameV2 &frame,bool posesProvided);
    std::unique_ptr<Ui::InputConfigDialogV2> ui;
    InputConfigV2 m_config;
    QGroupBox *m_contractGroup=nullptr;
    QLineEdit *m_runtimeRoot=nullptr, *m_jobId=nullptr, *m_runId=nullptr, *m_stepId=nullptr;
    QLineEdit *m_workpieceId=nullptr, *m_controllerId=nullptr, *m_scanOutputRef=nullptr;
    QDoubleSpinBox *m_maskPixel=nullptr;
};
}
