#include "intensityviewerwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PointCloudSuite"));
    QCoreApplication::setApplicationName(QStringLiteral("intensity_viewer"));

    IntensityViewerWindow window;
    window.show();
    return application.exec();
}
