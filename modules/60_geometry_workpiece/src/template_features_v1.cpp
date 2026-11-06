#include <pcv/workpiece/template_features_v1.h>
#include "plane_eigen_v1.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>
namespace pcv::workpiece {
namespace {
QVector3D xyz(const pointcloud::Point3D &p){return {p.x,p.y,p.z};}
QJsonArray vec(QVector3D p){return {double(p.x()),double(p.y()),double(p.z())};}
bool readVec(QJsonValue value,QVector3D &p){auto a=value.toArray();if(a.size()!=3)return false;for(int k=0;k<3;++k){double d=a.at(k).toDouble(std::numeric_limits<double>::quiet_NaN());if(!std::isfinite(d)||std::abs(d)>std::numeric_limits<float>::max())return false;p[k]=float(d);}return true;}
void describe(TemplatePlaneV1 &p,const QVector<pointcloud::Point3D> &points) {
    double center[3]{};for(int i:p.indices)for(int k=0;k<3;++k)center[k]+=xyz(points.at(i))[k];
    for(int k=0;k<3;++k)center[k]/=std::max<qsizetype>(1,p.indices.size());
    p.center={float(center[0]),float(center[1]),float(center[2])};p.normal=QVector3D(p.model.a,p.model.b,p.model.c).normalized();
    double covariance[3][3]{};for(int i:p.indices){auto d=xyz(points.at(i))-p.center;for(int a=0;a<3;++a)for(int b=0;b<3;++b)covariance[a][b]+=double(d[a])*d[b];}
    double values[3],vectors[3][3];pcv::planefitting::detail::symmetricEigen3(covariance,values,vectors);
    p.axis=QVector3D(float(vectors[0][0]),float(vectors[1][0]),float(vectors[2][0]));
    p.axis=(p.axis-p.normal*QVector3D::dotProduct(p.axis,p.normal)).normalized();
}
}
TemplateFeaturesV1 prepareTemplateFeaturesV1(const QVector<pointcloud::Point3D> &points,const TemplateFeatureOptionsV1 &o) {
    TemplateFeaturesV1 out;QElapsedTimer timer;timer.start();
    if(points.size()<100||points.size()>std::numeric_limits<int>::max()||o.maximumSeeds<1||!std::isfinite(o.toleranceMm)||o.toleranceMm<=0){out.error=QStringLiteral("特征准备需要至少 100 个有效点及正数容差");return out;}
    auto stopped=[&]{return o.isCancelled&&o.isCancelled();};
    pcv::planefitting::IndexPreparationOptions io;io.parallelEnabled=true;io.maxParallelTasks=2;io.processingBudget=o.budget;io.normalBatchSize=65536;io.cacheNormalNeighbors=true;
    auto index=pcv::planefitting::preparePlaneSelectionIndex(points,o.isCancelled,io);
    if(!index.index){out.error=index.error;out.cancelled=index.cancelled;return out;}
    out.timings.insert("index_ms",index.preparationMs);
    pcv::planefitting::SingleClickOptions so;so.surfaceToleranceMm=o.toleranceMm;so.parallelEnabled=true;so.maxParallelTasks=2;so.processingBudget=o.budget;so.isCancelled=o.isCancelled;
    for(int seed=0;seed<o.maximumSeeds;++seed){if(stopped()){out.cancelled=true;out.error=QStringLiteral("已取消");return out;}
        int i=int((quint64(seed)*quint64(points.size()))/quint64(o.maximumSeeds));bool covered=false;
        for(const auto &p:out.planes)if(std::binary_search(p.indices.cbegin(),p.indices.cend(),i)){covered=true;break;}if(covered)continue;
        auto fit=pcv::planefitting::selectPlaneAtPoint(points,index.index,i,so);if(!fit.ok())continue;
        bool duplicate=false;for(const auto &p:out.planes){qsizetype overlap=0;for(int j:fit.planeIndices)if(std::binary_search(p.indices.cbegin(),p.indices.cend(),j))++overlap;
            if(overlap>=.8*std::min(p.indices.size(),fit.planeIndices.size())){duplicate=true;break;}}
        if(duplicate)continue;
        TemplatePlaneV1 p;p.faceId=int(out.planes.size()+1);p.indices=fit.planeIndices;std::sort(p.indices.begin(),p.indices.end());p.model=fit.model;p.count=p.indices.size();describe(p,points);
        pcv::planefitting::PlaneMaskOptionsV1 mo;mo.samplingAxisU=fit.samplingAxisU;mo.spacingUMm=fit.samplingSpacingUMm;mo.spacingVMm=fit.samplingSpacingVMm;mo.processingBudget=o.budget;mo.isCancelled=o.isCancelled;
        auto mask=pcv::planefitting::buildPlaneMaskV1(points,p.indices,p.model,mo);
        if(!mask.ok){out.error=mask.error;out.cancelled=mask.cancelled;return out;}p.region=std::move(mask.mesh);
        out.planes.append(std::move(p));
    }
    if(stopped()){out.cancelled=true;out.error=QStringLiteral("已取消");return out;}
    out.ok=!out.planes.isEmpty();if(!out.ok)out.error=QStringLiteral("没有找到可用于定位的平面与边界证据");
    out.timings.insert("feature_ms",timer.nsecsElapsed()/1e6);out.timings.insert("seeds_limit",o.maximumSeeds);return out;
}
TemplateFeaturesV1 cadTemplateFeaturesV1(const StepModelResult &m,const CadCloudResult &cloud,const QSet<int> &interference,const std::function<bool()> &cancel) {
    TemplateFeaturesV1 out;
    for(const auto &range:cloud.faceRanges){auto it=std::find_if(m.faces.cbegin(),m.faces.cend(),[&](const auto &f){return f.id==range.faceId;});
        if(cancel&&cancel()){out.cancelled=true;out.planes.clear();out.error=QStringLiteral("已取消");return out;}
        if(it==m.faces.cend()||!it->planar){out.error=QStringLiteral("模板只支持解析平面");return out;}
        if(range.first<0||range.count<1||range.first>cloud.points.size()-range.count){out.error=QStringLiteral("CAD 逐面点范围无效");return out;}
        TemplatePlaneV1 p;p.faceId=range.faceId;p.interference=interference.contains(p.faceId);p.first=range.first;p.count=range.count;
        p.model.a=it->planeNormal.x();p.model.b=it->planeNormal.y();p.model.c=it->planeNormal.z();p.model.d=-QVector3D::dotProduct(it->planeNormal,it->planeOrigin);
        p.indices.reserve(range.count);for(qsizetype i=range.first;i<range.first+range.count;++i)p.indices.append(int(i));describe(p,cloud.points);
        // Use the exact trimmed OCCT triangulation, including its inner wires.
        QMap<int,int> vertexMap;QMap<QPair<int,int>,int> edges;
        for(qsizetype triangle=0;triangle<m.mesh.triangles.size();++triangle){if((triangle&4095)==0&&cancel&&cancel()){out.cancelled=true;out.planes.clear();out.error=QStringLiteral("已取消");return out;}const auto &t=m.mesh.triangles.at(triangle);if(t.faceId!=p.faceId)continue;int ids[3]={t.a,t.b,t.c};
            for(int j:ids)if(!vertexMap.contains(j)){int n=int(p.region.vertices.size());vertexMap.insert(j,n);p.region.vertices.append(m.mesh.vertices.at(j));}
            for(int k=0;k<3;++k){p.region.triangleIndices.append(quint32(vertexMap.value(ids[k])));int a=ids[k],b=ids[(k+1)%3];if(a>b)std::swap(a,b);++edges[qMakePair(a,b)];}}
        QMultiMap<int,int> adjacency;for(auto e=edges.cbegin();e!=edges.cend();++e)if(e.value()==1){adjacency.insert(e.key().first,e.key().second);adjacency.insert(e.key().second,e.key().first);}
        while(!adjacency.isEmpty()){int start=adjacency.firstKey(),current=start;int first=int(p.region.boundaryVertices.size());
            do{p.region.boundaryVertices.append(m.mesh.vertices.at(current));auto edge=adjacency.find(current);if(edge==adjacency.end()){out.error=QStringLiteral("CAD 边界未闭合");return out;}int next=edge.value();adjacency.erase(edge);adjacency.remove(next,current);current=next;}while(current!=start);
            p.region.boundaryVertices.append(m.mesh.vertices.at(start));
            p.region.boundaryRanges.append({first,int(p.region.boundaryVertices.size())-first});}
        out.planes.append(std::move(p));
    }
    out.ok=!out.planes.isEmpty();if(cancel&&cancel()){out.ok=false;out.cancelled=true;out.planes.clear();out.error=QStringLiteral("已取消");}return out;
}
QJsonObject serializeTemplateFeaturesV1(const QVector<TemplatePlaneV1> &planes) {
    QJsonArray faces;
    for(const auto &p:planes){QJsonArray vertices,triangles,boundary,ranges;for(auto v:p.region.vertices)vertices.append(vec(v));for(auto i:p.region.triangleIndices)triangles.append(int(i));for(auto v:p.region.boundaryVertices)boundary.append(vec(v));for(auto r:p.region.boundaryRanges)ranges.append(QJsonArray{r.first,r.second});
        QJsonArray indices;for(int i:p.indices)indices.append(i);
        faces.append(QJsonObject{{"face_id",p.faceId},{"role",p.interference?"interference":"workpiece"},{"first",qint64(p.first)},{"count",qint64(p.count)},{"indices",indices},
            {"plane",QJsonArray{p.model.a,p.model.b,p.model.c,p.model.d}},{"center",vec(p.center)},{"normal",vec(p.normal)},{"axis",vec(p.axis)},
            {"vertices",vertices},{"triangles",triangles},{"boundary",boundary},{"ranges",ranges}});}
    return {{"schema_version","pcv-template-roles-v1"},{"units","mm"},{"faces",faces}};
}
TemplateFeaturesV1 parseTemplateFeaturesV1(const QJsonObject &o,qsizetype workCount,qsizetype interferenceCount) {
    TemplateFeaturesV1 out;
    auto fail=[&]{out.planes.clear();out.error=QStringLiteral("模板角色/面片伴随文件无效");return out;};
    if(o.value("schema_version")!="pcv-template-roles-v1"||o.value("units")!="mm"||o.value("faces").toArray().isEmpty())return fail();
    QSet<int> ids;qsizetype nextWork=0,nextInterference=0;bool indexedWork=false,indexedInterference=false;
    for(auto item:o.value("faces").toArray()){auto f=item.toObject();TemplatePlaneV1 p;p.faceId=f.value("face_id").toInt();QString role=f.value("role").toString();if(role!="workpiece"&&role!="interference")return fail();p.interference=role=="interference";
        double first=f.value("first").toDouble(-1),count=f.value("count").toDouble(-1);auto &next=p.interference?nextInterference:nextWork;const auto total=p.interference?interferenceCount:workCount;
        auto indices=f.value("indices").toArray();
        if(p.faceId<=0||ids.contains(p.faceId)||count<1||std::floor(count)!=count||count>total)return fail();ids.insert(p.faceId);p.count=qsizetype(count);
        if(indices.isEmpty()){if(first!=next||count>total-next)return fail();p.first=next;next+=p.count;}
        else {if(p.interference)indexedInterference=true;else indexedWork=true;if(indices.size()!=p.count)return fail();int previous=-1;for(auto v:indices){double value=v.toDouble(-1);if(value<=previous||value>=total||std::floor(value)!=value)return fail();previous=int(value);p.indices.append(previous);}}
        auto plane=f.value("plane").toArray();if(plane.size()!=4)return fail();float *values[]={&p.model.a,&p.model.b,&p.model.c,&p.model.d};for(int k=0;k<4;++k){double v=plane.at(k).toDouble(std::numeric_limits<double>::quiet_NaN());if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())return fail();*values[k]=float(v);}
        if(!readVec(f.value("center"),p.center)||!readVec(f.value("normal"),p.normal)||!readVec(f.value("axis"),p.axis)||std::abs(p.normal.length()-1)>1e-3||std::abs(p.axis.length()-1)>1e-3||std::abs(QVector3D::dotProduct(p.axis,p.normal))>1e-3)return fail();
        if((QVector3D(p.model.a,p.model.b,p.model.c)-p.normal).length()>1e-3||std::abs(QVector3D::dotProduct(p.center,p.normal)+p.model.d)>.05f)return fail();
        for(auto v:f.value("vertices").toArray()){QVector3D value;if(!readVec(v,value))return fail();p.region.vertices.append(value);}
        for(auto v:f.value("triangles").toArray()){double index=v.toDouble(-1);if(index<0||std::floor(index)!=index||index>=p.region.vertices.size())return fail();p.region.triangleIndices.append(quint32(index));}
        for(auto v:f.value("boundary").toArray()){QVector3D value;if(!readVec(v,value))return fail();p.region.boundaryVertices.append(value);}
        int boundaryEnd=0;for(auto v:f.value("ranges").toArray()){auto r=v.toArray();if(r.size()!=2)return fail();int a=r.at(0).toInt(-1),n=r.at(1).toInt(-1);if(a!=boundaryEnd||n<4||n>p.region.boundaryVertices.size()-a||p.region.boundaryVertices.at(a)!=p.region.boundaryVertices.at(a+n-1))return fail();p.region.boundaryRanges.append({a,n});boundaryEnd+=n;}
        if(p.region.triangleIndices.isEmpty()||p.region.triangleIndices.size()%3||boundaryEnd!=p.region.boundaryVertices.size())return fail();
        out.planes.append(std::move(p));
    }
    bool hasWork=false;for(const auto &p:out.planes)if(!p.interference)hasWork=true;
    if(!hasWork||(!indexedWork&&nextWork!=workCount)||(!indexedInterference&&nextInterference!=interferenceCount))return fail();out.ok=true;return out;
}
}
