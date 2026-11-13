#ifdef PCV_BUILD_STEP_IMPORT
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDF_LabelSequence.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Pln.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Version.hxx>
#include <Standard_Failure.hxx>
#include <Precision.hxx>
#endif
#include <pcv/workpiece/cad_template_v1.h>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <sstream>
#include <chrono>

namespace pcv::workpiece {
namespace {
bool stopped(const std::function<bool()> &f) { return f && f(); }
template<class R> R fail(QString message, bool cancelled=false)
{
    R r; r.message=std::move(message); r.errorCode=QStringLiteral("PCV_INPUT_002");
    r.cancelled=cancelled; return r;
}
void report(const CadProgress &f, CadStage s, qint64 n=0, qint64 total=0) { if(f) f(s,n,total); }
#ifdef PCV_BUILD_STEP_IMPORT
std::timed_mutex importMutex; // OCCT STEP translators use process-wide settings.
double halton(quint64 i, int base)
{
    double v=0, f=1;
    while(i) { f/=base; v+=f*(i%base); i/=base; }
    return v;
}
QString surfaceName(GeomAbs_SurfaceType t)
{
    switch(t) {
    case GeomAbs_Plane: return "plane";
    case GeomAbs_Cylinder: return "cylinder";
    case GeomAbs_Cone: return "cone";
    case GeomAbs_Sphere: return "sphere";
    case GeomAbs_Torus: return "torus";
    case GeomAbs_BezierSurface: return "bezier";
    case GeomAbs_BSplineSurface: return "bspline";
    default: return "other";
    }
}
struct FaceData {
    TopoDS_Face face;
    Handle(Poly_Triangulation) triangulation;
    QVector<double> cumulativeArea;
    QVector<int> triangleIndices;
    CadFaceInfo info;
};
#endif
}
class CadModel {
public:
#ifdef PCV_BUILD_STEP_IMPORT
    QVector<FaceData> faces;
    mutable std::timed_mutex samplingMutex;
#endif
    QJsonObject source;
};

StepModelResult readStepModel(const ReadStepModelRequest &request)
{
#ifndef PCV_BUILD_STEP_IMPORT
    Q_UNUSED(request);
    return fail<StepModelResult>(QStringLiteral("此构建未启用 STEP 导入"));
#else
    try {
        if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        report(request.progress,CadStage::Reading);
        QFile file(request.file);
        if(!file.open(QIODevice::ReadOnly)) return fail<StepModelResult>(file.errorString());
        // One byte snapshot supplies both the hash and the Unicode-safe stream reader.
        std::string bytes;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while(!file.atEnd()) {
            if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
            const auto block=file.read(1024*1024);
            if(block.isEmpty() && file.error()!=QFileDevice::NoError) return fail<StepModelResult>(file.errorString());
            hash.addData(block); bytes.append(block.constData(),size_t(block.size()));
            report(request.progress,CadStage::Reading,file.pos(),file.size());
        }
        std::istringstream stream(std::move(bytes));
        std::string{}.swap(bytes); // C++17 stream construction may copy the snapshot.
        std::unique_lock<std::timed_mutex> lock(importMutex,std::defer_lock);
        while(!lock.try_lock_for(std::chrono::milliseconds(25))) {
            if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        }
        if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        STEPCAFControl_Reader reader;
        if(reader.ReadStream("input.step",stream)!=IFSelect_RetDone)
            return fail<StepModelResult>(QStringLiteral("STEP 解析失败"));
        TColStd_SequenceOfAsciiString length,angle,solidAngle;
        reader.ChangeReader().FileUnits(length,angle,solidAngle);
        if(length.Length()!=1) return fail<StepModelResult>(QStringLiteral("STEP 长度单位不明确或存在混合单位"));
        const QString unit=QString::fromLatin1(length.Value(1).ToCString()).toLower();
        double scale=0;
        if(unit=="millimetre" || unit=="millimeter" || unit=="mm") scale=1;
        else if(unit=="metre" || unit=="meter" || unit=="m") scale=1000;
        else if(unit=="centimetre" || unit=="centimeter" || unit=="cm") scale=10;
        else if(unit=="inch" || unit=="in") scale=25.4;
        else if(unit=="foot" || unit=="ft") scale=304.8;
        if(scale==0) return fail<StepModelResult>(QStringLiteral("不支持或无法确认 STEP 单位：%1").arg(unit));
        reader.ChangeReader().SetSystemLengthUnit(1.0); // mm; before transfer.
        Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");
        XCAFDoc_DocumentTool::SetLengthUnit(doc,1.0,UnitsMethods_LengthUnit_Millimeter);
        if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        report(request.progress,CadStage::Validating);
        reader.ChangeReader().SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
        if(!reader.Transfer(doc)) return fail<StepModelResult>(QStringLiteral("STEP 几何转换失败"));
        const auto shapes=XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        TDF_LabelSequence roots; shapes->GetFreeShapes(roots);
        if(roots.Length()!=1 || shapes->IsAssembly(roots.Value(1)))
            return fail<StepModelResult>(QStringLiteral("首版仅支持单个非装配工件实体"));
        auto shape=shapes->GetShape(roots.Value(1));
        lock.unlock();
        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(shape,TopAbs_SOLID,solids);
        if(solids.Extent()!=1 || !BRepCheck_Analyzer(shape).IsValid())
            return fail<StepModelResult>(QStringLiteral("需要有效闭合单实体；不支持开放曲面、多实体或坏几何"));
        auto solid=TopoDS::Solid(solids(1));
        for(TopExp_Explorer shells(solid,TopAbs_SHELL);shells.More();shells.Next()) {
            BRepCheck_Shell check(TopoDS::Shell(shells.Current()));
            if(check.Closed()!=BRepCheck_NoError) return fail<StepModelResult>(QStringLiteral("实体包含未闭合壳体"));
        }
        TopTools_IndexedMapOfShape allFaces,solidFaces;
        TopExp::MapShapes(shape,TopAbs_FACE,allFaces);
        TopExp::MapShapes(solid,TopAbs_FACE,solidFaces);
        if(allFaces.Extent()!=solidFaces.Extent()) return fail<StepModelResult>(QStringLiteral("实体之外存在额外曲面"));
        for(const auto kind : {TopAbs_EDGE,TopAbs_VERTEX}) {
            TopTools_IndexedMapOfShape all,owned;
            TopExp::MapShapes(shape,kind,all); TopExp::MapShapes(solid,kind,owned);
            if(all.Extent()!=owned.Extent()) return fail<StepModelResult>(QStringLiteral("实体之外存在额外边或点"));
        }
        BRepClass3d_SolidClassifier orientation(solid);
        orientation.PerformInfinitePoint(Precision::Confusion());
        if(orientation.State()==TopAbs_IN) solid.Reverse();
        if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        report(request.progress,CadStage::Meshing);
        BRepMesh_IncrementalMesh mesher(solid,0.02,false,0.2,false);
        if(!mesher.IsDone()) return fail<StepModelResult>(QStringLiteral("CAD 三角化失败"));
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(solid,TopAbs_FACE,faces);
        auto model=std::make_shared<CadModel>();
        StepModelResult out;
        bool first=true;
        for(int i=1;i<=faces.Extent();++i) {
            if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
            FaceData data;
            data.face=TopoDS::Face(faces(i));
            GProp_GProps props; BRepGProp::SurfaceProperties(data.face,props);
            data.info={i,surfaceName(BRepAdaptor_Surface(data.face).GetType()),props.Mass()};
            BRepAdaptor_Surface analytic(data.face);
            if(analytic.GetType()==GeomAbs_Plane) {
                const auto plane=analytic.Plane(); const auto p=plane.Location(); auto n=plane.Axis().Direction();
                if(data.face.Orientation()==TopAbs_REVERSED)n.Reverse();
                data.info.planar=true;
                data.info.planeOrigin=QVector3D(float(p.X()),float(p.Y()),float(p.Z()));
                data.info.planeNormal=QVector3D(float(n.X()),float(n.Y()),float(n.Z()));
            }
            if(!std::isfinite(data.info.areaMm2) || data.info.areaMm2<=0)
                return fail<StepModelResult>(QStringLiteral("面 %1 面积无效").arg(i));
            TopLoc_Location loc;
            data.triangulation=BRep_Tool::Triangulation(data.face,loc);
            if(data.triangulation.IsNull() || !data.triangulation->HasUVNodes())
                return fail<StepModelResult>(QStringLiteral("面 %1 缺少参数三角网格").arg(i));
            const auto &tri=data.triangulation;
            if(out.mesh.vertices.size()>std::numeric_limits<int>::max()-tri->NbNodes())
                return fail<StepModelResult>(QStringLiteral("显示网格顶点数量超出索引范围"));
            const int offset=int(out.mesh.vertices.size());
            data.cumulativeArea.reserve(tri->NbTriangles()); data.triangleIndices.reserve(tri->NbTriangles());
            for(int j=1;j<=tri->NbNodes();++j) {
                const auto p=tri->Node(j).Transformed(loc.Transformation());
                const QVector3D v(float(p.X()),float(p.Y()),float(p.Z()));
                if(!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z()))
                    return fail<StepModelResult>(QStringLiteral("模型坐标超出显示范围"));
                out.mesh.vertices.append(v);
                if(first) { out.minimum=out.maximum=v; first=false; }
                for(int k=0;k<3;++k) { out.minimum[k]=std::min(out.minimum[k],v[k]); out.maximum[k]=std::max(out.maximum[k],v[k]); }
            }
            double area=0;
            for(int j=1;j<=tri->NbTriangles();++j) {
                int a,b,c; tri->Triangle(j).Get(a,b,c);
                const auto pa=tri->Node(a),pb=tri->Node(b),pc=tri->Node(c);
                const double w=gp_Vec(pa,pb).Crossed(gp_Vec(pa,pc)).Magnitude()*0.5;
                if(w<=0) continue;
                area+=w; data.cumulativeArea.append(area); data.triangleIndices.append(j);
                if(data.face.Orientation()==TopAbs_REVERSED) std::swap(b,c);
                out.mesh.triangles.append({offset+a-1,offset+b-1,offset+c-1,i});
            }
            if(area<=0) return fail<StepModelResult>(QStringLiteral("面 %1 三角网格面积为空").arg(i));
            model->faces.append(data); out.faces.append(data.info);
            report(request.progress,CadStage::Meshing,i,faces.Extent());
        }
        model->source={{"entry","step_cad"},{"files",QJsonArray{QFileInfo(request.file).absoluteFilePath()}},
            {"processing_steps",QJsonArray{}},{"cad",QJsonObject{{"sha256",QString::fromLatin1(hash.result().toHex())},
                {"original_step",QFileInfo(request.file).absoluteFilePath()},{"original_units",unit},{"unit_scale_to_mm",scale},{"occt_version",OCC_VERSION_COMPLETE},
                {"algorithm_version","cad_surface_area_v1"}}}};
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(solid,bounds,false,false);
        double xmin,ymin,zmin,xmax,ymax,zmax; bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        out.minimum=QVector3D(float(xmin),float(ymin),float(zmin));
        out.maximum=QVector3D(float(xmax),float(ymax),float(zmax));
        out.source=model->source; out.model=std::move(model); out.success=true;
        if(stopped(request.isCancelled)) return fail<StepModelResult>("已取消",true);
        return out;
    } catch(const Standard_Failure &e) { return fail<StepModelResult>(QString::fromUtf8(e.GetMessageString())); }
      catch(const std::exception &e) { return fail<StepModelResult>(QString::fromUtf8(e.what())); }
#endif
}

