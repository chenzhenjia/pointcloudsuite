#include <pcv/registration/rigid_estimation_v1.h>
#include <pcv/registration/handeye_transform.h>
#include <QQuaternion>
#include <algorithm>
#include <cmath>
namespace pointcloud {
namespace {
bool finite(const QVector3D &p) {return std::isfinite(p.x())&&std::isfinite(p.y())&&std::isfinite(p.z());}
bool frame(QVector3D x,QVector3D y,QMatrix4x4 &m) {
    if(!finite(x)||!finite(y)||x.lengthSquared()<1e-12f)return false;
    x.normalize(); y-=x*QVector3D::dotProduct(x,y);
    if(y.lengthSquared()<1e-10f)return false;
    y.normalize(); const auto z=QVector3D::crossProduct(x,y);
    m.setColumn(0,QVector4D(x,0));m.setColumn(1,QVector4D(y,0));m.setColumn(2,QVector4D(z,0));return true;
}
}
QMatrix4x4 composeRigidV1(const QMatrix4x4 &a,const QMatrix4x4 &b){return a*b;}
double rotationDifferenceDegreesV1(const QMatrix4x4 &a,const QMatrix4x4 &b) {
    double trace=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j)trace+=double(a(i,j))*b(i,j);
    return std::acos(std::clamp((trace-1)*.5,-1.0,1.0))*180/3.14159265358979323846;
}
int rigidConstraintRankV1(const QVector<QVector3D> &positions,const QVector<QVector3D> &normals) {
    if(positions.size()!=normals.size()||positions.size()<6)return 0;
    QVector3D center;for(auto p:positions){if(!finite(p))return 0;center+=p;}center/=float(positions.size());
    double scale=0;for(auto p:positions)scale=std::max(scale,double((p-center).length()));if(scale<1e-6)return 0;
    double h[6][6]{};
    for(qsizetype i=0;i<positions.size();++i){if(!finite(normals.at(i))||normals.at(i).lengthSquared()<1e-10f)return 0;
        auto n=normals.at(i).normalized(),cross=QVector3D::crossProduct((positions.at(i)-center)/float(scale),n);
        double j[6]={n.x(),n.y(),n.z(),cross.x(),cross.y(),cross.z()};for(int a=0;a<6;++a)for(int b=0;b<6;++b)h[a][b]+=j[a]*j[b];}
    for(int iteration=0;iteration<150;++iteration){int p=0,q=1;for(int a=0;a<6;++a)for(int b=a+1;b<6;++b)if(std::abs(h[a][b])>std::abs(h[p][q])){p=a;q=b;}
        if(std::abs(h[p][q])<1e-10)break;double angle=.5*std::atan2(2*h[p][q],h[q][q]-h[p][p]),c=std::cos(angle),s=std::sin(angle);
        for(int k=0;k<6;++k){double a=h[k][p],b=h[k][q];h[k][p]=c*a-s*b;h[k][q]=s*a+c*b;}
        for(int k=0;k<6;++k){double a=h[p][k],b=h[q][k];h[p][k]=c*a-s*b;h[q][k]=s*a+c*b;}}
    double largest=0;for(int k=0;k<6;++k)largest=std::max(largest,h[k][k]);int rank=0;
    for(int k=0;k<6;++k)if(h[k][k]>std::max(1e-8,largest*1e-4))++rank;return rank;
}
RigidEstimateV1 alignFramesV1(QVector3D so,QVector3D sx,QVector3D sy,QVector3D to,QVector3D tx,QVector3D ty) {
    RigidEstimateV1 r;QMatrix4x4 s,t;
    if(!finite(so)||!finite(to)||!frame(sx,sy,s)||!frame(tx,ty,t)){r.error=QStringLiteral("方向不足以定义刚体坐标系");return r;}
    s.setColumn(3,QVector4D(so,1));t.setColumn(3,QVector4D(to,1));
    r.targetFromSource=composeRigidV1(t,s.inverted());r.ok=validateRigidTransform(r.targetFromSource,&r.error);return r;
}
RigidEstimateV1 alignPlanePairV1(QVector3D so,QVector3D sn,QVector3D s2,QVector3D sn2,QVector3D to,QVector3D tn,QVector3D t2,QVector3D tn2) {
    auto result=alignFramesV1(so,sn,sn2,to,tn,tn2);if(!result.ok)return result;tn.normalize();tn2.normalize();
    const double cosine=QVector3D::dotProduct(tn,tn2),determinant=1-cosine*cosine;
    if(determinant<1e-8||!finite(s2)||!finite(t2)){result.ok=false;result.error=QStringLiteral("平面方向不足");return result;}
    const double d1=QVector3D::dotProduct(tn,to-result.targetFromSource.map(so));
    const double d2=QVector3D::dotProduct(tn2,t2-result.targetFromSource.map(s2));
    auto correction=tn*float((d1-cosine*d2)/determinant)+tn2*float((d2-cosine*d1)/determinant);
    result.targetFromSource.setColumn(3,QVector4D(result.targetFromSource.column(3).toVector3D()+correction,1));
    result.ok=validateRigidTransform(result.targetFromSource,&result.error);return result;
}
RigidEstimateV1 estimateRigidV1(const QVector<QVector3D> &s,const QVector<QVector3D> &t) {
    RigidEstimateV1 r;
    if(s.size()!=t.size()||s.size()<3){r.error=QStringLiteral("刚体估计至少需要三组对应点");return r;}
    double cs[3]{},ct[3]{};
    for(qsizetype i=0;i<s.size();++i){if(!finite(s.at(i))||!finite(t.at(i))){r.error=QStringLiteral("非有限对应点");return r;}for(int k=0;k<3;++k){cs[k]+=s.at(i)[k];ct[k]+=t.at(i)[k];}}
    for(int k=0;k<3;++k){cs[k]/=s.size();ct[k]/=s.size();}
    double h[3][3]{},spread=0,area=0;QVector3D axis;
    for(qsizetype i=0;i<s.size();++i){QVector3D v;
        for(int k=0;k<3;++k)v[k]=float(s.at(i)[k]-cs[k]);
        if(v.lengthSquared()>axis.lengthSquared())axis=v;
        spread+=v.lengthSquared();
        for(int a=0;a<3;++a)for(int b=0;b<3;++b)h[a][b]+=(s.at(i)[a]-cs[a])*(t.at(i)[b]-ct[b]);
    }
    for(const auto &p:s)area+=QVector3D::crossProduct(p-QVector3D(float(cs[0]),float(cs[1]),float(cs[2])),axis).lengthSquared();
    if(spread<1e-10||area<spread*axis.lengthSquared()*1e-8){r.error=QStringLiteral("对应点共线或重合");return r;}
    const double xx=h[0][0],xy=h[0][1],xz=h[0][2],yx=h[1][0],yy=h[1][1],yz=h[1][2],zx=h[2][0],zy=h[2][1],zz=h[2][2];
    double n[4][4]={{xx+yy+zz,yz-zy,zx-xz,xy-yx},{yz-zy,xx-yy-zz,xy+yx,zx+xz},{zx-xz,xy+yx,-xx+yy-zz,yz+zy},{xy-yx,zx+xz,yz+zy,-xx-yy+zz}},v[4][4]{};
    for(int i=0;i<4;++i)v[i][i]=1;
    for(int iteration=0;iteration<80;++iteration){int p=0,q=1;for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)if(std::abs(n[i][j])>std::abs(n[p][q])){p=i;q=j;}
        if(std::abs(n[p][q])<1e-12*(spread+1))break;
        const double angle=.5*std::atan2(2*n[p][q],n[q][q]-n[p][p]),c=std::cos(angle),sn=std::sin(angle);
        for(int k=0;k<4;++k){const double a=n[k][p],b=n[k][q];n[k][p]=c*a-sn*b;n[k][q]=sn*a+c*b;}
        for(int k=0;k<4;++k){const double a=n[p][k],b=n[q][k];n[p][k]=c*a-sn*b;n[q][k]=sn*a+c*b;const double x=v[k][p],y=v[k][q];v[k][p]=c*x-sn*y;v[k][q]=sn*x+c*y;}
    }
    int best=0;for(int i=1;i<4;++i)if(n[i][i]>n[best][best])best=i;
    QQuaternion quaternion{float(v[0][best]),float(v[1][best]),float(v[2][best]),float(v[3][best])};
    r.targetFromSource.rotate(quaternion.normalized());
    const QVector3D center{float(cs[0]),float(cs[1]),float(cs[2])};
    const auto translation=QVector3D(float(ct[0]),float(ct[1]),float(ct[2]))-r.targetFromSource.mapVector(center);
    r.targetFromSource.setColumn(3,QVector4D(translation,1));
    r.ok=validateRigidTransform(r.targetFromSource,&r.error);
    double sum=0;for(qsizetype i=0;i<s.size();++i)sum+=(r.targetFromSource.map(s.at(i))-t.at(i)).lengthSquared();r.rmsMm=std::sqrt(sum/s.size());return r;
}
}
