#include "pointcloudprocessor.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTextStream>
#include <QVector3D>
#include <QMatrix4x4>
#include <cmath>

static QMatrix4x4 pose(double x, double y, double z, double rx, double ry, double rz) {
    QMatrix4x4 m; m.setToIdentity();
    m.rotate(float(rz), 0, 0, 1); m.rotate(float(ry), 0, 1, 0); m.rotate(float(rx), 1, 0, 0);
    m(0, 3) = float(x); m(1, 3) = float(y); m(2, 3) = float(z); return m;
}

static void saveTopView(const QVector<pointcloud::Point3D> &points,
                        const QVector<int> &cloudIds, const QString &path) {
    if (points.isEmpty()) return;
    QVector3D lo(1e30f,1e30f,1e30f), hi(-1e30f,-1e30f,-1e30f);
    for (const auto &p : points) if (std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)) {
        lo.setX(std::min(lo.x(),p.x)); lo.setY(std::min(lo.y(),p.y)); lo.setZ(std::min(lo.z(),p.z));
        hi.setX(std::max(hi.x(),p.x)); hi.setY(std::max(hi.y(),p.y)); hi.setZ(std::max(hi.z(),p.z));
    }
    const int w=1800,h=1200,pad=8; QImage image(w,h,QImage::Format_RGB32); image.fill(qRgb(0,0,0));
    const float sx=(hi.x()-lo.x())>1e-6f ? float(w-2*pad)/(hi.x()-lo.x()) : 1.0f;
    const float sy=(hi.y()-lo.y())>1e-6f ? float(h-2*pad)/(hi.y()-lo.y()) : 1.0f;
    const float scale=std::min(sx,sy);
    const QRgb colors[4]={qRgb(220,220,220),qRgb(255,120,80),qRgb(100,220,140),qRgb(100,160,255)};
    for (int i=0;i<points.size();++i) { const auto &p=points[i]; if (!std::isfinite(p.x)||!std::isfinite(p.y)) continue;
        const int x=qBound(0,int((p.x-lo.x())*scale)+pad,w-1); const int y=qBound(0,h-1-(int((p.y-lo.y())*scale)+pad),h-1);
        image.setPixel(x,y,colors[qBound(0,cloudIds.value(i),3)]);
    }
    image.save(path);
}

