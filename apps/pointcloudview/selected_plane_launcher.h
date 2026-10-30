#pragma once
class QWidget;
class QDialog;
// Keep legacy processor's same-named ICP DTOs out of the module-30 TU.
void showSelectedPlaneDialog(float surfaceToleranceMm, QWidget *parent);
QDialog *createSelectedPlanePage(float surfaceToleranceMm, QWidget *parent);
void cancelSelectedPlanePage(QDialog *page);
