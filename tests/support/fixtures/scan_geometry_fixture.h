#pragma once
#include "scan_geometry_session_v2.h"
#include <QFile>
#include <QDir>

// Synthetic input is confined to tests. Production always reads user PLYs.
inline bool writeScanGeometryFixtureV2(const QString &root,int count,pcv::demo::InputConfigV2 *config) {
    config->calibration=QDir(root).filePath("handeye.xml");config->outputRoot=QDir(root).filePath("output");
    QFile xml(config->calibration);if(!xml.open(QIODevice::WriteOnly))return false;
    const QByteArray calibration="<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>";
    if(xml.write(calibration)!=calibration.size())return false;xml.close();
    QByteArray ply="ply\nformat ascii 1.0\nelement vertex 625\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
    for(int y=0;y<25;++y)for(int x=0;x<25;++x)ply+=QByteArray::number(x*0.5)+' '+QByteArray::number(y*0.5)+" 30\n";
    for(int i=0;i<count;++i){pcv::demo::InputFrameV2 frame;frame.path=QDir(root).filePath(QString("frame_%1.ply").arg(i));frame.end.y=12;
        QFile file(frame.path);if(!file.open(QIODevice::WriteOnly)||file.write(ply)!=ply.size())return false;config->frames.append(frame);}
    return true;
}
