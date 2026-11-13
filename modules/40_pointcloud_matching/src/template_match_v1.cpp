#include <pcv/matching/template_match_v1.h>
#include <pcv/registration/rigid_estimation_v1.h>
#include <pcv/registration/handeye_transform.h>
#include <QElapsedTimer>
#include <QJsonArray>
#include <map>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <climits>
namespace pcv::matching {
namespace {
using Plane=pcv::workpiece::TemplatePlaneV1;
QVector3D xyz(const pointcloud::Point3D &p){return {p.x,p.y,p.z};}
bool finite(QVector3D p){return std::isfinite(p.x())&&std::isfinite(p.y())&&std::isfinite(p.z());}
using Key=std::array<qint64,3>;
// Deterministic bounded-neighborhood nearest-neighbor search, retaining exact point indices.
struct Grid {
    double size;
    const QVector<pointcloud::Point3D> &points;
    struct Entry {Key cell;int index;};
    QVector<Entry> entries;
    Grid(double s,const QVector<pointcloud::Point3D> &p):size(s),points(p){}
    Key key(QVector3D p)const{return {qint64(std::floor(p.x()/size)),qint64(std::floor(p.y()/size)),qint64(std::floor(p.z()/size))};}
    bool build(const std::function<bool()> &cancel={}) {
        entries.reserve(points.size());for(int i=0;i<points.size();++i){if((i&4095)==0&&cancel&&cancel())return false;entries.append({key(xyz(points.at(i))),i});}
        std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b){return a.cell<b.cell||(a.cell==b.cell&&a.index<b.index);});return !cancel||!cancel();
    }
    int nearest(QVector3D p,double &distance)const{
        if(!finite(p)||std::max({std::abs(double(p.x())),std::abs(double(p.y())),std::abs(double(p.z()))})/size>double(std::numeric_limits<qint64>::max()/4))return -1;
        const auto k=key(p);int best=-1;double squared=distance*distance;
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){Key search{k[0]+x,k[1]+y,k[2]+z};auto it=std::lower_bound(entries.cbegin(),entries.cend(),search,[](const Entry &a,const Key &b){return a.cell<b;});
            for(;it!=entries.cend()&&it->cell==search;++it){int i=it->index;const auto d=xyz(points.at(i))-p;double ds=double(d.x())*d.x()+double(d.y())*d.y()+double(d.z())*d.z();if(ds<squared||(ds==squared&&(best<0||i<best))){squared=ds;best=i;}}}
        if(best>=0)distance=std::sqrt(squared);return best;
    }
};
QVector<int> subset(qsizetype count,int limit){QVector<int> ids;qsizetype n=std::min<qsizetype>(count,limit);ids.reserve(n);for(qsizetype i=0;i<n;++i)ids.append(int(quint64(i)*quint64(count)/quint64(n)));return ids;}
QVector<QVector3D> anchors(const Plane &p) {
    QVector<QVector3D> out{p.center};
    // Boundary extrema and hole centers preserve concave/hole placement evidence.
    auto y=QVector3D::crossProduct(p.normal,p.axis).normalized();
    for(auto direction:{p.axis,y})for(int sign:{-1,1}){
        double best=-std::numeric_limits<double>::infinity();QVector3D v=p.center;
        for(auto b:p.region.boundaryVertices){double score=sign*QVector3D::dotProduct(b-p.center,direction);if(score>best){best=score;v=b;}}if(std::isfinite(best))out.append(v);}
    for(auto range:p.region.boundaryRanges){if(out.size()>=9)break;QVector3D center;for(int i=0;i<range.second;++i)center+=p.region.boundaryVertices.at(range.first+i);if(range.second)out.append(center/float(range.second));}
    return out;
}
QVector<QVector3D> directions(const Plane &p) {
    QVector<QVector3D> result{p.axis,QVector3D::crossProduct(p.normal,p.axis).normalized()};
    QVector<QVector3D> edges;
    for(auto range:p.region.boundaryRanges)for(int k=0;k<range.second-1;++k){auto delta=p.region.boundaryVertices.at(range.first+k+1)-p.region.boundaryVertices.at(range.first+k);if(delta.lengthSquared()>1e-8f)edges.append(delta);}
    std::stable_sort(edges.begin(),edges.end(),[](auto a,auto b){return a.lengthSquared()>b.lengthSquared();});
    for(auto direction:edges){direction.normalize();bool found=false;for(auto previous:result)if(std::abs(QVector3D::dotProduct(direction,previous))>.995f){found=true;break;}if(!found)result.append(direction);if(result.size()>=6)break;}return result;
}
bool duplicatePose(const QMatrix4x4 &a,const QMatrix4x4 &b){return (a.column(3).toVector3D()-b.column(3).toVector3D()).length()<=1&&pointcloud::rotationDifferenceDegreesV1(a,b)<=1;}
struct Evaluation {TemplateMatchCandidateV1 candidate;QVector<QVector3D> source,target;};
Evaluation evaluate(const TemplateMatchRequestV1 &r,const Grid &grid,const QVector<int> &indices,const QMatrix4x4 &transform,bool correspondences) {
    Evaluation e;e.candidate.targetFromTemplate=transform;double sum=0;
    if(correspondences){e.source.reserve(indices.size());e.target.reserve(indices.size());}
    for(qsizetype k=0;k<indices.size();++k){if((k&4095)==0&&r.isCancelled&&r.isCancelled())return {};
        const auto p=xyz(r.templatePoints.at(indices.at(k)));const auto q=transform.map(p);double d=r.parameters.correspondenceMm;int i=grid.nearest(q,d);
        if(i<0)continue;++e.candidate.matchedPoints;sum+=d*d;if(correspondences){e.source.append(p);e.target.append(xyz(r.targetPoints.at(i)));}}
    e.candidate.coverage=double(e.candidate.matchedPoints)/std::max<qsizetype>(1,indices.size());
    e.candidate.rmsMm=e.candidate.matchedPoints?std::sqrt(sum/e.candidate.matchedPoints):r.parameters.correspondenceMm;
    e.candidate.score=e.candidate.coverage/(1+e.candidate.rmsMm/r.parameters.correspondenceMm);return e;
}
// Plane residuals do not constrain in-plane translation/rotation. Require spatially
// distributed boundary correspondences and an isolated boundary-residual minimum.
double observable(const TemplateMatchRequestV1 &r,const Grid &grid,const QMatrix4x4 &m) {
    QVector<QVector3D> boundary,boundaryNormals;QVector<QVector3D> normals;
    for(const auto &p:r.targetPlanes)if(!p.interference)for(auto range:p.region.boundaryRanges)for(int k=0;k<range.second-1;++k){int i=range.first+k;auto tangent=p.region.boundaryVertices.at(i+1)-p.region.boundaryVertices.at(i);auto normal=QVector3D::crossProduct(p.normal,tangent).normalized();if(normal.isNull())continue;
        int samples=std::clamp(int(std::min(256.0,std::ceil(tangent.length()/r.parameters.correspondenceMm))),1,256);
        for(int j=0;j<samples&&boundary.size()<200000;++j){boundary.append(p.region.boundaryVertices.at(i)+tangent*(float(j)/samples));boundaryNormals.append(normal);}}
    if(boundary.size()<6)return 0;
    QVector<pointcloud::Point3D> boundaryPoints;boundaryPoints.reserve(boundary.size());for(auto p:boundary)boundaryPoints.append({p.x(),p.y(),p.z()});
    Grid edges(r.parameters.correspondenceMm,boundaryPoints);if(!edges.build(r.isCancelled))return 0;
    QVector<QVector3D> hits,constraintPositions,constraintNormals;int tested=0,matched=0;double spread=0;
    for(const auto &p:r.templatePlanes)if(!p.interference){normals.append(p.normal);
        QVector<QVector3D> samples;for(auto range:p.region.boundaryRanges)for(int k=0;k<range.second-1&&samples.size()<20000;++k){auto a=p.region.boundaryVertices.at(range.first+k),delta=p.region.boundaryVertices.at(range.first+k+1)-a;
            int n=std::clamp(int(std::min(256.0,std::ceil(delta.length()/r.parameters.correspondenceMm))),1,256);for(int j=0;j<n&&samples.size()<20000;++j)samples.append(a+delta*(float(j)/n));}
        for(int i:subset(samples.size(),2048)){if(r.isCancelled&&r.isCancelled())return 0;++tested;auto q=m.map(samples.at(i));double distance=r.parameters.correspondenceMm;int edge=edges.nearest(q,distance);if(edge>=0){++matched;hits.append(q);constraintPositions.append(q);constraintNormals.append(boundaryNormals.at(edge));constraintPositions.append(q);constraintNormals.append(m.mapVector(p.normal).normalized());}}}
    if(tested<6||matched<6||double(matched)/tested<.15||pointcloud::rigidConstraintRankV1(constraintPositions,constraintNormals)<6)return 0;
    QVector3D center;for(auto v:hits)center+=v;center/=float(hits.size());QVector3D axis;for(auto v:hits)if((v-center).lengthSquared()>axis.lengthSquared())axis=v-center;
    for(auto v:hits)spread+=QVector3D::crossProduct(v-center,axis).lengthSquared();
    if(axis.lengthSquared()<r.parameters.correspondenceMm*r.parameters.correspondenceMm*4||spread<axis.lengthSquared()*axis.lengthSquared()*.001)return 0;
    // Numerical six-DOF point-to-plane and boundary constraints. Test independent
    // perturbations along the matched plane frame; circular in-plane symmetry fails.
    if(normals.isEmpty())return 0;
    auto n=m.mapVector(normals.first()).normalized();QVector3D u=QVector3D::crossProduct(n,std::abs(n.z())<.9f?QVector3D(0,0,1):QVector3D(1,0,0)).normalized(),v=QVector3D::crossProduct(n,u);
    auto boundaryCost=[&](QVector3D shift,float degrees){double sum=0;QMatrix4x4 rotation;rotation.rotate(degrees,n);
        for(auto q:hits){q=center+rotation.mapVector(q-center)+shift;double distance=r.parameters.correspondenceMm*3;
            // The edge grid has 1-cell neighborhood: use capped distance only within its search radius.
            distance=r.parameters.correspondenceMm;if(edges.nearest(q,distance)<0)distance=r.parameters.correspondenceMm;sum+=distance*distance;}return sum/hits.size();};
    const double base=boundaryCost({},0),step=r.parameters.correspondenceMm;
    for(auto d:{u,v})if(std::min(boundaryCost(d*float(step),0),boundaryCost(-d*float(step),0))<base+step*step*.015)return 0;
    const float angle=float(std::clamp(step/std::sqrt(axis.lengthSquared())*180/3.14159265358979323846,2.0,10.0));
    if(std::min(boundaryCost({},angle),boundaryCost({},-angle))<base+step*step*.015)return 0;
    Q_UNUSED(grid);return double(matched)/tested;
}
}
TemplateMatchResultV1 matchTemplateV1(const TemplateMatchRequestV1 &r) {
    TemplateMatchResultV1 out;QElapsedTimer timer;timer.start();const auto &p=r.parameters;
    auto stopped=[&]{return r.isCancelled&&r.isCancelled();};
    auto fail=[&](QString error){out.candidates.clear();out.error=error;out.cancelled=stopped();out.timings.insert("total_ms",timer.nsecsElapsed()/1e6);return out;};
    if(!std::isfinite(p.correspondenceMm)||p.correspondenceMm<=0||!std::isfinite(p.minimumCoverage)||p.minimumCoverage<=0||p.minimumCoverage>1||p.minimumMatches<100||p.maximumHypotheses<1||p.maximumHypotheses>4096||p.refineCount<1||p.refineCount>16||p.maximumCandidates<1||p.maximumCandidates>5||r.templatePoints.size()<p.minimumMatches||r.targetPoints.size()<p.minimumMatches||r.templatePoints.size()>INT_MAX||r.targetPoints.size()>INT_MAX)return fail(QStringLiteral("匹配参数或有效点数不满足约束"));
    for(const auto *planes:{&r.templatePlanes,&r.targetPlanes})for(const auto &plane:*planes){
        if(!finite(plane.center)||!finite(plane.normal)||!finite(plane.axis)||std::abs(plane.normal.length()-1)>.001f||std::abs(plane.axis.length()-1)>.001f||std::abs(QVector3D::dotProduct(plane.normal,plane.axis))>.001f)return fail(QStringLiteral("平面特征坐标或方向无效"));
        for(auto v:plane.region.boundaryVertices)if(!finite(v))return fail(QStringLiteral("边界包含非有限点"));
        for(auto range:plane.region.boundaryRanges)if(range.first<0||range.second<4||range.first>plane.region.boundaryVertices.size()-range.second||plane.region.boundaryVertices.at(range.first)!=plane.region.boundaryVertices.at(range.first+range.second-1))return fail(QStringLiteral("边界未闭合或索引越界"));
    }
    for(const auto *points:{&r.templatePoints,&r.targetPoints})for(qsizetype i=0;i<points->size();++i){if((i&16383)==0&&stopped())return fail(QStringLiteral("已取消"));auto v=xyz(points->at(i));if(!finite(v)||std::max({std::abs(double(v.x())),std::abs(double(v.y())),std::abs(double(v.z()))})/p.correspondenceMm>double(std::numeric_limits<qint64>::max()/4))return fail(QStringLiteral("匹配输入包含非有限或超范围坐标"));}
    // Flat sorted cell entries avoid per-cell allocations; reserve auxiliary work too.
    auto lease=r.budget?r.budget->acquire(size_t(r.targetPoints.size())*sizeof(Grid::Entry)+size_t(r.templatePoints.size())*8+16u*1024u*1024u):nullptr;
    if(r.budget&&!lease)return fail(QStringLiteral("匹配索引超出共享优化预算"));
    Grid grid(p.correspondenceMm,r.targetPoints);if(!grid.build(r.isCancelled))return fail(QStringLiteral("已取消"));
    out.timings.insert("target_grid_ms",timer.nsecsElapsed()/1e6);QElapsedTimer search;search.start();
    const auto coarse=subset(r.templatePoints.size(),2048),refine=subset(r.templatePoints.size(),20000);
    QVector<TemplateMatchCandidateV1> best;
    auto hypothesis=[&](const QMatrix4x4 &m){if(stopped()||out.evaluatedHypotheses>=p.maximumHypotheses){out.searchLimited=true;return;}
        ++out.evaluatedHypotheses;if(r.progress)r.progress(QStringLiteral("全局姿态搜索"),out.evaluatedHypotheses,p.maximumHypotheses);
        auto c=evaluate(r,grid,coarse,m,false).candidate;if(c.coverage<.05)return;
        for(const auto &b:best)if(duplicatePose(b.targetFromTemplate,m))return;
        best.append(c);std::stable_sort(best.begin(),best.end(),[](const auto &a,const auto &b){return a.score>b.score;});if(best.size()>p.refineCount)best.resize(p.refineCount);
    };
    for(const auto &s:r.templatePlanes)if(!s.interference)for(const auto &t:r.targetPlanes)if(!t.interference){if(stopped())return fail(QStringLiteral("已取消"));
        if(out.evaluatedHypotheses>=p.maximumHypotheses){out.searchLimited=true;break;}
        auto sa=anchors(s),ta=anchors(t);
        // Nonparallel face pairs supply orientation hypotheses independent of principal boundary axes.
        for(const auto &s2:r.templatePlanes)if(!s2.interference&&std::abs(QVector3D::dotProduct(s.normal,s2.normal))<.98f)
            for(const auto &t2:r.targetPlanes)if(!t2.interference&&std::abs(std::abs(QVector3D::dotProduct(s.normal,s2.normal))-std::abs(QVector3D::dotProduct(t.normal,t2.normal)))<.08f)
                for(int a:{-1,1})for(int b:{-1,1}){auto fit=pointcloud::alignPlanePairV1(s.center,s.normal,s2.center,s2.normal,t.center,t.normal*float(a),t2.center,t2.normal*float(b));if(fit.ok)hypothesis(fit.targetFromSource);}
        for(auto sx:directions(s))for(auto tx:directions(t))for(int sign:{-1,1})for(int tangentSign:{-1,1}){
            if(stopped())return fail(QStringLiteral("已取消"));if(out.evaluatedHypotheses>=p.maximumHypotheses){out.searchLimited=true;break;}
            for(auto so:sa)for(auto to:ta){auto fit=pointcloud::alignFramesV1(so,sx,s.normal,to,tx*float(tangentSign),t.normal*float(sign));if(fit.ok)hypothesis(fit.targetFromSource);if(out.evaluatedHypotheses>=p.maximumHypotheses)break;}}
    }
    if(out.evaluatedHypotheses>=p.maximumHypotheses)out.searchLimited=true;
    out.timings.insert("search_ms",search.nsecsElapsed()/1e6);QElapsedTimer refinement;refinement.start();
    int refinedCount=0;
    for(auto c:best){if(stopped())return fail(QStringLiteral("已取消"));
        if(r.progress)r.progress(QStringLiteral("六自由度精化与完整模板核验"),refinedCount++,int(best.size()));
        for(int iteration=0;iteration<25;++iteration){auto e=evaluate(r,grid,refine,c.targetFromTemplate,true);if(e.source.size()<3)break;auto fit=pointcloud::estimateRigidV1(e.source,e.target);if(!fit.ok)break;
            bool converged=(fit.targetFromSource.column(3).toVector3D()-c.targetFromTemplate.column(3).toVector3D()).length()<.001&&pointcloud::rotationDifferenceDegreesV1(fit.targetFromSource,c.targetFromTemplate)<.001;
            c.targetFromTemplate=fit.targetFromSource;if(converged)break;if(stopped())return fail(QStringLiteral("已取消"));}
        // Final metrics use every original template point, never extrapolate sampled counts.
        const auto full=subset(r.templatePoints.size(),int(r.templatePoints.size()));c=evaluate(r,grid,full,c.targetFromTemplate,false).candidate;
        c.observability=observable(r,grid,c.targetFromTemplate);c.acceptable=c.coverage>=p.minimumCoverage&&c.matchedPoints>=p.minimumMatches&&c.observability>0&&pointcloud::validateRigidTransform(c.targetFromTemplate);
        if(!c.acceptable)continue;bool duplicate=false;for(const auto &existing:out.candidates)if(duplicatePose(c.targetFromTemplate,existing.targetFromTemplate)){duplicate=true;break;}if(!duplicate)out.candidates.append(c);
    }
    if(stopped())return fail(QStringLiteral("已取消"));
    std::stable_sort(out.candidates.begin(),out.candidates.end(),[](const auto &a,const auto &b){return a.score>b.score;});if(out.candidates.size()>p.maximumCandidates)out.candidates.resize(p.maximumCandidates);
    out.success=!out.candidates.isEmpty();if(!out.success)out.error=out.searchLimited?QStringLiteral("搜索受限，未找到合格候选"):QStringLiteral("没有满足覆盖、空间分布与可观测性要求的候选；几何证据不足或模板不匹配");
    out.timings.insert("refine_verify_ms",refinement.nsecsElapsed()/1e6);out.timings.insert("total_ms",timer.nsecsElapsed()/1e6);return out;
}
QJsonObject matchParametersJsonV1(const TemplateMatchParametersV1 &p){return {{"correspondence_mm",p.correspondenceMm},{"minimum_coverage",p.minimumCoverage},{"minimum_matches",p.minimumMatches},{"maximum_hypotheses",p.maximumHypotheses},{"refine_count",p.refineCount},{"maximum_candidates",p.maximumCandidates}};}
QJsonObject matchCandidateJsonV1(const TemplateMatchCandidateV1 &c){QJsonArray matrix;for(int row=0;row<4;++row){QJsonArray values;for(int col=0;col<4;++col)values.append(double(c.targetFromTemplate(row,col)));matrix.append(values);}return {{"T_target_from_template",matrix},{"rms_mm",c.rmsMm},{"template_coverage",c.coverage},{"matched_points",qint64(c.matchedPoints)},{"boundary_observability",c.observability},{"acceptable",c.acceptable},{"score",c.score}};}
}
