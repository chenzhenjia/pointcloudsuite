#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <pcv/planefitting/reference_plane_match_v2.h>
#include <pcv/planefitting/high_side_plane_candidates_v1.h>
#include "plane_eigen_v1.h"
#include "plane_connectivity_v2.h"

#include <QElapsedTimer>
#include <QVector2D>
#include <QtMath>
#include <algorithm>
#include <array>
#include <future>
#include <system_error>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>
#include <utility>

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
    QVector<int> pointBuckets;
    std::shared_ptr<pcv::ProcessingBudget::Lease> bucketLease;
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
                                                    const std::function<bool()> &cancelled,
                                                    const IndexPreparationOptions &options) {
    PlaneSelectionIndexResult result; QElapsedTimer timer; timer.start();
    const auto stopped=[&] { if(cancelled && cancelled()) {result.cancelled=true;result.error=QStringLiteral("cancelled");return true;} return false; };
    if(stopped()) return result;
    if(points.size()<100 || points.size()>std::numeric_limits<int>::max()) {result.error=QStringLiteral("单击选面至少需要100个点，点数不得超过int索引范围");return result;}
    try {
        auto index=std::make_shared<PlaneSelectionIndex>(); index->points=points;
        if(options.cachePointBuckets) {
            const size_t bytes=size_t(points.size())*sizeof(int);
            if(options.processingBudget)index->bucketLease=options.processingBudget->acquire(bytes);
            if(!options.processingBudget||index->bucketLease)index->pointBuckets.fill(-1,points.size());
        }
        QVector3D low(std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max());
        QVector3D high=-low; int validSampleCount=0;
        const int step=qMax(1,int(points.size()/4096));
        for(int i=0;i<points.size();i+=step) if(finite(points.at(i))) {
            const auto p=position(points.at(i)); ++validSampleCount;
            for(int axis=0;axis<3;++axis) {low[axis]=qMin(low[axis],p[axis]);high[axis]=qMax(high[axis],p[axis]);}
        }
        if(validSampleCount<3) {result.error=QStringLiteral("有效点不足");return result;}
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
            if(!index->pointBuckets.isEmpty())index->pointBuckets[i]=it->second;
            auto &bucket=index->buckets[size_t(it->second)];bucket.indices.push_back(i);bucket.moments.add(p-index->origin);
        }
        result.gridMs=timer.nsecsElapsed()/1e6;
        QElapsedTimer normalTimer;normalTimer.start();
        // Read the finished grid through a const view. Each worker owns its
        // models; the original dz/dy/dx reduction order is unchanged.
        const PlaneSelectionIndex *view=index.get();
        struct Normal { Model model;bool valid=false; };
        // A worker that observes cancellation reports it, so its remaining
        // buckets are never published with default models.
        struct NormalChunk { std::vector<Normal> output;bool cancelled=false; };
        const auto normals=[view,&cancelled](size_t begin,size_t end) {
            NormalChunk chunk;
            chunk.output.resize(end-begin);
            for(size_t i=begin;i<end;++i) {
                if(cancelled&&cancelled()) {chunk.cancelled=true;break;}
                const auto &bucket=view->buckets.at(i);Moments moments;
                for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int neighbor=view->find({bucket.key.x+dx,bucket.key.y+dy,bucket.key.z+dz});
                    if(neighbor>=0)moments.append(view->buckets.at(size_t(neighbor)).moments);
                }
                auto &normal=chunk.output.at(i-begin);normal.valid=pca(moments,view->origin,&normal.model);
            }
            return chunk;
        };
        const auto normalsSerial=[&](size_t begin,size_t end) {
            for(size_t i=begin;i<end;++i) {
                if(stopped())return false;
                auto &bucket=index->buckets.at(i);
                Moments moments;
                for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int neighbor=view->find({bucket.key.x+dx,bucket.key.y+dy,bucket.key.z+dz});
                    if(neighbor>=0)moments.append(view->buckets.at(size_t(neighbor)).moments);
                }
                bucket.hasLocalModel=pca(moments,index->origin,&bucket.localModel);
            }
            return true;
        };
        int workers=options.parallelEnabled?qBound(1,options.maxParallelTasks,4):1;
        const size_t bucketCount=index->buckets.size();
        constexpr size_t windowSize=4096;
        std::shared_ptr<pcv::ProcessingBudget::Lease> normalLease;
        if(options.processingBudget) {
            normalLease=options.processingBudget->acquire(windowSize*size_t(workers)*sizeof(Normal));
            if(!normalLease)workers=1;
        }
        if(workers==1) {
            for(size_t begin=0;begin<bucketCount;) {
                const size_t end=qMin(bucketCount,begin+windowSize);
                if(!normalsSerial(begin,end))return result;
                begin=end;
            }
        } else {
            for(size_t begin=0;begin<bucketCount;) {
                std::vector<std::pair<size_t,std::future<NormalChunk>>> futures;
                size_t serialBegin=bucketCount,serialEnd=bucketCount;
                for(int worker=0;worker<workers&&begin<bucketCount;++worker) {
                    const size_t end=qMin(bucketCount,begin+windowSize);
                    try {
                        futures.emplace_back(begin,std::async(std::launch::async,[&normals,begin,end]{return normals(begin,end);}));
                    } catch(const std::system_error &) {
                        serialBegin=begin;serialEnd=end;workers=1;
                    }
                    begin=end;
                    if(serialBegin<bucketCount)break;
                }
                std::vector<std::pair<size_t,NormalChunk>> chunks;chunks.reserve(futures.size());
                bool chunkCancelled=false;
                for(auto &entry:futures) {
                    auto chunk=entry.second.get();chunkCancelled=chunkCancelled||chunk.cancelled;
                    chunks.emplace_back(entry.first,std::move(chunk));
                }
                if(chunkCancelled) {result.cancelled=true;result.error=QStringLiteral("cancelled");return result;}
                if(stopped())return result;
                for(const auto &entry:chunks)for(size_t i=0;i<entry.second.output.size();++i) {
                    auto &bucket=index->buckets.at(entry.first+i);
                    bucket.localModel=entry.second.output.at(i).model;bucket.hasLocalModel=entry.second.output.at(i).valid;
                }
                if(serialBegin<bucketCount) {
                    if(!normalsSerial(serialBegin,serialEnd)||!normalsSerial(begin,bucketCount))return result;
                    break;
                }
            }
        }
        result.normalsMs=normalTimer.nsecsElapsed()/1e6;
        result.pointBucketCacheUsed=!index->pointBuckets.isEmpty();
        result.optimizationBytes=size_t(index->pointBuckets.size())*sizeof(int);
        if(stopped()) return result;
        result.index=std::move(index);result.preparationMs=timer.nsecsElapsed()/1e6;
    } catch(const std::bad_alloc &) {result.outOfMemory=true;result.error=QStringLiteral("内存不足，无法准备单击选面索引");}
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
        result.samplingAxisU=axes.u;
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
        int classificationTasks=options.parallelEnabled?qBound(1,options.maxParallelTasks,4):1;
        std::shared_ptr<pcv::ProcessingBudget::Lease> classifyLease;
        if(classificationTasks>1&&options.processingBudget) {
            classifyLease=options.processingBudget->acquire(size_t(points.size())*sizeof(int)*2);
            if(!classifyLease)classificationTasks=1;
        }
        // Private reusable range buffers; worker t writes only scratch[t].
        // Reserving the exact maximum prevents QVector growth from escaping
        // the optional memory reservation. Reduction remains original order.
        std::vector<QVector<int>> scratch(static_cast<size_t>(classificationTasks));
        if(classificationTasks>1)for(int t=0;t<classificationTasks;++t)
            scratch.at(size_t(t)).reserve(qsizetype(qint64(points.size())*(t+1)/classificationTasks-qint64(points.size())*t/classificationTasks));
        const auto classify=[&]() {
            const auto work=[&points,&index,&cancelled,usableView,model,tolerance=options.surfaceToleranceMm](int begin,int end,QVector<int> &output) {
                output.clear();
                for(int i=begin;i<end;++i) {
                    if((i&4095)==0&&cancelled()){output.clear();return;}
                    const auto &p=points.at(i);if(!finite(p)||residual(model,p)>tolerance)continue;
                    const int cell=index->pointBuckets.isEmpty()?index->find(index->key(position(p))):index->pointBuckets.at(i);
                    if(cell<0)continue;
                    if(!usableView->at(cell)) {
                        const auto &local=index->buckets.at(size_t(cell)).localModel;
                        if(residual(model,p)>residual(local,p)+tolerance*0.25f)continue;
                    }
                    output.push_back(i);
                }
            };
            if(classificationTasks==1) {QVector<int> output;work(0,int(points.size()),output);return output;}
            std::vector<std::future<void>> futures;
            bool launchFailed=false;
            for(int t=1;t<classificationTasks;++t) {
                const int begin=int(qint64(points.size())*t/classificationTasks),end=int(qint64(points.size())*(t+1)/classificationTasks);
                auto *output=&scratch.at(size_t(t));
                try {futures.push_back(std::async(std::launch::async,[&work,begin,end,output]{work(begin,end,*output);}));}
                catch(const std::system_error &) {launchFailed=true;break;}
            }
            if(launchFailed) {
                for(auto &future:futures)future.get();
                classificationTasks=1;QVector<int> output;work(0,int(points.size()),output);return output;
            }
            work(0,int(points.size()/classificationTasks),scratch.at(0));
            for(auto &future:futures)future.get();
            QVector<int> output;
            qsizetype count=0;for(const auto &range:scratch)count+=range.size();output.reserve(count);
            for(const auto &range:scratch)output.append(range);
            return output;
        };
        std::shared_ptr<pcv::ProcessingBudget::Lease> reuseLease;
        if(options.processingBudget)reuseLease=options.processingBudget->acquire(size_t(points.size())*(sizeof(QVector2D)+sizeof(float)+sizeof(int)));
        const bool reuseBuffers=!options.processingBudget||bool(reuseLease);
        QVector<QVector2D> projected;
        QVector<float> distances;
        QVector<int> connectivityQueue;
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
            QElapsedTimer projectionTimer;projectionTimer.start();
            projected.clear();projected.reserve(classified.size());int seedLocal=-1;
            constexpr int projectionChunkSize=65536;
            int projectionTasks=options.parallelEnabled&&classified.size()>=100000?qBound(1,options.maxParallelTasks,4):1;
            std::shared_ptr<pcv::ProcessingBudget::Lease> projectionLease;
            if(options.processingBudget)while(projectionTasks>1) {
                projectionLease=options.processingBudget->acquire(size_t(projectionTasks)*projectionChunkSize*sizeof(QVector2D));
                if(projectionLease)break;
                --projectionTasks;
            }
            // One scalar expression for both paths. Workers only return private
            // contiguous ranges; the coordinator merges after every future joins.
            const auto projectRange=[&points,&classified,seedPosition,projectionU,projectionV,&cancelled,seed]
                (int begin,int end,QVector<QVector2D> &output,int &foundSeed) {
                for(int local=begin;local<end;++local) {
                    if((local&4095)==0&&cancelled())return;
                    const int id=classified.at(local);const auto p=position(points.at(id))-seedPosition;
                    output.push_back({QVector3D::dotProduct(p,projectionU),QVector3D::dotProduct(p,projectionV)});
                    if(id==seed)foundSeed=local;
                }
            };
            if(projectionTasks==1)projectRange(0,int(classified.size()),projected,seedLocal);
            else {
                struct ProjectionChunk {QVector<QVector2D> points;int seed=-1;};
                for(int begin=0;begin<classified.size();) {
                    std::vector<std::future<ProjectionChunk>> futures;
                    bool launchFailed=false;
                    for(int task=0;task<projectionTasks&&begin<classified.size();++task) {
                        const int end=int(qMin<qsizetype>(classified.size(),qsizetype(begin)+projectionChunkSize));
                        try {
                            futures.push_back(std::async(std::launch::async,[&projectRange,begin,end] {
                                ProjectionChunk chunk;chunk.points.reserve(end-begin);
                                projectRange(begin,end,chunk.points,chunk.seed);return chunk;
                            }));
                        } catch(const std::system_error &) {launchFailed=true;break;}
                        begin=end;
                    }
                    std::vector<ProjectionChunk> chunks;chunks.reserve(futures.size());
                    for(auto &future:futures)chunks.push_back(future.get());
                    for(const auto &chunk:chunks) {
                        projected.append(chunk.points);if(chunk.seed>=0)seedLocal=chunk.seed;
                    }
                    if(cancelled())break;
                    if(launchFailed) {projectRange(begin,int(classified.size()),projected,seedLocal);break;}
                }
            }
            projectionLease.reset();
            result.projectionMs+=projectionTimer.nsecsElapsed()/1e6;
            if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            if(seedLocal<0)return fail(SingleClickStatus::Ambiguous,QStringLiteral("点击点不在目标平面内，请重选"));
            const auto component=detail::selectProjectedComponents(projected,seedLocal,result.connectivityRadiusUMm,
                result.connectivityRadiusVMm,options.maximumGapMm,cancelled,reuseBuffers?&connectivityQueue:nullptr);
            result.normalConnectivityMs+=component.normalMs;
            result.timings.connectivityMs+=stage.nsecsElapsed()/1e6;result.gapMergeMs+=component.gapMs;stage.restart();
            if(cancelled())return fail(SingleClickStatus::Cancelled,QStringLiteral("cancelled"));
            if(!reuseBuffers)projected={};
            QVector<int> selected;selected.reserve(component.localIndices.size());
            for(int local:component.localIndices)selected.push_back(classified.at(local));
            result.mergedRegionCount=component.mergedRegions;result.refinementIterations=iteration+1;
            if(selected.size()<100)return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("目标平面不足100点，请重选或改用三点选面"));
            const bool unchanged=selected==result.planeIndices;result.planeIndices=std::move(selected);
            if(unchanged) {result.refinementConverged=true;break;}
            if(iteration==5)break; // The final set always belongs to the published model.
            // Trim high residuals for model estimation only; classification
            // still uses the user's full tolerance and can recover those points.
            distances.clear();distances.reserve(result.planeIndices.size());
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
            if(!reuseBuffers)distances={};
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
    } catch(const std::bad_alloc &) {result.outOfMemory=true;return fail(SingleClickStatus::InsufficientSupport,QStringLiteral("内存不足，保留原选择"));}
}
ReferencePlaneMatchResultV2 matchReferencePlaneV2(
    const QVector<pointcloud::Point3D> &points, const PlaneSelectionIndexPtr &index,
    const PlaneModel &reference, const ReferencePlaneMatchOptionsV2 &options) {
    ReferencePlaneMatchResultV2 result;
    const auto cancelled = [&] { return options.selection.isCancelled && options.selection.isCancelled(); };
    const auto cancel = [&] {
        result.status=ReferencePlaneMatchStatusV2::Cancelled; result.candidates.clear();
        result.error=QStringLiteral("cancelled"); return result;
    };
    if (cancelled()) return cancel();
    QVector3D normal(reference.a,reference.b,reference.c);
    const float length=normal.length();
    if (!index || points.isEmpty() || index->points.constData()!=points.constData() || index->points.size()!=points.size()
        || !std::isfinite(length) || length<1e-6f || !std::isfinite(reference.d)
        || !std::isfinite(options.maximumDistanceMm) || options.maximumDistanceMm<=0
        || !std::isfinite(options.maximumNormalAngleDegrees) || options.maximumNormalAngleDegrees<=0
        || options.maximumNormalAngleDegrees>90 || options.maximumSeeds<1 || options.maximumSeeds>64
        || !std::isfinite(options.selection.surfaceToleranceMm)||options.selection.surfaceToleranceMm<=0
        || !std::isfinite(options.selection.maximumGapMm)||options.selection.maximumGapMm<0||options.selection.maximumGapMm>50
        || options.selection.maxParallelTasks<1||options.selection.maxParallelTasks>4) {
        result.status=ReferencePlaneMatchStatusV2::InvalidInput;
        result.error=QStringLiteral("参考面、完整点云索引或匹配参数无效"); return result;
    }
    normal/=length; const float offset=reference.d/length;
    const float cosine=std::cos(qDegreesToRadians(options.maximumNormalAngleDegrees));
    const auto eligible = [&](const Bucket &bucket) {
        if (!bucket.hasLocalModel || bucket.localModel.thinness>0.05f || bucket.indices.isEmpty()) return false;
        if (std::abs(QVector3D::dotProduct(normal,bucket.localModel.n))<cosine) return false;
        const auto p=position(points.at(bucket.indices.at(bucket.indices.size()/2)));
        return std::abs(QVector3D::dotProduct(normal,p)+offset)<=options.maximumDistanceMm;
    };
    try {
        for (size_t i=0;i<index->buckets.size();++i) {
            if ((i&4095)==0 && cancelled()) return cancel();
            if (eligible(index->buckets.at(i))) ++result.eligibleBuckets;
        }
        const int count=qMin(options.maximumSeeds,result.eligibleBuckets);
        int ordinal=0, next=0;
        for (size_t i=0;i<index->buckets.size() && next<count;++i) {
            if (cancelled()) return cancel();
            const auto &bucket=index->buckets.at(i);
            if (!eligible(bucket)) continue;
            const qint64 target=count==1?result.eligibleBuckets/2
                :qint64(next)*(result.eligibleBuckets-1)/(count-1);
            if (ordinal++!=target) continue;
            ++next;
            const int seed=bucket.indices.at(bucket.indices.size()/2);
            bool covered=false;
            for (const auto &candidate:std::as_const(result.candidates))
                if (std::binary_search(candidate.selection.planeIndices.cbegin(),candidate.selection.planeIndices.cend(),seed)) covered=true;
            if (covered) continue;
            ++result.attemptedSeeds;
            result.attemptedSeedIndices.append(seed);
            auto selected=selectPlaneAtPoint(points,index,seed,options.selection);
            if (cancelled() || selected.status==SingleClickStatus::Cancelled) return cancel();
            if (selected.outOfMemory) {
                result.status=ReferencePlaneMatchStatusV2::Error; result.outOfMemory=true;
                result.error=selected.error; result.candidates.clear(); return result;
            }
            if (!selected.ok()) continue;
            const QVector3D candidateNormal(selected.model.a,selected.model.b,selected.model.c);
            const float dot=qBound(0.0f,std::abs(QVector3D::dotProduct(normal,candidateNormal.normalized())),1.0f);
            const float angle=qRadiansToDegrees(std::acos(dot));
            const auto bounds=calculateBoundsCenter(points,selected.planeIndices);
            if (!bounds.ok) continue;
            const float distance=std::abs(QVector3D::dotProduct(normal,bounds.center)+offset);
            if (angle>options.maximumNormalAngleDegrees || distance>options.maximumDistanceMm) continue;
            std::sort(selected.planeIndices.begin(),selected.planeIndices.end());
            bool duplicate=false;
            for (auto &candidate:result.candidates) {
                const auto &a=candidate.selection.planeIndices; const auto &b=selected.planeIndices;
                qsizetype x=0,y=0,overlap=0;
                while (x<a.size() && y<b.size()) {
                    if (((x+y)&4095)==0 && cancelled()) return cancel();
                    if (a.at(x)==b.at(y)) {++overlap;++x;++y;}
                    else if (a.at(x)<b.at(y)) ++x; else ++y;
                }
                if (overlap*5>=qMin(a.size(),b.size())*4) {
                    duplicate=true;
                    if (b.size()>a.size()) candidate={seed,std::move(selected),angle,distance};
                    break;
                }
            }
            if (!duplicate) result.candidates.append({seed,std::move(selected),angle,distance});
            if (result.candidates.size()>1) {
                result.status=ReferencePlaneMatchStatusV2::Ambiguous;
                result.error=QStringLiteral("发现至少两个不同候选，请手动选择工件面或明确跳过"); return result;
            }
        }
        result.status=result.candidates.size()==1?ReferencePlaneMatchStatusV2::Unique:ReferencePlaneMatchStatusV2::NotFound;
        if (result.candidates.isEmpty()) result.error=QStringLiteral("搜索范围内未找到稳定工件面，请手动选择或明确跳过");
        return result;
    } catch (const std::bad_alloc &) {
        result.status=ReferencePlaneMatchStatusV2::Error; result.candidates.clear(); result.outOfMemory=true;
        result.error=QStringLiteral("参考面匹配内存不足，未发布候选"); return result;
    }
}

