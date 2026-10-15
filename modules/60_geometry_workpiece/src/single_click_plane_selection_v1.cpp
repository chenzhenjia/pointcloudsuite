#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include "plane_eigen_v1.h"
#include "plane_connectivity_v2.h"

#include <QElapsedTimer>
#include <QVector2D>
#include <QtMath>
#include <algorithm>
#include <array>
#include <future>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

namespace pcv::planefitting {
namespace {
struct Key {
    qint64 x, y, z;
    bool operator==(const Key &b) const { return x == b.x && y == b.y && z == b.z; }
};
struct KeyHash {
    size_t operator()(const Key &k) const {
        return std::hash<quint64>{}(quint64(k.x) * 73856093u ^ quint64(k.y) * 19349663u ^ quint64(k.z) * 83492791u);
    }
};
QVector3D position(const pointcloud::Point3D &p) { return {p.x, p.y, p.z}; }
bool finite(const pointcloud::Point3D &p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
struct Moments {
    int count = 0;
    double sum[3]{};
    double squares[3][3]{};
    void add(const QVector3D &p) {
        const double v[3]{p.x(), p.y(), p.z()}; ++count;
        for(int r=0;r<3;++r) { sum[r]+=v[r]; for(int c=0;c<3;++c) squares[r][c]+=v[r]*v[c]; }
    }
    void append(const Moments &m) {
        count+=m.count;
        for(int r=0;r<3;++r) { sum[r]+=m.sum[r]; for(int c=0;c<3;++c) squares[r][c]+=m.squares[r][c]; }
    }
};
struct Model {
    QVector3D n;
    float d = 0;
    float thinness = 1;
};
bool pca(const Moments &m, const QVector3D &origin, Model *out) {
    if(m.count<6) return false;
    double covariance[3][3], values[3], vectors[3][3];
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        covariance[r][c]=m.squares[r][c]/m.count-(m.sum[r]/m.count)*(m.sum[c]/m.count);
    detail::symmetricEigen3(covariance,values,vectors);
    if(!std::isfinite(values[0]) || values[1]<=qMax(values[0],1e-20)*1e-6) return false;
    out->n=QVector3D(float(vectors[0][2]),float(vectors[1][2]),float(vectors[2][2])).normalized();
    if(out->n.z()<0) out->n=-out->n;
    const QVector3D center=origin+QVector3D(float(m.sum[0]/m.count),float(m.sum[1]/m.count),float(m.sum[2]/m.count));
    out->d=-QVector3D::dotProduct(out->n,center);
    out->thinness=float(qMax(0.0,values[2])/values[1]);
    return true;
}
float residual(const Model &m, const pointcloud::Point3D &p) { return std::abs(QVector3D::dotProduct(m.n,position(p))+m.d); }
bool sameDirection(const Model &a,const Model &b,float degrees) {
    return std::abs(QVector3D::dotProduct(a.n,b.n))>=std::cos(qDegreesToRadians(degrees));
}
struct Bucket {
    Key key;
    QVector<int> indices;
    Moments moments;
    Model localModel;
    bool hasLocalModel = false;
};
}

class PlaneSelectionIndex {
public:
    QVector<pointcloud::Point3D> points;
    QVector3D origin;
    float cellSize = 1;
    std::unordered_map<Key,int,KeyHash> lookup;
    std::vector<Bucket> buckets;
    Key key(const QVector3D &p) const {
        const QVector3D v=(p-origin)/cellSize;
        return {qint64(std::floor(double(v.x()))),qint64(std::floor(double(v.y()))),qint64(std::floor(double(v.z())))};
    }
    int find(const Key &k) const { const auto it=lookup.find(k); return it==lookup.end()?-1:it->second; }
};

PlaneSelectionIndexResult preparePlaneSelectionIndex(const QVector<pointcloud::Point3D> &points,
                                                    const std::function<bool()> &cancelled) {
    PlaneSelectionIndexResult result; QElapsedTimer timer; timer.start();
    const auto stopped=[&] { if(cancelled && cancelled()) {result.cancelled=true;result.error=QStringLiteral("cancelled");return true;} return false; };
    if(stopped()) return result;
    if(points.size()<100 || points.size()>std::numeric_limits<int>::max()) {result.error=QStringLiteral("单击选面至少需要100个点，点数不得超过int索引范围");return result;}
    try {
        auto index=std::make_shared<PlaneSelectionIndex>(); index->points=points;
        QVector3D low(std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max());
        QVector3D high=-low; int count=0;
        const int step=qMax(1,int(points.size()/4096));
        for(int i=0;i<points.size();i+=step) if(finite(points.at(i))) {
            const auto p=position(points.at(i)); ++count;
            for(int axis=0;axis<3;++axis) {low[axis]=qMin(low[axis],p[axis]);high[axis]=qMax(high[axis],p[axis]);}
        }
        if(count<3) {result.error=QStringLiteral("有效点不足");return result;}
        index->origin=low;
        std::array<float,3> spans{high.x()-low.x(),high.y()-low.y(),high.z()-low.z()};
        std::sort(spans.begin(),spans.end(),std::greater<float>());
        index->cellSize=qBound(0.2f,float(4*std::sqrt(double(spans[0])*spans[1]/points.size())),5.0f);
        for(int i=0;i<points.size();++i) {
            if((i&4095)==0 && stopped()) return result;
            if(!finite(points.at(i))) continue;
            const auto p=position(points.at(i)); const auto key=index->key(p);
            auto [it,inserted]=index->lookup.emplace(key,int(index->buckets.size()));
            if(inserted) index->buckets.push_back({key,{},{}});
            auto &bucket=index->buckets[size_t(it->second)];bucket.indices.push_back(i);bucket.moments.add(p-index->origin);
        }
        // Neighborhood normals are immutable and independent of a clicked
        // plane. Prepare them once, outside click-to-preview timing.
        for(auto &bucket:index->buckets) {
            if(stopped())return result;
            Moments moments;
            for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const int neighbor=index->find({bucket.key.x+dx,bucket.key.y+dy,bucket.key.z+dz});
                if(neighbor>=0)moments.append(index->buckets[size_t(neighbor)].moments);
            }
            bucket.hasLocalModel=pca(moments,index->origin,&bucket.localModel);
        }
        if(stopped()) return result;
        result.index=std::move(index);result.preparationMs=timer.nsecsElapsed()/1e6;
    } catch(const std::bad_alloc &) {result.error=QStringLiteral("内存不足，无法准备单击选面索引");}
    return result;
}

namespace {
QVector<int> neighborhood(const PlaneSelectionIndex &index,int seed,const std::function<bool()> &cancelled) {
    QVector<int> ids; const auto p=position(index.points.at(seed));const auto key=index.key(p);
    for(int radius=0;radius<=32;++radius) {
        if(cancelled && cancelled()) return {};
        for(int dz=-radius;dz<=radius;++dz) for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
            if(qMax(qMax(std::abs(dx),std::abs(dy)),std::abs(dz))!=radius) continue;
            const int cell=index.find({key.x+dx,key.y+dy,key.z+dz});
            if(cell>=0) ids.append(index.buckets[size_t(cell)].indices);
        }
        // A whole additional shell avoids an asymmetric nearest-point sample.
        if(ids.size()>=2048 || (radius>=3 && ids.size()>=1024)) break;
    }
    const auto &points=index.points;
    std::sort(ids.begin(),ids.end(),[&points,p](int a,int b) {
        const float da=(position(points.at(a))-p).lengthSquared(),db=(position(points.at(b))-p).lengthSquared();
        return da==db?a<b:da<db;
    });
    if(ids.size()>2048) ids.resize(2048);
    return ids;
}

bool localModel(const QVector<pointcloud::Point3D> &points,const QVector<int> &ids,int count,int seed,
                float tolerance,Model *out,float *support,const std::function<bool()> &cancelled) {
    std::mt19937 random(20260813u); int best=0;Model bestModel;
    for(int trial=0;trial<300;++trial) {
        if(cancelled && cancelled()) return false;
        const int a=int(random()%unsigned(count)),b=int(random()%unsigned(count)),c=int(random()%unsigned(count));
        if(a==b||a==c||b==c) continue;
        const auto pa=position(points.at(ids.at(a))),pb=position(points.at(ids.at(b))),pc=position(points.at(ids.at(c)));
        Model model;model.n=QVector3D::crossProduct(pb-pa,pc-pa);
        if(model.n.lengthSquared()<1e-12f) continue;
        model.n.normalize();model.d=-QVector3D::dotProduct(model.n,pa);
        if(residual(model,points.at(seed))>tolerance) continue;
        int inliers=0;for(int i=0;i<count;++i) if(residual(model,points.at(ids.at(i)))<=tolerance) ++inliers;
        if(inliers>best) {best=inliers;bestModel=model;}
    }
    if(best<count*0.8) return false;
    const auto origin=position(points.at(seed));
    for(int refine=0;refine<2;++refine) {
        Moments moments;for(int i=0;i<count;++i) if(residual(bestModel,points.at(ids.at(i)))<=tolerance) moments.add(position(points.at(ids.at(i)))-origin);
        Model refined;if(!pca(moments,origin,&refined)||refined.thinness>0.05f) return false;
        bestModel=refined;
    }
    int inliers=0;for(int i=0;i<count;++i) if(residual(bestModel,points.at(ids.at(i)))<=tolerance) ++inliers;
    *support=float(inliers)/count;*out=bestModel;
    return *support>=0.8f && residual(bestModel,points.at(seed))<=tolerance;
}

struct SamplingAxes {
    QVector3D u, v;
    float spacingU = 0, spacingV = 0;
};

SamplingAxes samplingAxes(const QVector<pointcloud::Point3D> &points, const QVector<int> &ids,
                          const Model &model, float tolerance, const std::function<bool()> &cancelled) {
    SamplingAxes axes;
    axes.u = QVector3D::crossProduct(model.n, std::abs(model.n.z()) < 0.9f ? QVector3D(0,0,1) : QVector3D(0,1,0)).normalized();
    axes.v = QVector3D::crossProduct(model.n, axes.u).normalized();
    QVector<QVector2D> local;
    const auto origin = position(points.at(ids.first()));
    for (int i : ids) if (residual(model, points.at(i)) <= tolerance) {
        const auto p = position(points.at(i)) - origin;
        local.push_back({QVector3D::dotProduct(p, axes.u), QVector3D::dotProduct(p, axes.v)});
    }
    // Align one axis to the dominant nearest-neighbor direction. This also
    // handles rotated line scans where area/count hides a large row spacing.
    constexpr int bins = 36;
    std::array<int,bins> histogram{};
    std::vector<QVector2D> nearestDeltas;
    const int step = qMax(1, int(local.size()/128));
    for (int i=0;i<local.size();i+=step) {
        if (cancelled()) return axes;
        float best=std::numeric_limits<float>::max(); QVector2D delta;
        for (int j=0;j<local.size();++j) {
            const auto d=local.at(j)-local.at(i);const float length=d.lengthSquared();
            if (length>1e-10f && length<best) {best=length;delta=d;}
        }
        if (best<std::numeric_limits<float>::max()) {
            if (delta.x()<0 || (delta.x()==0 && delta.y()<0)) delta=-delta;
            float angle=std::atan2(delta.y(),delta.x());if(angle<0)angle+=float(M_PI);
            ++histogram[size_t(qMin(bins-1,int(angle/float(M_PI)*bins)))]; nearestDeltas.push_back(delta.normalized());
        }
    }
    const int bestBin=int(std::max_element(histogram.begin(),histogram.end())-histogram.begin());
    const float dominant=(bestBin+0.5f)*float(M_PI)/bins;
    QVector2D direction(std::cos(dominant),std::sin(dominant)),average;
    for(auto d:nearestDeltas) {
        if(QVector2D::dotProduct(d,direction)<0)d=-d;
        if(QVector2D::dotProduct(d,direction)>std::cos(qDegreesToRadians(10.0f)))average+=d;
    }
    if(!average.isNull())direction=average.normalized();
    const auto originalU=axes.u,originalV=axes.v;
    axes.u=originalU*direction.x()+originalV*direction.y();
    axes.v=QVector3D::crossProduct(model.n,axes.u).normalized();
    const QVector2D transverse(-direction.y(),direction.x());
    QVector<float> distancesU,distancesV;
    for(int i=0;i<local.size();i+=step) {
        if(cancelled())return axes;
        float bestU=std::numeric_limits<float>::max(),bestV=bestU;
        for(int j=0;j<local.size();++j) {
            const auto d=local.at(j)-local.at(i);
            const float u=std::abs(QVector2D::dotProduct(d,direction)),v=std::abs(QVector2D::dotProduct(d,transverse));
            if(u>1e-5f && v<=u*0.35f)bestU=qMin(bestU,u);
            if(v>1e-5f && u<=v*0.35f)bestV=qMin(bestV,v);
        }
        if(bestU<std::numeric_limits<float>::max())distancesU.push_back(bestU);
        if(bestV<std::numeric_limits<float>::max())distancesV.push_back(bestV);
    }
    const auto median=[](QVector<float> &values) {
        if(values.isEmpty())return 0.0f;
        std::sort(values.begin(),values.end());return values.at(values.size()/2);
    };
    axes.spacingU=median(distancesU);axes.spacingV=median(distancesV);
    return axes;
}
} // namespace

