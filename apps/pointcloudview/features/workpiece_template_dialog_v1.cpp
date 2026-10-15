#include "workpiece_template_dialog_v1.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

WorkpieceTemplateDialogV1::WorkpieceTemplateDialogV1(
    const pcv::workpiece::WorkpieceTemplateInfo &summary, const QString &root, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("workpiece_template_create_dialog_v1"));
    setWindowTitle(tr("创建工件模板"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(summary.name, this);
    m_name->setObjectName(QStringLiteral("template_name"));
    m_root = new QLineEdit(root, this);
    m_root->setObjectName(QStringLiteral("template_output_root"));
    auto *choose = new QPushButton(tr("选择目录…"), this);
    auto *directory = new QHBoxLayout;
    directory->addWidget(m_root);
    directory->addWidget(choose);
    form->addRow(tr("工件名称"), m_name);
    form->addRow(tr("保存根目录"), directory);
    const QString bounds = tr("X [%1, %2]  Y [%3, %4]  Z [%5, %6] mm")
        .arg(summary.minimum.x(), 0, 'g', 8).arg(summary.maximum.x(), 0, 'g', 8)
        .arg(summary.minimum.y(), 0, 'g', 8).arg(summary.maximum.y(), 0, 'g', 8)
        .arg(summary.minimum.z(), 0, 'g', 8).arg(summary.maximum.z(), 0, 'g', 8);
    form->addRow(tr("全部点数"), new QLabel(QLocale().toString(summary.pointCount), this));
    form->addRow(tr("坐标 / 单位"), new QLabel(QStringLiteral("robot_base / mm"), this));
    auto *boundsLabel = new QLabel(bounds, this);
    boundsLabel->setWordWrap(true);
    form->addRow(tr("包围盒"), boundsLabel);
    QStringList sourcePaths;
    for (const QJsonValue &value : summary.source.value(QStringLiteral("files")).toArray())
        sourcePaths.append(value.toString());
    const QString manifest = summary.source.value(QStringLiteral("manifest")).toString();
    if (!manifest.isEmpty()) sourcePaths.append(manifest);
    auto *source = new QLabel(summary.source.value(QStringLiteral("entry")).toString()
        + QStringLiteral("\n") + sourcePaths.join(QLatin1Char('\n')), this);
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("来源"), source);
    layout->addLayout(form);
    auto *note = new QLabel(tr("保存当前全部 XYZ 和已有法向，每次创建新版本。\n"
        "遮挡、未扫描表面和区域内背景会保留；不自动检查几何质量。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    m_confirm = new QCheckBox(tr("确认当前整幅点云就是目标工件的基准状态"), this);
    m_confirm->setObjectName(QStringLiteral("template_baseline_confirmed"));
    layout->addWidget(m_confirm);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(QStringLiteral("template_buttons"));
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_root, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_confirm, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("选择模板保存根目录"), outputRoot());
        if (!path.isEmpty()) m_root->setText(path);
    });
    resize(660, sizeHint().height());
    refresh();
}

QString WorkpieceTemplateDialogV1::templateName() const { return m_name->text(); }
QString WorkpieceTemplateDialogV1::outputRoot() const { return m_root->text().trimmed(); }
void WorkpieceTemplateDialogV1::refresh()
{
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(m_confirm->isChecked()
        && !templateName().trimmed().isEmpty() && !outputRoot().isEmpty());
}
