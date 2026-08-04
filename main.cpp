#include "mainwindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    if (qEnvironmentVariableIsSet("POINTCLOUDVIEW_SELFTEST_CLOSE")) {
        QTimer::singleShot(100, &w, &QWidget::close);
    }
    return QApplication::exec();
}
