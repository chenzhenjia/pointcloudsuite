#include "renderdialog.h"
#include "ui_renderdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

RenderDialog::RenderDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::RenderDialog)
{
    ui->setupUi(this);
    const auto notify = [this]() { emit settingsChanged(); };
    connect(ui->spb_point_size, qOverload<int>(&QSpinBox::valueChanged), this, notify);
    connect(ui->cb_color_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, notify);
    connect(ui->dsb_overlay, qOverload<double>(&QDoubleSpinBox::valueChanged), this, notify);
    connect(ui->dsb_map_min, qOverload<double>(&QDoubleSpinBox::valueChanged), this, notify);
    connect(ui->dsb_map_max, qOverload<double>(&QDoubleSpinBox::valueChanged), this, notify);
    for (QCheckBox *checkBox : {ui->chk_show_axes, ui->chk_show_floor_grid,
                                ui->chk_floor_z_up, ui->chk_show_axis_labels,
                                ui->chk_show_normals, ui->chk_show_bbox,
                                ui->chk_show_statistics}) {
        connect(checkBox, &QCheckBox::toggled, this, notify);
    }
    connect(ui->btn_reset_view, &QPushButton::clicked, this, &RenderDialog::resetViewRequested);
}

RenderDialog::~RenderDialog()
{
    delete ui;
}

int RenderDialog::pointSize() const { return ui->spb_point_size->value(); }
int RenderDialog::colorMode() const { return ui->cb_color_mode->currentIndex(); }
double RenderDialog::overlay() const { return ui->dsb_overlay->value(); }
double RenderDialog::mapMinimum() const { return ui->dsb_map_min->value(); }
double RenderDialog::mapMaximum() const { return ui->dsb_map_max->value(); }

pcv::render::RenderOverlayOptions RenderDialog::overlayOptions() const
{
    pcv::render::RenderOverlayOptions options;
    options.showCoordinateAxes = ui->chk_show_axes->isChecked();
    options.showFloorGrid = ui->chk_show_floor_grid->isChecked();
    options.floorZUp = ui->chk_floor_z_up->isChecked();
    options.showAxisLabels = ui->chk_show_axis_labels->isChecked();
    options.showNormals = ui->chk_show_normals->isChecked();
    options.showBoundingBox = ui->chk_show_bbox->isChecked();
    options.showStatistics = ui->chk_show_statistics->isChecked();
    return options;
}

void RenderDialog::setDisplaySettings(int pointSize, int colorMode, double overlay,
                                      double mapMinimum, double mapMaximum)
{
    const QSignalBlocker pointSizeBlocker(ui->spb_point_size);
    const QSignalBlocker colorModeBlocker(ui->cb_color_mode);
    const QSignalBlocker overlayBlocker(ui->dsb_overlay);
    const QSignalBlocker mapMinimumBlocker(ui->dsb_map_min);
    const QSignalBlocker mapMaximumBlocker(ui->dsb_map_max);
    ui->spb_point_size->setValue(pointSize);
    ui->cb_color_mode->setCurrentIndex(colorMode);
    ui->dsb_overlay->setValue(overlay);
    ui->dsb_map_min->setValue(mapMinimum);
    ui->dsb_map_max->setValue(mapMaximum);
}

void RenderDialog::setOverlayOptions(const pcv::render::RenderOverlayOptions &options)
{
    const QSignalBlocker axesBlocker(ui->chk_show_axes);
    const QSignalBlocker floorBlocker(ui->chk_show_floor_grid);
    const QSignalBlocker zUpBlocker(ui->chk_floor_z_up);
    const QSignalBlocker labelsBlocker(ui->chk_show_axis_labels);
    const QSignalBlocker normalsBlocker(ui->chk_show_normals);
    const QSignalBlocker bboxBlocker(ui->chk_show_bbox);
    const QSignalBlocker statisticsBlocker(ui->chk_show_statistics);
    ui->chk_show_axes->setChecked(options.showCoordinateAxes);
    ui->chk_show_floor_grid->setChecked(options.showFloorGrid);
    ui->chk_floor_z_up->setChecked(options.floorZUp);
    ui->chk_show_axis_labels->setChecked(options.showAxisLabels);
    ui->chk_show_normals->setChecked(options.showNormals);
    ui->chk_show_bbox->setChecked(options.showBoundingBox);
    ui->chk_show_statistics->setChecked(options.showStatistics);
}

void RenderDialog::setContentText(const QString &text)
{
    ui->pte_content->setPlainText(text);
}
