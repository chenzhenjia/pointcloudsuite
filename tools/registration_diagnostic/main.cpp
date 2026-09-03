#include <pcv/interface/stitching_interface.h>
#include <pcv/io/ply_reader.h>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace {
struct PoseRecord { pointcloud::RobotPose start; pointcloud::RobotPose end; };

bool parsePoseLine(const QString &line, int *scan, PoseRecord *record)
{
    const QString n = QStringLiteral("([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)");
    const QRegularExpression rx(QStringLiteral("A(\\d+)\\s+Start\\s+X%1\\s+Y%1\\s+Z%1\\s+A%1\\s+B%1\\s+C%1\\s*;\\s*END\\s+X%1\\s+Y%1\\s+Z%1\\s+A%1\\s+B%1\\s+C%1").arg(n), QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(line.trimmed()); if (!m.hasMatch()) return false;
    *scan = m.captured(1).toInt();
    double *a[] = {&record->start.x,&record->start.y,&record->start.z,&record->start.rx,&record->start.ry,&record->start.rz,&record->end.x,&record->end.y,&record->end.z,&record->end.rx,&record->end.ry,&record->end.rz};
    for (int i=0;i<12;++i) { bool ok=false; *a[i]=m.captured(i+2).toDouble(&ok); if(!ok) return false; }
    return true;
}

bool readPoses(const QString &path, QHash<int,PoseRecord> *poses, QString *error)
{
    QFile f(path); if(!f.open(QIODevice::ReadOnly|QIODevice::Text)){*error=f.errorString();return false;}
    while(!f.atEnd()){int scan;PoseRecord r;if(!parsePoseLine(QString::fromUtf8(f.readLine()),&scan,&r))continue;if(poses->contains(scan)){*error=QStringLiteral("位姿文件包含重复扫描编号 A%1").arg(scan);return false;}poses->insert(scan,r);}
    if(poses->size()<2){*error=QStringLiteral("位姿文件中少于两组有效 Start/End");return false;} return true;
}

int scanIdFor(const QString &path)
{
    const auto m=QRegularExpression(QStringLiteral("A(\\d+)(?:\\.ply)?$"),QRegularExpression::CaseInsensitiveOption).match(QFileInfo(path).fileName());
    return m.hasMatch()?m.captured(1).toInt():-1;
}

QStringList collectInputs(const QCommandLineParser &p,const QCommandLineOption &input,const QCommandLineOption &dir,QString *error)
{
    QStringList files=p.values(input);
    if(p.isSet(dir)){QDir d(QFileInfo(p.value(dir)).absoluteFilePath());if(!d.exists()){*error=QStringLiteral("输入目录不存在：%1").arg(d.absolutePath());return{};}for(const QString &f:d.entryList({QStringLiteral("*.ply")},QDir::Files,QDir::Name))files<<d.absoluteFilePath(f);}
    QStringList out;QSet<QString> seen;for(const QString &v:files){const QString path=QFileInfo(v).absoluteFilePath(),key=QDir::cleanPath(path).toLower();if(seen.contains(key)){*error=QStringLiteral("同一 PLY 不能重复添加：%1").arg(path);return{};}seen.insert(key);out<<path;}
    if(out.isEmpty())*error=QStringLiteral("至少需要 --input 或 --input-dir"); return out;
}

QString formatName(pcv::detail::io::PlyFormat f){return f==pcv::detail::io::PlyFormat::Ascii?QStringLiteral("ascii"):f==pcv::detail::io::PlyFormat::BinaryLittleEndian?QStringLiteral("binary_little_endian"):QStringLiteral("binary_big_endian");}

int runInspect(const QCommandLineParser &p,const QCommandLineOption &input,const QCommandLineOption &dir,const QCommandLineOption &json,QTextStream &out,QTextStream &err)
{
    QString error;const QStringList files=collectInputs(p,input,dir,&error);if(files.isEmpty()){err<<error<<'\n';return 2;}QJsonArray records;bool all=true;
    for(const QString &path:files){const auto info=pcv::detail::io::inspectPlyHeader(path);const bool conversion=info.ok&&info.format!=pcv::detail::io::PlyFormat::BinaryLittleEndian;all=all&&info.ok;QJsonObject item{{QStringLiteral("file"),path},{QStringLiteral("ok"),info.ok},{QStringLiteral("format"),formatName(info.format)},{QStringLiteral("version"),info.version},{QStringLiteral("vertices"),info.declaredPointCount},{QStringLiteral("requires_conversion"),conversion},{QStringLiteral("canonical_format"),QStringLiteral("binary_little_endian")},{QStringLiteral("error"),info.error}};records.append(item);if(!p.isSet(json))out<<"file="<<path<<" ok="<<info.ok<<" format="<<formatName(info.format)<<" requires_conversion="<<conversion<<" canonical_format=binary_little_endian version="<<info.version<<" vertices="<<info.declaredPointCount<<" error="<<info.error<<'\n';}
    if(p.isSet(json))out<<QJsonDocument(records).toJson(QJsonDocument::Compact)<<'\n';return all?0:2;
}

void printResult(const pcv::interface::StitchingResult &r,const QString &label,QTextStream &out){out<<label<<" success="<<r.success<<" cancelled="<<r.cancelled<<" points="<<r.points.size()<<" error_code="<<r.errorCode<<" message="<<r.message<<"\n";if(!r.outputPly.isEmpty())out<<"formal_output="<<r.outputPly<<"\n";if(!r.diagnostics.isEmpty())out<<r.diagnostics;}

}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QStringList args=app.arguments();const bool inspect=args.size()>1&&args[1]==QStringLiteral("inspect");if(inspect|| (args.size()>1&&args[1]==QStringLiteral("stitch")))args.removeAt(1);
    QCommandLineParser p;p.addHelpOption();QCommandLineOption input(QStringList{QStringLiteral("input")},QStringLiteral("PLY 文件，可重复"),QStringLiteral("file")),dir(QStringList{QStringLiteral("input-dir")},QStringLiteral("PLY 目录"),QStringLiteral("directory")),pose(QStringList{QStringLiteral("pose-info")},QStringLiteral("Start/End 位姿 TXT"),QStringLiteral("txt")),cal(QStringList{QStringLiteral("calibration")},QStringLiteral("Eye-in-hand XML"),QStringLiteral("xml")),outOpt(QStringList{QStringLiteral("output-dir")},QStringLiteral("输出目录"),QStringLiteral("directory")),json(QStringList{QStringLiteral("json")},QStringLiteral("inspect 输出 JSON"));p.addOption(input);p.addOption(dir);p.addOption(pose);p.addOption(cal);p.addOption(outOpt);p.addOption(json);p.process(args);QTextStream out(stdout),err(stderr);
    if(inspect)return runInspect(p,input,dir,json,out,err);
    QString error;const QStringList files=collectInputs(p,input,dir,&error);if(files.size()<2||!p.isSet(pose)||!p.isSet(cal)||!p.isSet(outOpt)){err<<"Usage: registration_diagnostic stitch --input <file>... --input-dir <directory> --pose-info <txt> --calibration <xml> --output-dir <directory>\n";return 2;}QHash<int,PoseRecord> poses;if(!readPoses(p.value(pose),&poses,&error)){err<<error<<'\n';return 2;}for(const QString &f:files){if(!QFileInfo::exists(f)||!poses.contains(scanIdFor(f))){err<<"PLY 文件或对应位姿不存在："<<f<<'\n';return 2;}}const QString cp=QFileInfo(p.value(cal)).absoluteFilePath();if(!QFileInfo::exists(cp)){err<<"Calibration XML does not exist: "<<cp<<'\n';return 2;}const QString od=QFileInfo(p.value(outOpt)).absoluteFilePath();if(!QDir().mkpath(od)){err<<"Could not create output directory: "<<od<<'\n';return 2;}
    auto run=[&](bool seam){pcv::interface::StitchingOptions o;o.calibrationPath=cp;o.outputDirectory=QDir(od).filePath(seam?QStringLiteral("seam"):QStringLiteral("no-seam"));o.sampleStride=8;o.seamEnabled=seam;for(const QString &f:files){pcv::interface::StitchingFrameInput frame;frame.plyPath=f;frame.startPose=poses[scanIdFor(f)].start;frame.endPose=poses[scanIdFor(f)].end;o.frames<<frame;}return pcv::interface::stitchRawLineProfiles(o);};const auto a=run(false);printResult(a,QStringLiteral("NO_SEAM"),out);const auto b=run(true);printResult(b,QStringLiteral("SEAM"),out);return a.success&&b.success?0:2;
}
