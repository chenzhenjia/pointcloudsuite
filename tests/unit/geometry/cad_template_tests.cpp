#ifdef PCV_BUILD_STEP_IMPORT
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Interface_Static.hxx>
#include <gp_Pln.hxx>
#endif
#include <pcv/workpiece/cad_template_v1.h>
#include <pcv/workpiece/template_package_v1.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

using namespace pcv::workpiece;
#define CHECK(x) do { if(!(x)) {std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return false;} } while(false)
QJsonObject provenance()
{
    return {{"entry","step_cad"},{"processing_steps",QJsonArray{}},{"cad",QJsonObject{
        {"original_step","missing.step"},{"sha256",QString(64,'a')},{"original_units","millimetre"},
        {"unit_scale_to_mm",1.0},{"occt_version","7.9.3"},{"algorithm_version","cad_surface_area_v1"},
        {"selected_faces",QJsonArray{QJsonObject{{"id",1},{"area_mm2",.5},{"surface_type","plane"}}}},
        {"spacing_mm",.5},{"geometry_tolerance_mm",.01},{"generated_point_count",2},{"edge_sampling",false}}}};
}
bool packageTests()
{
    QTemporaryDir tmp; CHECK(tmp.isValid());
    SaveWorkpieceTemplateRequest r; r.name=QString::fromUtf8("设计模板 中文");r.outputRoot=tmp.path();r.coordinateFrame="cad_model";
    r.source=provenance();r.points={{1,2,3,0,0,1},{-1,5,8,0,1,0}};
    const auto extraShared=r.points;
    const auto saved=saveCadTemplate(r);CHECK(saved.success);CHECK(saved.info.coordinateFrame=="cad_model");
    CHECK(saved.outputDirectory.contains("_cad_v1_"));
    CHECK(!loadWorkpieceTemplate({saved.infoFile}).success);
    CHECK(!saveWorkpieceTemplate(r).success);
    const auto loaded=loadAnyWorkpieceTemplate({saved.infoFile});CHECK(loaded.success);
    CHECK(loaded.points.size()==r.points.size());CHECK(std::memcmp(loaded.points.constData(),extraShared.constData(),size_t(r.points.size())*sizeof(pointcloud::Point3D))==0);
    CHECK(saveCadTemplate(r).outputDirectory!=saved.outputDirectory);
    const QString moved=tmp.path()+QString::fromUtf8("/搬迁 包");CHECK(QDir().rename(saved.outputDirectory,moved));
    CHECK(loadCadTemplate({moved+"/"+kCadTemplateJson}).success);
    LoadWorkpieceTemplateRequest cancelledLoad;cancelledLoad.infoFile=moved+"/"+kCadTemplateJson;cancelledLoad.isCancelled=[]{return true;};
    CHECK(loadCadTemplate(cancelledLoad).cancelled);
    r.points[0].nz=0;CHECK(!saveCadTemplate(r).success);r.points=extraShared;
    r.points[0].x=std::numeric_limits<float>::infinity();CHECK(!saveCadTemplate(r).success);r.points=extraShared;
    r.source={};CHECK(!saveCadTemplate(r).success);r.source=provenance();
    r.isCancelled=[] {return true;};CHECK(saveCadTemplate(r).cancelled);r.isCancelled={};
    bool cancel=false;r.isCancelled=[&]{return cancel;};r.progress=[&](WorkpieceTemplateStage s,qsizetype,qsizetype){if(s==WorkpieceTemplateStage::Committing)cancel=true;};
    const auto before=QDir(tmp.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot); CHECK(saveCadTemplate(r).cancelled);
    CHECK(QDir(tmp.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==before);
    r.isCancelled={};r.progress={};
    QFile json(moved+"/"+kCadTemplateJson);CHECK(json.open(QIODevice::ReadOnly));auto object=QJsonDocument::fromJson(json.readAll()).object();json.close();
    object.insert("point_cloud_file","../escape.ply");CHECK(json.open(QIODevice::WriteOnly|QIODevice::Truncate));json.write(QJsonDocument(object).toJson());json.close();
    CHECK(!loadCadTemplate({json.fileName()}).success);
    const auto valid=saveCadTemplate(r);CHECK(valid.success);QFile ply(valid.pointCloudFile);CHECK(ply.open(QIODevice::ReadWrite));CHECK(ply.resize(ply.size()-1));ply.close();CHECK(!loadCadTemplate({valid.infoFile}).success);
    return true;
}
#ifdef PCV_BUILD_STEP_IMPORT
bool writeStep(const TopoDS_Shape &shape,const QString &path,const char *unit="MM")
{
    STEPControl_Writer writer;Interface_Static::SetCVal("write.step.unit",unit);
    if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone) return false;
    const bool ok=writer.Write(path.toLocal8Bit().constData())==IFSelect_RetDone;
    Interface_Static::SetCVal("write.step.unit","MM");return ok;
}
bool geometryTests()
{
    QTemporaryDir tmp;CHECK(tmp.isValid());const QString input=tmp.path()+"/input.step";
    CHECK(writeStep(BRepPrimAPI_MakeBox(gp_Pnt(10,20,30),10,20,30).Shape(),input));
    const QString unicode=tmp.path()+QString::fromUtf8("/中文 工件.stp");CHECK(QFile::copy(input,unicode));
    const auto model=readStepModel({unicode});if(!model.success)std::cerr<<model.message.toStdString()<<'\n';CHECK(model.success);CHECK(model.faces.size()==6);
    for(const auto &face:model.faces)CHECK(face.planar&&std::abs(face.planeNormal.length()-1)<1e-5);
    const auto roles=generateStepTemplatePackageV1(model,{1,2},{3},.5);CHECK(roles.ok&&roles.planes.size()==3&&!roles.interferencePoints.isEmpty());
    const auto roleSaved=saveTemplatePackageV1(roles,QString::fromUtf8("多平面 CAD"),tmp.path());CHECK(roleSaved.success);
    const auto roleRead=loadTemplatePackageV1(roleSaved.outputDirectory);CHECK(roleRead.ok&&roleRead.planes.size()==3&&roleRead.coordinateFrame=="cad_model");
    CHECK(!generateStepTemplatePackageV1(model,{1},{1},.5).ok);
    CHECK(!generateStepTemplatePackageV1(model,{1},{2},1e-9).ok);
    CHECK(std::abs(model.minimum.x()-10)<.01);CHECK(std::abs(model.maximum.z()-60)<.01);
    GenerateCadTemplateRequest g;g.model=model.model;g.faceIds={6,1,3,2,5,4};
    const auto cloud=generateCadTemplateCloud(g);if(!cloud.success)std::cerr<<cloud.message.toStdString()<<'\n';CHECK(cloud.success);CHECK(cloud.points.size()==8800);
    const auto shared=cloud.points;
    const auto again=generateCadTemplateCloud(g);CHECK(again.success);CHECK(std::memcmp(shared.constData(),again.points.constData(),size_t(shared.size())*sizeof(pointcloud::Point3D))==0);
    for(const auto &p:shared)CHECK((p.x-15)*p.nx+(p.y-30)*p.ny+(p.z-45)*p.nz>0);
    g.spacingMm=1;CHECK(generateCadTemplateCloud(g).points.size()==2200);
    g.spacingMm=1e-10;CHECK(!generateCadTemplateCloud(g).success);
    g.spacingMm=0;CHECK(!generateCadTemplateCloud(g).success);
    g.spacingMm=.5;g.faceIds={1,1};CHECK(!generateCadTemplateCloud(g).success);g.faceIds={7};CHECK(!generateCadTemplateCloud(g).success);
    g.faceIds={1};g.isCancelled=[] {return true;};CHECK(generateCadTemplateCloud(g).cancelled);g.isCancelled={};
    bool cancelled=false;g.isCancelled=[&]{return cancelled;};g.progress=[&](CadStage,qint64,qint64){cancelled=true;};CHECK(generateCadTemplateCloud(g).cancelled);g.isCancelled={};g.progress={};
    CHECK(readStepModel({unicode,[]{return true;},{}}).cancelled);
    CHECK(!readStepModel({tmp.path()+"/missing.step"}).success);
    CHECK(writeStep(BRepPrimAPI_MakeBox(25.4,50.8,76.2).Shape(),input,"INCH"));
    const auto inch=readStepModel({input});if(!inch.success)std::cerr<<inch.message.toStdString()<<'\n';CHECK(inch.success);CHECK(std::abs(inch.maximum.x()-25.4)<.01);CHECK(inch.source.value("cad").toObject().value("unit_scale_to_mm").toDouble()==25.4);
    CHECK(writeStep(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,10,0,10).Shape(),input));CHECK(!readStepModel({input}).success);
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);builder.Add(compound,BRepPrimAPI_MakeBox(1,1,1).Shape());builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(3,0,0),1,1,1).Shape());
    CHECK(writeStep(compound,input));CHECK(!readStepModel({input}).success);
    // Explicit assembly with just one component must also be rejected.
    Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");auto tool=XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto part=tool->AddShape(BRepPrimAPI_MakeBox(1,1,1).Shape(),false);const auto assembly=tool->NewShape();tool->AddComponent(assembly,part,TopLoc_Location());tool->UpdateAssemblies();
    STEPCAFControl_Writer assemblyWriter;CHECK(assemblyWriter.Transfer(doc));CHECK(assemblyWriter.Write(input.toLocal8Bit().constData())==IFSelect_RetDone);CHECK(!readStepModel({input}).success);
    int shapeIndex=0;
    for(const auto &shape: {BRepPrimAPI_MakeCylinder(5,10).Shape(),BRepPrimAPI_MakeSphere(5).Shape(),
            BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(20,20,20).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10,10,-1),gp_Dir(0,0,1)),3,22).Shape()).Shape(),
            BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(20,20,20).Shape(),BRepPrimAPI_MakeSphere(gp_Pnt(10,10,10),3).Shape()).Shape()}) {
        CHECK(writeStep(shape,input));const auto m=readStepModel({input});if(!m.success)std::cerr<<m.message.toStdString()<<'\n';CHECK(m.success);
        g.model=m.model;g.faceIds.clear();for(const auto &f:m.faces)g.faceIds.append(f.id);
        const auto c=generateCadTemplateCloud(g);if(!c.success)std::cerr<<c.message.toStdString()<<'\n';CHECK(c.success);
        for(const auto &p:c.points) {const double n=double(p.nx)*p.nx+double(p.ny)*p.ny+double(p.nz)*p.nz;CHECK(std::abs(n-1)<.001);}
        if(shapeIndex==2) for(const auto &p:c.points) {
            const double radius=std::hypot(double(p.x)-10,double(p.y)-10);
            if(std::abs(p.z)<.001 || std::abs(p.z-20)<.001) CHECK(radius>=3-.01);
            if(std::abs(radius-3)<.001) CHECK((p.x-10)*p.nx+(p.y-10)*p.ny<0);
        }
        if(shapeIndex==3) for(const auto &p:c.points) {
            const double radius=std::sqrt(std::pow(double(p.x)-10,2)+std::pow(double(p.y)-10,2)+std::pow(double(p.z)-10,2));
            if(std::abs(radius-3)<.001) CHECK((p.x-10)*p.nx+(p.y-10)*p.ny+(p.z-10)*p.nz<0);
        }
        ++shapeIndex;
        if(m.faces.size()==1)for(const auto &p:c.points){CHECK(std::abs(std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z)-5)<.01);CHECK(p.x*p.nx+p.y*p.ny+p.z*p.nz>0);}
    }
    CHECK(writeStep(BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakeSphere(5).Shape(),true).Shape(),input));
    const auto nurbs=readStepModel({input});CHECK(nurbs.success);g.model=nurbs.model;g.faceIds.clear();for(const auto &f:nurbs.faces)g.faceIds.append(f.id);
    CHECK(generateCadTemplateCloud(g).success);
    auto reversed=BRepPrimAPI_MakeBox(10,20,30).Shape();reversed.Reverse();CHECK(writeStep(reversed,input));
    const auto reverseModel=readStepModel({input});CHECK(reverseModel.success);g.model=reverseModel.model;g.faceIds={1,2,3,4,5,6};
    const auto reverseCloud=generateCadTemplateCloud(g);CHECK(reverseCloud.success);
    for(const auto &p:reverseCloud.points)CHECK((p.x-5)*p.nx+(p.y-10)*p.ny+(p.z-15)*p.nz>0);
    CHECK(writeStep(BRepPrimAPI_MakeBox(.001,.001,.001).Shape(),input));const auto tiny=readStepModel({input});CHECK(tiny.success);g.model=tiny.model;g.faceIds={1};CHECK(generateCadTemplateCloud(g).points.size()==1);
    CHECK(writeStep(BRepPrimAPI_MakeSphere(gp_Pnt(1e8+.123,1e8+.234,1e8+.345),5).Shape(),input));const auto large=readStepModel({input});CHECK(large.success);g.model=large.model;g.faceIds={1};CHECK(!generateCadTemplateCloud(g).success);
    QFile bad(input);CHECK(bad.open(QIODevice::WriteOnly|QIODevice::Truncate));bad.write("bad STEP");bad.close();CHECK(!readStepModel({input}).success);
    return true;
}
bool benchmark(qint64 count)
{
    QTemporaryDir tmp;CHECK(tmp.isValid());const QString path=tmp.path()+"/benchmark.step";
    CHECK(writeStep(BRepPrimAPI_MakeBox(500,500,1).Shape(),path));const auto model=readStepModel({path});CHECK(model.success);
    int face=0;for(const auto &f:model.faces)if(f.areaMm2>249999){face=f.id;break;}CHECK(face>0);
    GenerateCadTemplateRequest g;g.model=model.model;g.faceIds={face};g.spacingMm=std::nextafter(std::sqrt(250000.0/count),std::numeric_limits<double>::infinity());
    QElapsedTimer timer;timer.start();const auto cloud=generateCadTemplateCloud(g);const auto generateMs=timer.elapsed();CHECK(cloud.success);
    SaveWorkpieceTemplateRequest save;save.name="benchmark";save.outputRoot=tmp.path();save.coordinateFrame="cad_model";save.source=cloud.source;save.points=cloud.points;
    timer.restart();const auto saved=saveCadTemplate(save);const auto saveMs=timer.elapsed();CHECK(saved.success);
    timer.restart();const auto loaded=loadCadTemplate({saved.infoFile});const auto loadMs=timer.elapsed();CHECK(loaded.success);
    bool stop=false;g.isCancelled=[&]{return stop;};g.progress=[&](CadStage,qint64 done,qint64){if(done>1000){timer.restart();stop=true;}};
    CHECK(generateCadTemplateCloud(g).cancelled);const auto cancelMs=timer.elapsed();
    quint64 peak=0;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS memory{};if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))peak=memory.PeakWorkingSetSize;
#endif
    std::cout<<"BENCHMARK points="<<cloud.points.size()<<" generate_ms="<<generateMs<<" save_ms="<<saveMs<<" load_ms="<<loadMs<<" cancel_ms="<<cancelMs<<" peak_working_set_bytes="<<peak<<'\n';return true;
}
#endif
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
#ifdef PCV_BUILD_STEP_IMPORT
    if(app.arguments().size()==3 && app.arguments().at(1)=="--benchmark")return benchmark(app.arguments().at(2).toLongLong())?0:1;
#endif
    if(!packageTests())return 1;
#ifdef PCV_BUILD_STEP_IMPORT
    if(!geometryTests())return 2;
#else
    if(readStepModel({}).success || generateCadTemplateCloud({}).success)return 3;
#endif
    std::cout<<"CAD template tests passed\n";return 0;
}
