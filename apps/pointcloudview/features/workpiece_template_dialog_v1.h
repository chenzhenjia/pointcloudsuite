#pragma once

#include <pcv/workpiece/workpiece_template_v1.h>
#include <QDialog>

class QLineEdit;
class QCheckBox;
class QDialogButtonBox;

class WorkpieceTemplateDialogV1 final : public QDialog {
public:
    WorkpieceTemplateDialogV1(const pcv::workpiece::WorkpieceTemplateInfo &summary,
                            const QString &outputRoot, QWidget *parent = nullptr);
    QString templateName() const;
    QString outputRoot() const;
private:
    void refresh();
    QLineEdit *m_name;
    QLineEdit *m_root;
    QCheckBox *m_confirm;
    QDialogButtonBox *m_buttons;
};
