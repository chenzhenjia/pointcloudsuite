#include <pcv/workpiece/template_package_v1.h>
#include <pcv/io/ply_reader.h>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
namespace pcv::workpiece {
QString templatePointIdentityV1(const QVector<pointcloud::Point3D> &points,const std::function<bool()> &cancel) {
    QCryptographicHash hash(QCryptographicHash::Sha256);QByteArray bytes;bytes.reserve(4096*24);
    for(qsizetype offset=0;offset<points.size();offset+=4096){if(cancel&&cancel())return {};
        bytes.clear();QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for(qsizetype i=offset;i<std::min(offset+4096,points.size());++i){const auto &p=points.at(i);stream<<p.x<<p.y<<p.z<<p.nx<<p.ny<<p.nz;}hash.addData(bytes);}
    return QString::fromLatin1(hash.result().toHex());
}
TemplatePackageV1 generateStepTemplatePackageV1(const StepModelResult &model,const QSet<int> &work,const QSet<int> &interference,double spacing,const TemplateFeatureOptionsV1 &o) {
    TemplatePackageV1 out;QElapsedTimer timer;timer.start();
    if(!model.success||!model.model||work.isEmpty()||work.intersects(interference)){out.error=QStringLiteral("请选择互斥的工件解析平面与干涉解析平面");return out;}
    auto all=work;all.unite(interference);for(int id:all){auto it=std::find_if(model.faces.cbegin(),model.faces.cend(),[&](auto f){return f.id==id;});if(it==model.faces.cend()||!it->planar){out.error=QStringLiteral("只允许选择解析平面");return out;}}
    GenerateCadTemplateRequest request;request.model=model.model;request.faceIds=all.values();request.spacingMm=spacing;request.isCancelled=o.isCancelled;
    auto cloud=generateCadTemplateCloud(request);out.timings.insert("sampling_ms",timer.nsecsElapsed()/1e6);if(!cloud.success){out.error=cloud.message;out.cancelled=cloud.cancelled;return out;}
    QElapsedTimer regions;regions.start();
    auto features=cadTemplateFeaturesV1(model,cloud,interference,o.isCancelled);if(!features.ok){out.error=features.error;out.cancelled=features.cancelled;return out;}
    qsizetype workCount=0,interferenceCount=0;for(const auto &p:features.planes)(p.interference?interferenceCount:workCount)+=p.count;
    out.points.reserve(workCount);out.interferencePoints.reserve(interferenceCount);
    QJsonArray selected;
    for(auto p:features.planes){if(o.isCancelled&&o.isCancelled()){out.cancelled=true;out.error=QStringLiteral("已取消");return out;}
        auto &destination=p.interference?out.interferencePoints:out.points;const auto originalFirst=p.first;p.first=destination.size();
        for(qsizetype i=originalFirst;i<originalFirst+p.count;++i)destination.append(cloud.points.at(i));p.indices.clear();out.planes.append(std::move(p));}
    out.source=cloud.source;auto cad=out.source.value("cad").toObject();for(auto v:cad.value("selected_faces").toArray())if(work.contains(v.toObject().value("id").toInt()))selected.append(v);
    cad.insert("selected_faces",selected);cad.insert("generated_point_count",qint64(out.points.size()));out.source.insert("cad",cad);
    out.timings.insert("regions_split_ms",regions.nsecsElapsed()/1e6);QElapsedTimer digest;digest.start();
    out.coordinateFrame=QStringLiteral("cad_model");out.identity=templatePointIdentityV1(out.points,o.isCancelled);out.timings.insert("identity_ms",digest.nsecsElapsed()/1e6);
    out.cancelled=o.isCancelled&&o.isCancelled();out.ok=!out.cancelled;out.timings.insert("generate_package_ms",timer.nsecsElapsed()/1e6);return out;
}
WorkpieceTemplateResult saveTemplatePackageV1(const TemplatePackageV1 &p,QString name,QString root,const std::function<bool()> &cancel) {
    if(!p.ok){WorkpieceTemplateResult r;r.message=QStringLiteral("没有有效模板");return r;}
    SaveWorkpieceTemplateRequest request;request.name=name;request.outputRoot=root;request.coordinateFrame=p.coordinateFrame;request.points=p.points;request.source=p.source;request.isCancelled=cancel;request.interferencePoints=p.interferencePoints;
    request.roleCompanion=serializeTemplateFeaturesV1(p.planes);
    if(!parseTemplateFeaturesV1(request.roleCompanion,p.points.size(),p.interferencePoints.size()).ok){WorkpieceTemplateResult r;r.message=QStringLiteral("模板面片、点范围或角色无效，未保存");return r;}
    request.roleCompanion.insert("workpiece_identity",templatePointIdentityV1(p.points,cancel));request.roleCompanion.insert("interference_identity",templatePointIdentityV1(p.interferencePoints,cancel));
    request.roleCompanion.insert("workpiece_count",qint64(p.points.size()));request.roleCompanion.insert("interference_count",qint64(p.interferencePoints.size()));request.roleCompanion.insert("coordinate_frame",p.coordinateFrame);
    return p.coordinateFrame=="cad_model"?saveCadTemplate(request):saveWorkpieceTemplate(request);
}
TemplatePackageV1 loadTemplatePackageV1(QString path,const TemplateFeatureOptionsV1 &o) {
    TemplatePackageV1 out;QElapsedTimer timer;timer.start();
    if(o.isCancelled&&o.isCancelled()){out.cancelled=true;out.error=QStringLiteral("已取消");return out;}
    if(QFileInfo(path).isDir()){QDir dir(path);for(const auto &name:{QString::fromLatin1(kCadTemplateJson),QString::fromLatin1(kWorkpieceTemplateJson),QStringLiteral("selected_plane_geometry_v1.json")})if(dir.exists(name)){path=dir.filePath(name);break;}}
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){out.error=file.errorString();return out;}
    QJsonParseError parse;auto doc=QJsonDocument::fromJson(file.readAll(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject()){out.error=QStringLiteral("模板元数据无效");return out;}
    auto meta=doc.object();QDir root(QFileInfo(path).absolutePath());
    auto contained=[&](QString name){QString candidate=QFileInfo(root.filePath(name)).canonicalFilePath();QString parent=QFileInfo(root.absolutePath()).canonicalFilePath();return !candidate.isEmpty()&&candidate.startsWith(parent+"/",Qt::CaseInsensitive);};
    if(meta.value("schema_version")=="selected_plane_geometry_v1"){
        if(meta.value("coordinate_frame")!="robot_base"||meta.value("units")!="mm"||!contained("selected_plane_trimmed.ply")){out.error=QStringLiteral("几何结果坐标或点云路径无效");return out;}
        pcv::detail::io::PlyReadOptions io;io.isCancelled=o.isCancelled;io.asciiWorkerCount=2;io.binaryWorkerCount=2;
        auto cloud=pcv::detail::io::readPly(root.filePath("selected_plane_trimmed.ply"),io);if(!cloud.ok){out.error=cloud.error;out.cancelled=cloud.cancelled;return out;}
        if(cloud.points.size()!=meta.value("trim").toObject().value("kept_point_count").toInteger(-1)){out.error=QStringLiteral("几何结果点数与元数据不一致");return out;}
        out.points=std::move(cloud.points);out.coordinateFrame=QStringLiteral("robot_base");out.source=meta.value("source").toObject();
    }else{
        LoadWorkpieceTemplateRequest request;request.infoFile=path;request.isCancelled=o.isCancelled;auto loaded=loadAnyWorkpieceTemplate(request);
        if(!loaded.success){out.error=loaded.message;out.cancelled=loaded.cancelled;return out;}out.points=std::move(loaded.points);out.source=loaded.info.source;out.coordinateFrame=loaded.info.coordinateFrame;
    }
    out.identity=templatePointIdentityV1(out.points,o.isCancelled);
    if(root.exists("template_roles_v1.json")){
        if(!contained("template_roles_v1.json")){out.error=QStringLiteral("角色伴随文件越出模板包");return out;}
        QFile roles(root.filePath("template_roles_v1.json"));if(!roles.open(QIODevice::ReadOnly)){out.error=roles.errorString();return out;}auto object=QJsonDocument::fromJson(roles.readAll()).object();
        qint64 count=object.value("interference_count").toInteger(-1);
        if(count<0||count>kCadMaximumPoints||object.value("workpiece_count").toInteger(-1)!=out.points.size()||object.value("coordinate_frame")!=out.coordinateFrame||object.value("workpiece_identity")!=out.identity){out.error=QStringLiteral("角色伴随文件与工件点云不一致");return out;}
        if(count>0){if(!contained("template_interference_v1.ply")){out.error=QStringLiteral("缺少包内干涉点云");return out;}pcv::detail::io::PlyReadOptions io;io.isCancelled=o.isCancelled;io.asciiWorkerCount=2;io.binaryWorkerCount=2;auto cloud=pcv::detail::io::readPly(root.filePath("template_interference_v1.ply"),io);
            if(!cloud.ok){out.error=cloud.error;out.cancelled=cloud.cancelled;return out;}out.interferencePoints=std::move(cloud.points);}
        if(out.interferencePoints.size()!=count||object.value("interference_identity")!=templatePointIdentityV1(out.interferencePoints,o.isCancelled)){out.error=QStringLiteral("干涉点云摘要不一致");return out;}
        auto features=parseTemplateFeaturesV1(object,out.points.size(),out.interferencePoints.size());if(!features.ok){out.error=features.error;return out;}out.planes=std::move(features.planes);
    }else{
        auto features=prepareTemplateFeaturesV1(out.points,o);if(!features.ok){out.error=features.error;out.cancelled=features.cancelled;return out;}out.planes=std::move(features.planes);out.timings=features.timings;
    }
    if(o.isCancelled&&o.isCancelled()){out.error=QStringLiteral("已取消");out.cancelled=true;return out;}
    out.infoFile=QFileInfo(path).absoluteFilePath();out.ok=true;out.timings.insert("load_package_ms",timer.nsecsElapsed()/1e6);return out;
}
}
