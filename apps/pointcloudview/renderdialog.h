#pragma once

#include <pcv/render/pointcloud_canvas_contract.h>

#include <QDialog>

class QString;

namespace Ui { class RenderDialog; }

class RenderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit RenderDialog(QWidget *parent = nullptr);
    ~RenderDialog() override;

    int pointSize() const;
    int colorMode() const;
    double overlay() const;
    double mapMinimum() const;
    double mapMaximum() const;
    pcv::render::RenderOverlayOptions overlayOptions() const;

    void setDisplaySettings(int pointSize, int colorMode, double overlay,
                            double mapMinimum, double mapMaximum);
    void setOverlayOptions(const pcv::render::RenderOverlayOptions &options);
    void setContentText(const QString &text);

signals:
    void settingsChanged();
    void resetViewRequested();

private:
    Ui::RenderDialog *ui = nullptr;
};
