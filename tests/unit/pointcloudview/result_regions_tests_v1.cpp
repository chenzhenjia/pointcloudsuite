#include "result_regions_v1.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
using namespace pcv::demo;
namespace {
bool check(bool value,const char *message){if(!value)std::cerr<<message<<'\n';return value;}
ResultRegionsRequestV1 fixture() {
    ResultRegionsRequestV1 r;r.budget=std::make_shared<pcv::ProcessingBudget>(16u*1024u*1024u);
    r.corrections={QMatrix4x4{},QMatrix4x4{}};r.frames.resize(2);r.referenceKeptIndex=0;
    r.reference.normal={0,0,1};r.reference.center={0,0,4};
    // Concave occupancy with a hole and a disjoint island; all complete points are used.
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        if((x==2&&y==2)||(x>=5&&y>=5))continue;
        r.points.append({float(x),float(y),4});
    }
    r.points.append({20,20,4});
    r.basis.valid=true;r.basis.model.c=1;r.basis.model.d=-4;
    r.basis.axisU={1,0,0};r.basis.spacingU=r.basis.spacingV=1;
    auto &red=r.frames[1].mask;
    red.vertices={{0,0,8},{1,0,8},{1,1,8},{0,1,8}};red.triangleIndices={0,1,2,0,2,3};
    red.boundaryVertices={{0,0,8},{1,0,8},{1,1,8},{0,1,8},{0,0,8}};red.boundaryRanges={{0,5}};
    r.corrections[1].translate(3,5,2);r.corrections[1].rotate(20,0,0,1);
    return r;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);auto r=fixture();const auto original=r.points;const auto retained=r;
    Q_UNUSED(retained);
    const auto complete=buildResultRegionsV1(r);
    if(!check(complete.workpieceOk&&complete.interferenceOk,"full regions failed"))return 1;
    if(!check(complete.workpiece.triangleIndices.size()==r.points.size()*6
        &&complete.workpiece.boundaryRanges.size()==3,"full occupancy/holes/islands lost"))return 2;
    if(!check(complete.interference.vertices.first()==r.corrections.at(1).map(r.frames.at(1).mask.vertices.first())
        &&complete.interference.boundaryVertices.last()==r.corrections.at(1).map(r.frames.at(1).mask.boundaryVertices.last()),"red correction mismatch"))return 3;
    if(!check(complete.interference.vertices.first().z()==10,"red height projected onto workpiece"))return 4;
    r.removed={0};const auto trimmed=buildResultRegionsV1(r);
    if(!check(trimmed.workpieceOk&&trimmed.basis.spacingU==complete.basis.spacingU
        &&trimmed.workpiece.triangleIndices.size()==complete.workpiece.triangleIndices.size()-6
        &&trimmed.interference.vertices==complete.interference.vertices,"trim affected wrong geometry"))return 5;
    if(!check(trimmed.basis.gridOriginU==complete.basis.gridOriginU&&trimmed.basis.gridOriginV==complete.basis.gridOriginV,
        "trim moved fixed grid origin"))return 14;
    for(int i=0;i<r.points.size();++i)r.removed.insert(i);
    const auto empty=buildResultRegionsV1(r);
    if(!check(empty.workpieceOk&&empty.workpiece.vertices.isEmpty()&&!empty.interference.vertices.isEmpty(),"empty trim lost interference"))return 6;
    r.removed.clear();r.basis={};r.reference.normal=QVector3D(-0.2f,0,1).normalized();
    r.corrections[0].translate(0,0,3);r.corrections[0].rotate(7,0,1,0);
    const auto resolved=buildResultRegionsV1(r);
    if(!check(resolved.workpieceOk&&resolved.basis.spacingU>0&&resolved.basis.spacingV>0,"complete spacing not resolved"))return 7;
    const auto n=r.corrections.at(0).mapVector(r.reference.normal).normalized();
    const auto c=r.corrections.at(0).map(r.reference.center);
    for(const auto &v:resolved.workpiece.vertices)
        if(!check(std::abs(QVector3D::dotProduct(n,v-c))<1e-4f,"workpiece not on corrected reference"))return 8;
    r.basis=resolved.basis;r.removed={0,1,2};
    const auto after=buildResultRegionsV1(r);
    if(!check(after.workpieceOk&&after.basis.spacingU==resolved.basis.spacingU&&after.basis.spacingV==resolved.basis.spacingV,"spacing changed after trim"))return 9;
    auto low=r;low.budget=std::make_shared<pcv::ProcessingBudget>(0);
    const auto failed=buildResultRegionsV1(low);
    if(!check(!failed.workpieceOk&&!failed.error.isEmpty()&&!failed.interference.vertices.isEmpty(),"mask failure lost valid red region"))return 10;
    auto bad=r;bad.corrections.removeLast();
    if(!check(!buildResultRegionsV1(bad).workpieceOk,"invalid frame mapping accepted"))return 11;
    r.isCancelled=[] {return true;};const auto cancelled=buildResultRegionsV1(r);
    if(!check(cancelled.cancelled&&cancelled.workpiece.vertices.isEmpty()&&cancelled.interference.vertices.isEmpty(),"cancel published partial mesh"))return 12;
    if(!check(r.points.constData()==original.constData()&&r.budget->used()==0,"mutated points or retained lease"))return 13;
    return 0;
}