static void reportCloudStats(const pointcloud::WorldCloudMergeResult &r, QTextStream &out) {
    for (int id=0; id<4; ++id) {
        QVector3D lo(1e30f,1e30f,1e30f), hi(-1e30f,-1e30f,-1e30f); qsizetype count=0;
        for (int i=0;i<r.points.size();++i) if (r.cloudIds.value(i)==id) { const auto &p=r.points[i];
            lo.setX(std::min(lo.x(),p.x)); lo.setY(std::min(lo.y(),p.y)); lo.setZ(std::min(lo.z(),p.z));
            hi.setX(std::max(hi.x(),p.x)); hi.setY(std::max(hi.y(),p.y)); hi.setZ(std::max(hi.z(),p.z)); ++count; }
        out << "cloud " << id+1 << " count=" << count
            << " bounds=" << lo.x() << "," << lo.y() << "," << lo.z()
            << " .. " << hi.x() << "," << hi.y() << "," << hi.z() << "\n";
    }
    for (const auto &d : r.icpDiagnostics)
        out << "icp " << d.filePath << " attempted=" << d.attempted
            << " accepted=" << d.accepted << " corr=" << d.correspondences
            << " fitness=" << d.fitness << " rmse=" << d.rmse
            << " overlap=" << d.overlapRatio << " reason=" << d.reason << "\n";
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PointCloudSuite registration diagnostic"));
    parser.addHelpOption();
    const QCommandLineOption inputDirectoryOption(
        QStringList{QStringLiteral("input-dir")}, QStringLiteral("Directory containing Point_Cloud_A01.ply through Point_Cloud_A04.ply."), QStringLiteral("directory"));
    const QCommandLineOption outputDirectoryOption(
        QStringList{QStringLiteral("output-dir")}, QStringLiteral("Directory for diagnostic PNG output; defaults to the current directory."), QStringLiteral("directory"));
    parser.addOption(inputDirectoryOption);
    parser.addOption(outputDirectoryOption);
    parser.process(app);
    const QString dir = parser.isSet(inputDirectoryOption)
        ? parser.value(inputDirectoryOption).trimmed()
        : qEnvironmentVariable("PCV_REGISTRATION_DIAGNOSTIC_INPUT_DIR").trimmed();
    if (dir.isEmpty()) {
        QTextStream(stderr) << "Missing --input-dir. Set --input-dir <directory> or PCV_REGISTRATION_DIAGNOSTIC_INPUT_DIR.\n";
        return 2;
    }
    const QString outputDirectory = parser.isSet(outputDirectoryOption)
        ? parser.value(outputDirectoryOption).trimmed() : QDir::currentPath();
    if (!QDir().mkpath(outputDirectory)) {
        QTextStream(stderr) << "Could not create output directory: " << outputDirectory << '\n';
        return 2;
    }
    const QStringList names = {"Point_Cloud_A01.ply","Point_Cloud_A02.ply","Point_Cloud_A03.ply","Point_Cloud_A04.ply"};
    const double xs[] = {500,600,700,800}; QVector<pointcloud::WorldCloudInput> inputs;
    // RTmatDepth2robot / DepthInRobotPose from the supplied Eye-in-Hand XML.
    QMatrix4x4 hand; hand.setToIdentity();
    hand(0,0)=0.99989712774722139f; hand(0,1)=-0.013419399546123298f; hand(0,2)=-0.0050649421199512445f;
    hand(1,0)=-0.013405969174753618f; hand(1,1)=-0.999906554241616f; hand(1,2)=0.0026763361417942795f;
    hand(2,0)=-0.0051003836466001415f; hand(2,1)=-0.0026081603631342f; hand(2,2)=-0.999983591658472f;
    hand(0,3)=109.49121811215774f; hand(1,3)=-76.4519295996864f; hand(2,3)=253.3476715717871f;
    for (int i=0;i<4;++i) { pointcloud::WorldCloudInput in; in.filePath=dir+'/'+names[i];
        in.startBaseFromFlange=pose(xs[i],150,700,0,0,180); in.endBaseFromFlange=pose(xs[i],-150,700,0,0,180);
        in.flangeFromDepth=hand; inputs.push_back(in); }
    QTextStream out(stdout);
    pointcloud::IcpOptions noIcp; noIcp.enabled=false;
    const auto result=pointcloud::mergePlyCloudsInWorld(inputs,noIcp);
    out << "NO_ICP ok=" << result.ok << " points=" << result.points.size() << " error=" << result.error << "\n";
    out << result.diagnostics << "\n"; reportCloudStats(result,out);
    saveTopView(result.points,result.cloudIds,QDir(outputDirectory).filePath(QStringLiteral("Point_Cloud_A_robot_world_top.png")));
    pointcloud::IcpOptions withIcp; withIcp.enabled=true;
    withIcp.maximumCorrectionTranslation=10.0f; withIcp.maximumCorrectionAngleDegrees=2.0f;
    const auto refined=pointcloud::mergePlyCloudsInWorld(inputs,withIcp);
    out << "WITH_ICP ok=" << refined.ok << " points=" << refined.points.size() << " error=" << refined.error << "\n";
    out << refined.diagnostics << "\n"; reportCloudStats(refined,out);
    saveTopView(refined.points,refined.cloudIds,QDir(outputDirectory).filePath(QStringLiteral("Point_Cloud_A_robot_world_icp_top.png")));
    QVector<pointcloud::WorldCloudInput> fixedInputs = inputs;
    for (auto &in : fixedInputs) { const QMatrix4x4 mid = pose(xs[0],0,700,0,0,180); Q_UNUSED(mid); }
    for (int i=0;i<fixedInputs.size();++i) {
        const QMatrix4x4 mid = pose(xs[i],0,700,0,0,180);
        fixedInputs[i].startBaseFromFlange = mid; fixedInputs[i].endBaseFromFlange = mid;
    }
    const auto fixed = pointcloud::mergePlyCloudsInWorld(fixedInputs,noIcp);
    out << "FIXED_MIDPOINT ok=" << fixed.ok << " points=" << fixed.points.size() << " error=" << fixed.error << "\n";
    out << fixed.diagnostics << "\n"; reportCloudStats(fixed,out);
    saveTopView(fixed.points,fixed.cloudIds,QDir(outputDirectory).filePath(QStringLiteral("Point_Cloud_A_robot_world_fixed_mid_top.png")));
    QVector<pointcloud::WorldCloudInput> swapped = inputs;
    for (auto &in : swapped) std::swap(in.startBaseFromFlange, in.endBaseFromFlange);
    const auto swappedResult = pointcloud::mergePlyCloudsInWorld(swapped,noIcp);
    out << "SWAPPED_START_END ok=" << swappedResult.ok << " points=" << swappedResult.points.size() << " error=" << swappedResult.error << "\n";
    out << swappedResult.diagnostics << "\n"; reportCloudStats(swappedResult,out);
    saveTopView(swappedResult.points,swappedResult.cloudIds,QDir(outputDirectory).filePath(QStringLiteral("Point_Cloud_A_robot_world_swapped_top.png")));
    return result.ok ? 0 : 2;
}





