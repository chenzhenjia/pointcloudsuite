#pragma once
#include "scan_geometry_session_v2.h"
#include <QDialog>
#include <memory>
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
};
}
