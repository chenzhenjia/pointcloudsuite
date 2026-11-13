#include "step_template_builder_window_v1.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QCommandLineParser>
#include <QTimer>
int main(int argc,char **argv){
    qputenv("QT_OPENGL","desktop");QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc,argv);application.setApplicationName("step_template_builder_v1");
    QCommandLineParser parser;parser.setApplicationDescription(QStringLiteral("STEP 数模模板制作"));parser.addHelpOption();
    QCommandLineOption stepOption(QStringLiteral("step"),QStringLiteral("启动后导入 STEP 文件"),QStringLiteral("file"));parser.addOption(stepOption);parser.process(application);
    pcv::stepbuilder::StepTemplateBuilderWindowV1 window;window.show();
    if(qEnvironmentVariableIsSet("PCV_STEP_TEMPLATE_SELFTEST_CLOSE"))
        QTimer::singleShot(800,&application,&QCoreApplication::quit);
    if(parser.isSet(stepOption)){
        const QString file=parser.value(stepOption);
        QTimer::singleShot(0,&window,[&window,file]{window.openStepFile(file);});
    }
    return application.exec();
}