SingleClickResult selectPlaneAtPoint(const QVector<pointcloud::Point3D> &points,const PlaneSelectionIndexPtr &index,
                                    int seed,const SingleClickOptions &options) {
    SingleClickResult result; QElapsedTimer total, stage; total.start(); stage.start();
    const auto cancelled=[&options] { return options.isCancelled && options.isCancelled(); };
    const auto fail=[&](SingleClickStatus status,const QString &message) {
        result.status=status; result.error=message; result.planeIndices.clear();
        result.timings.totalMs=total.nsecsElapsed()/1e6; return result;
    };
    if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
    if(!index || index->points.constData()!=points.constData() || index->points.size()!=points.size()
       || seed<0 || seed>=points.size() || !finite(points.at(seed))
       || !std::isfinite(options.surfaceToleranceMm) || options.surfaceToleranceMm<=0
       || options.maxParallelTasks<1 || options.maxParallelTasks>4
       || !std::isfinite(options.maximumGapMm) || options.maximumGapMm<0 || options.maximumGapMm>50)
        return fail(SingleClickStatus::InvalidInput,QStringLiteral("点击点、索引缓存或选面参数无效"));
    try {
        const auto ids=neighborhood(*index,seed,cancelled);
        if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
        if(ids.size()<64)return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("点击处邻域点不足，请点击平面内部或改用三点选面"));
        Model model,previous; bool havePrevious=false,stable=false;
        for(int count:{64,128,256,512,1024,2048}) {
            if(count>ids.size())break;
            Model candidate;float support=0;
            if(!localModel(points,ids,count,seed,options.surfaceToleranceMm,&candidate,&support,cancelled)) {havePrevious=false;continue;}
            if(havePrevious && sameDirection(candidate,previous,5)
               && std::abs(residual(candidate,points.at(seed))-residual(previous,points.at(seed)))<=options.surfaceToleranceMm*0.5f) {
                model=candidate;result.localSupportRatio=support;stable=true;
            }
            previous=candidate;havePrevious=true;
        }
        if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
        if(!stable)return fail(SingleClickStatus::Ambiguous,QStringLiteral("点击处平面不稳定或位于折边，请点击平面内部或改用三点选面"));
        result.timings.ransacMs=stage.nsecsElapsed()/1e6;stage.restart();
        const Model initial=model; const auto seedPosition=position(points.at(seed));
        const auto axes=samplingAxes(points,ids,model,options.surfaceToleranceMm,cancelled);
        if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
        if(axes.spacingU<=0 || axes.spacingV<=0)return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("无法估计平面两个方向的采样间距"));
        result.samplingSpacingUMm=axes.spacingU;result.samplingSpacingVMm=axes.spacingV;
        result.localSpacingMm=std::sqrt(axes.spacingU*axes.spacingV);
        result.connectivityRadiusUMm=qMax(1e-5f,axes.spacingU*1.65f);
        result.connectivityRadiusVMm=qMax(1e-5f,axes.spacingV*1.65f);
        result.connectivityRadiusMm=qMax(result.connectivityRadiusUMm,result.connectivityRadiusVMm);
        result.maximumGapMm=options.maximumGapMm;result.gridSizeMm=index->cellSize;
        // A clearly different local surface only supplies a point-level
        // membership test. Never reject a whole mixed cell near a fold.
        QVector<quint8> usableCells(qsizetype(index->buckets.size()),1);
        const QVector<quint8> *usableView=&usableCells;
        for(size_t cell=0;cell<index->buckets.size();++cell) {
            if((cell&1023)==0 && cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            const auto &bucket=index->buckets[cell];
            if(bucket.hasLocalModel && bucket.localModel.thinness<=0.01f && !sameDirection(bucket.localModel,initial,25))usableCells[qsizetype(cell)]=0;
        }
        result.timings.candidateMs=stage.nsecsElapsed()/1e6;stage.restart();
        const auto classify=[&]() {
            const int tasks=options.parallelEnabled?options.maxParallelTasks:1;
            const auto work=[&points,&index,&cancelled,usableView,model,tolerance=options.surfaceToleranceMm](int begin,int end) {
                QVector<int> output;
                for(int i=begin;i<end;++i) {
                    if((i&4095)==0 && cancelled())return QVector<int>{};
                    const auto &p=points.at(i);if(!finite(p)||residual(model,p)>tolerance)continue;
                    const int cell=index->find(index->key(position(p)));
                    if(cell<0)continue;
                    if(!usableView->at(cell)) {
                        const auto &local=index->buckets[size_t(cell)].localModel;
                        if(residual(model,p)>residual(local,p)+tolerance*0.25f)continue;
                    }
                    output.push_back(i);
                }
                return output;
            };
            std::vector<std::future<QVector<int>>> futures;
            // Declare outputs after futures: work captures live until every
            // async task is joined, including exceptional allocation exits.
            for(int task=1;task<tasks;++task) {
                const int begin=int(qint64(points.size())*task/tasks),end=int(qint64(points.size())*(task+1)/tasks);
                futures.push_back(std::async(std::launch::async,[&work,begin,end]{return work(begin,end);}));
            }
            auto output=work(0,int(points.size()/tasks));for(auto &future:futures)output.append(future.get());return output;
        };
        QVector<int> previousClassified;
        QVector3D previousNormal;
        for(int iteration=0;iteration<6;++iteration) {
            const auto classified=classify();
            result.timings.classificationMs+=stage.nsecsElapsed()/1e6;stage.restart();
            if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            // With an unchanged normal, identical full-frame candidates have
            // identical projected regions. Otherwise reproject and reconnect.
            if(iteration>0 && model.n==previousNormal && classified==previousClassified) {
                result.refinementIterations=iteration+1;result.refinementConverged=true;break;
            }
            previousClassified=classified;previousNormal=model.n;
            const auto projectionU=(axes.u-model.n*QVector3D::dotProduct(axes.u,model.n)).normalized();
            const auto projectionV=QVector3D::crossProduct(model.n,projectionU).normalized();
            QVector<QVector2D> projected;projected.reserve(classified.size());int seedLocal=-1;
            for(int local=0;local<classified.size();++local) {
                if((local&4095)==0 && cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
                const int id=classified.at(local);const auto p=position(points.at(id))-seedPosition;
                projected.push_back({QVector3D::dotProduct(p,projectionU),QVector3D::dotProduct(p,projectionV)});
                if(id==seed)seedLocal=local;
            }
            if(seedLocal<0)return fail(SingleClickStatus::Ambiguous,QStringLiteral("点击点不在目标平面内，请重选"));
            const auto component=detail::selectProjectedComponents(projected,seedLocal,result.connectivityRadiusUMm,
                result.connectivityRadiusVMm,options.maximumGapMm,cancelled);
            result.timings.connectivityMs+=stage.nsecsElapsed()/1e6;result.gapMergeMs+=component.gapMs;stage.restart();
            if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            QVector<int> selected;selected.reserve(component.localIndices.size());
            for(int local:component.localIndices)selected.push_back(classified.at(local));
            result.mergedRegionCount=component.mergedRegions;result.refinementIterations=iteration+1;
            if(selected.size()<100)return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("目标平面不足100点，请重选或改用三点选面"));
            const bool unchanged=selected==result.planeIndices;result.planeIndices=std::move(selected);
            if(unchanged) {result.refinementConverged=true;break;}
            if(iteration==5)break; // The final set always belongs to the published model.
            // Trim high residuals for model estimation only; classification
            // still uses the user's full tolerance and can recover those points.
            QVector<float> distances;distances.reserve(result.planeIndices.size());
            for(int id:result.planeIndices) {
                if((distances.size()&4095)==0 && cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
                distances.push_back(residual(model,points.at(id)));
            }
            std::nth_element(distances.begin(),distances.begin()+distances.size()*9/10,distances.end());
            const float trim=qMax(options.surfaceToleranceMm*0.25f,distances.at(distances.size()*9/10));
            Moments moments;
            for(int id:result.planeIndices) {
                if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
                if(residual(model,points.at(id))<=trim)moments.add(position(points.at(id))-seedPosition);
            }
            Model refined;
            if(!pca(moments,seedPosition,&refined) || !sameDirection(refined,initial,5) || refined.thinness>0.05f
               || residual(refined,points.at(seed))>options.surfaceToleranceMm)
                return fail(SingleClickStatus::Ambiguous,QStringLiteral("完整区域与点击处平面不一致，请重选"));
            model=refined;result.timings.pcaMs+=stage.nsecsElapsed()/1e6;stage.restart();
        }
        double squared=0;float maximum=0;
        for(int id:result.planeIndices) {
            if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            const float distance=residual(model,points.at(id));squared+=double(distance)*distance;maximum=qMax(maximum,distance);
        }
        result.rmsMm=float(std::sqrt(squared/result.planeIndices.size()));
        result.model={model.n.x(),model.n.y(),model.n.z(),model.d,int(result.planeIndices.size()),result.rmsMm,maximum};
        result.status=SingleClickStatus::Success;result.timings.assemblyMs=stage.nsecsElapsed()/1e6;result.timings.totalMs=total.nsecsElapsed()/1e6;
        return result;
    } catch(const std::bad_alloc &) {return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("内存不足，保留原选择"));}
}
} // namespace pcv::planefitting
