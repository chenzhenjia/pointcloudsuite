#include "stitchingwindow.h"
#include "regressionrunner.h"

#include <QApplication>
#include <QByteArray>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("pointcloudstitch"));
    QApplication::setOrganizationName(QStringLiteral("PointCloudSuite"));

    if (application.arguments().contains(QStringLiteral("--regression")))
        return runRegression(application.arguments());

    StitchingWindow window;
    window.show();
    if (qEnvironmentVariableIsSet("POINTCLOUDSTITCH_SELFTEST_CLOSE")
        || application.arguments().contains(QStringLiteral("--selftest-close")))
        QTimer::singleShot(500, &application, &QCoreApplication::quit);
    return QApplication::exec();
}
