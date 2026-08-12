#include "mainwindow.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

namespace {
void startupMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message) {
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/startup.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << QLatin1Char(' ') << message << Qt::endl;
    }
}
}

#ifdef Q_OS_WIN
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[])
{
    qInstallMessageHandler(startupMessageHandler);
    qInfo() << "startup: entering main";
    // This Qt 6.8.3 installation crashes while creating QOpenGLWidget with
    // QT_OPENGL=software. Use desktop OpenGL unless explicitly overridden.
    const QByteArray backend = qgetenv("QT_OPENGL").trimmed().toLower();
    if (backend.isEmpty()) {
        qputenv("QT_OPENGL", QByteArrayLiteral("desktop"));
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    } else if (backend == "desktop") {
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    } else if (backend == "software") {
        QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    qInfo() << "startup: QApplication created";
    MainWindow w;
    qInfo() << "startup: MainWindow constructed";
    w.show();
    qInfo() << "startup: MainWindow shown";
    if (qEnvironmentVariableIsSet("POINTCLOUDVIEW_SELFTEST_CLOSE")) {
        QTimer::singleShot(100, &w, &QWidget::close);
    }
    return QApplication::exec();
}
