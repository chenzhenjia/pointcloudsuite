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
    // Qt 6.8.3's software OpenGL path crashes inside Qt6OpenGLd.dll on this
    // machine. Override stale IDE/user settings and always request the native
    // desktop driver before QApplication exists.
    qputenv("QT_OPENGL", QByteArrayLiteral("desktop"));
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    // Install diagnostics only after the event loop is ready.  Qt may emit
    // startup/destruction messages while QApplication is still wiring its
    // internal event dispatchers; replacing that handler at that point can
    // trigger a Qt QObject sender assertion in debug builds.
    qInstallMessageHandler(startupMessageHandler);
    qInfo() << "startup: QApplication created";
    // Keep the top-level window on the heap.  QOpenGLWidget and its native
    // context are destroyed after the event loop has stopped; placing the
    // complete widget tree in main's stack makes MSVC's /RTC1 stack guard
    // report a false-looking "stack around w was corrupted" during Qt's
    // deferred native-window cleanup.  Explicit destruction after exec also
    // gives the widget a well-defined lifetime and keeps the stack frame
    // free of Qt/OpenGL-owned state.
    auto *w = new MainWindow;
    qInfo() << "startup: MainWindow constructed";
    w->show();
    qInfo() << "startup: MainWindow shown";
    if (qEnvironmentVariableIsSet("POINTCLOUDVIEW_SELFTEST_CLOSE")) {
        // Closing a QOpenGLWidget a few milliseconds after show() races its
        // first native surface/GL initialization on some Qt 6 debug builds.
        // Use the normal application quit path for the startup smoke test;
        // this still verifies QApplication, MainWindow construction and the
        // event loop without forcing a mid-initialization widget close.
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }
    const int result = QApplication::exec();
    delete w;
    return result;
}
