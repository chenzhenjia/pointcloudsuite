#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QStackedWidget>
#include <iostream>
#include "step_template_builder_window_v1.h"
int main(int argc,char **argv){
    QApplication app(argc,argv);
    pcv::stepbuilder::StepTemplateBuilderWindowV1 window;
    auto *pages=window.findChild<QStackedWidget*>("pages");auto *name=window.findChild<QLineEdit*>("workpieceName");auto *next=window.findChild<QPushButton*>("next");auto *import=window.findChild<QPushButton*>("importStep");
    if(!pages||!name||!next||!import||pages->count()!=3)return 1;
    if(next->isEnabled())return 2;
#ifndef PCV_BUILD_STEP_IMPORT
    if(import->isEnabled())return 3;
#endif
    name->setText(QStringLiteral("测试工件"));
#ifdef PCV_BUILD_STEP_IMPORT
    if(!next->isEnabled())return 4;
    next->click();if(pages->currentIndex()!=1)return 5;
#else
    if(next->isEnabled())return 4;
#endif
    if(window.findChild<QPushButton*>("saveDraft")->isEnabled())return 6;
    return 0;
}