CadCloudResult generateCadTemplateCloud(const GenerateCadTemplateRequest &request)
{
#ifndef PCV_BUILD_STEP_IMPORT
    Q_UNUSED(request);
    return fail<CadCloudResult>(QStringLiteral("此构建未启用 STEP 导入"));
#else
    try {
        if(stopped(request.isCancelled)) return fail<CadCloudResult>("已取消",true);
        if(!request.model || request.faceIds.isEmpty() || !std::isfinite(request.spacingMm) || request.spacingMm<=0)
            return fail<CadCloudResult>(QStringLiteral("请选择面并输入有限正数间距"));
        // OCCT surface adaptors may maintain evaluation caches; serialize access
        // to the same immutable model while independent models remain independent.
        std::unique_lock<std::timed_mutex> modelLock(request.model->samplingMutex,std::defer_lock);
        while(!modelLock.try_lock_for(std::chrono::milliseconds(25))) {
            if(stopped(request.isCancelled)) return fail<CadCloudResult>("已取消",true);
        }
        QVector<int> ids=request.faceIds; std::sort(ids.begin(),ids.end());
        if(std::adjacent_find(ids.cbegin(),ids.cend())!=ids.cend()) return fail<CadCloudResult>(QStringLiteral("面编号重复"));
        QVector<qint64> counts;
        qint64 total=0;
        for(int id:ids) {
            if(id<1 || id>request.model->faces.size()) return fail<CadCloudResult>(QStringLiteral("面编号越界"));
            const double n=std::max(1.0,std::ceil(request.model->faces.at(id-1).info.areaMm2/request.spacingMm/request.spacingMm));
            if(!std::isfinite(n) || n>kCadMaximumPoints-total) return fail<CadCloudResult>(QStringLiteral("超过 10000000 点，请增大间距"));
            counts.append(qint64(n)); total+=qint64(n);
        }
        CadCloudResult out;
        out.points.reserve(total);
        QJsonArray selected;
        for(qsizetype faceIndex=0;faceIndex<ids.size();++faceIndex) {
            const int id=ids.at(faceIndex);
            const auto &data=request.model->faces.at(id-1);
            const auto tri=data.triangulation;
            BRepAdaptor_Surface surface(data.face);
            const qint64 count=counts.at(faceIndex);
            out.faceRanges.append({id,out.points.size(),qsizetype(count)});
            selected.append(QJsonObject{{"id",id},{"surface_type",data.info.surfaceType},{"area_mm2",data.info.areaMm2}});
            qint64 accepted=0;
            // A fixed low-discrepancy sequence, no global RNG or thread-dependent order.
            for(quint64 candidate=1;accepted<count && candidate<=quint64(count)*64+1024;++candidate) {
                if((candidate&255)==1) {
                    if(stopped(request.isCancelled)) return fail<CadCloudResult>("已取消",true);
                    report(request.progress,CadStage::Sampling,out.points.size(),total);
                }
                const double target=halton(candidate,5)*data.cumulativeArea.constLast();
                const auto it=std::upper_bound(data.cumulativeArea.cbegin(),data.cumulativeArea.cend(),target);
                const qsizetype ix=std::min(qsizetype(it-data.cumulativeArea.cbegin()),data.cumulativeArea.size()-1);
                int a,b,c; tri->Triangle(data.triangleIndices.at(ix)).Get(a,b,c);
                const double r=std::sqrt(halton(candidate,2)),s=halton(candidate,3);
                const auto uva=tri->UVNode(a),uvb=tri->UVNode(b),uvc=tri->UVNode(c);
                const gp_Pnt2d uv((1-r)*uva.X()+r*(1-s)*uvb.X()+r*s*uvc.X(),
                                  (1-r)*uva.Y()+r*(1-s)*uvb.Y()+r*s*uvc.Y());
                BRepClass_FaceClassifier classifier(data.face,uv,Precision::PConfusion());
                if(classifier.State()!=TopAbs_IN && classifier.State()!=TopAbs_ON) continue;
                gp_Pnt p; gp_Vec du,dv; surface.D1(uv.X(),uv.Y(),p,du,dv);
                auto normal=du.Crossed(dv);
                if(normal.SquareMagnitude()<1e-24) continue; // Do not emit singular normals.
                normal.Normalize(); if(data.face.Orientation()==TopAbs_REVERSED) normal.Reverse();
                pointcloud::Point3D point{float(p.X()),float(p.Y()),float(p.Z()),float(normal.X()),float(normal.Y()),float(normal.Z())};
                // The exact evaluated point is in the trimmed face. Distance to that
                // point is a conservative bound on float32 point-to-face distance.
                const double error=p.Distance(gp_Pnt(point.x,point.y,point.z));
                const double n=double(point.nx)*point.nx+double(point.ny)*point.ny+double(point.nz)*point.nz;
                if(!std::isfinite(error) || error>0.01 || !std::isfinite(n) || std::abs(n-1)>0.001)
                    return fail<CadCloudResult>(QStringLiteral("面 %1 的 float32 坐标/法向不能满足 0.01 mm 容差").arg(id));
                out.points.append(point); ++accepted;
            }
            if(accepted!=count) return fail<CadCloudResult>(QStringLiteral("面 %1 无法完成有效曲面采样").arg(id));
        }
        if(stopped(request.isCancelled)) return fail<CadCloudResult>("已取消",true);
        out.source=request.model->source;
        auto cad=out.source.value("cad").toObject();
        cad.insert("selected_faces",selected); cad.insert("spacing_mm",request.spacingMm);
        cad.insert("geometry_tolerance_mm",0.01); cad.insert("edge_sampling",false);
        cad.insert("generated_point_count",qint64(out.points.size())); out.source.insert("cad",cad);
        report(request.progress,CadStage::Sampling,total,total);
        out.success=true; return out;
    } catch(const Standard_Failure &e) { return fail<CadCloudResult>(QString::fromUtf8(e.GetMessageString())); }
      catch(const std::exception &e) { return fail<CadCloudResult>(QString::fromUtf8(e.what())); }
#endif
}
} // namespace pcv::workpiece