HighSidePlaneSearchResultV1 findHighSidePlaneCandidatesV1(
    const QVector<pointcloud::Point3D> &points, const PlaneSelectionIndexPtr &index,
    const QVector3D &selectedCenter, const QVector<int> &excludedIndices,
    const HighSidePlaneSearchOptionsV1 &options) {
    HighSidePlaneSearchResultV1 result;
    const auto cancelled = [&] { return options.selection.isCancelled && options.selection.isCancelled(); };
    if (cancelled()) {
        result.status=HighSidePlaneSearchStatusV1::Cancelled;
        result.error=QStringLiteral("cancelled");
        return result;
    }
    if (!index || points.isEmpty() || index->points.constData()!=points.constData()
        || index->points.size()!=points.size() || !std::isfinite(selectedCenter.x())
        || !std::isfinite(selectedCenter.y()) || !std::isfinite(selectedCenter.z())
        || !std::isfinite(options.minimumHeightMm)
        || options.minimumHeightMm<0 || options.minimumPointCount<1
        || options.maximumSeeds<1 || options.maximumSeeds>64
        || !std::isfinite(options.selection.surfaceToleranceMm) || options.selection.surfaceToleranceMm<=0
        || !std::isfinite(options.selection.maximumGapMm) || options.selection.maximumGapMm<0
        || options.selection.maximumGapMm>50 || options.selection.maxParallelTasks<1
        || options.selection.maxParallelTasks>4) {
        result.status=HighSidePlaneSearchStatusV1::InvalidInput;
        result.error=QStringLiteral("高侧平面搜索输入或参数无效");
        return result;
    }
    try {
        QSet<int> excluded;
        excluded.reserve(excludedIndices.size());
        for (int i:excludedIndices) if (i>=0&&i<points.size()) excluded.insert(i);
        const auto bucketHeight=[&](const Bucket &bucket) {
            const auto p=position(points.at(bucket.indices.at(bucket.indices.size()/2)));
            return p.z()-selectedCenter.z();
        };
        const auto eligible=[&](const Bucket &bucket) {
            return bucket.hasLocalModel && bucket.localModel.thinness<=0.05f
                && !bucket.indices.isEmpty() && bucketHeight(bucket)>=options.minimumHeightMm;
        };
        for (size_t i=0;i<index->buckets.size();++i) {
            if ((i&4095)==0&&cancelled()) {
                result.status=HighSidePlaneSearchStatusV1::Cancelled;
                result.error=QStringLiteral("cancelled");
                result.candidates.clear();
                return result;
            }
            if (eligible(index->buckets.at(i))) ++result.eligibleBuckets;
        }
        if (result.eligibleBuckets==0) {
            result.status=HighSidePlaneSearchStatusV1::Success;
            return result;
        }
        const int seedCount=qMin(options.maximumSeeds,result.eligibleBuckets);
        int eligibleOrdinal=0, selectedOrdinal=0;
        QVector<QVector<int>> candidateIndices;
        for (size_t i=0;i<index->buckets.size()&&selectedOrdinal<seedCount;++i) {
            if (cancelled()) {
                result.status=HighSidePlaneSearchStatusV1::Cancelled;
                result.error=QStringLiteral("cancelled");
                result.candidates.clear();
                return result;
            }
            const auto &bucket=index->buckets.at(i);
            if (!eligible(bucket)) continue;
            const qint64 target=seedCount==1?result.eligibleBuckets/2
                :qint64(selectedOrdinal)*(result.eligibleBuckets-1)/(seedCount-1);
            if (eligibleOrdinal++!=target) continue;
            ++selectedOrdinal;
            const int seed=bucket.indices.at(bucket.indices.size()/2);
            if (excluded.contains(seed)) continue;
            ++result.attemptedSeeds;
            auto selection=selectPlaneAtPoint(points,index,seed,options.selection);
            if (cancelled()||selection.status==SingleClickStatus::Cancelled) {
                result.status=HighSidePlaneSearchStatusV1::Cancelled;
                result.error=QStringLiteral("cancelled");
                result.candidates.clear();
                return result;
            }
            if (selection.outOfMemory) {
                result.status=HighSidePlaneSearchStatusV1::Error;
                result.error=selection.error;
                result.candidates.clear();
                return result;
            }
            if (!selection.ok()||selection.planeIndices.size()<options.minimumPointCount) continue;
            std::sort(selection.planeIndices.begin(),selection.planeIndices.end());
            int excludedOverlap=0;
            for (int pointIndex:selection.planeIndices) if (excluded.contains(pointIndex)) ++excludedOverlap;
            if (qint64(excludedOverlap)*5>=qint64(selection.planeIndices.size())*4) continue;
            const auto bounds=calculateBoundsCenter(points,selection.planeIndices);
            if (!bounds.ok) continue;
            const float height=bounds.center.z()-selectedCenter.z();
            if (height<options.minimumHeightMm) continue;
            bool duplicate=false;
            for (int c=0;c<candidateIndices.size();++c) {
                const auto &prior=candidateIndices.at(c);
                qsizetype a=0,b=0,overlap=0;
                while (a<prior.size()&&b<selection.planeIndices.size()) {
                    if (((a+b)&4095)==0&&cancelled()) {
                        result.status=HighSidePlaneSearchStatusV1::Cancelled;
                        result.error=QStringLiteral("cancelled");
                        result.candidates.clear();
                        return result;
                    }
                    if (prior.at(a)==selection.planeIndices.at(b)) {++overlap;++a;++b;}
                    else if (prior.at(a)<selection.planeIndices.at(b)) ++a;
                    else ++b;
                }
                if (overlap*5>=qMin(prior.size(),selection.planeIndices.size())*4) {
                    duplicate=true;
                    if (selection.planeIndices.size()>prior.size()) {
                        candidateIndices[c]=selection.planeIndices;
                        result.candidates[c]={seed,int(selection.planeIndices.size()),height};
                    }
                    break;
                }
            }
            if (duplicate) continue;
            candidateIndices.append(selection.planeIndices);
            result.candidates.append({seed,int(selection.planeIndices.size()),height});
        }
        result.status=HighSidePlaneSearchStatusV1::Success;
        return result;
    } catch (const std::bad_alloc &) {
        result.status=HighSidePlaneSearchStatusV1::Error;
        result.candidates.clear();
        result.error=QStringLiteral("高侧平面搜索内存不足");
        return result;
    }
}
} // namespace pcv::planefitting
