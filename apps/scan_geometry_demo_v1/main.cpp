#include "scan_geometry_window_v1.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char **argv) {
    qputenv("QT_OPENGL", "desktop");
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format; format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("PointCloudSuite"); app.setApplicationName("scan_geometry_demo_v1");
    pcv::demo::ScanGeometryWindowV1 window;
    window.show();
    if (qEnvironmentVariableIsSet("PCV_SCAN_GEOMETRY_SELFTEST_CLOSE")) QTimer::singleShot(800, &app, &QCoreApplication::quit);
    return app.exec();
}
