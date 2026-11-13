#include <selected_plane_tasks.h>
#include <QtTest>
#include <algorithm>
#include <limits>
using namespace pcv::planefitting;
class ReferencePlaneMatchTestsV2 : public QObject {
    Q_OBJECT
    static QVector<pointcloud::Point3D> patch(float x0=0,float z=0,float spacing=0.5f) {
        QVector<pointcloud::Point3D> points;for(int y=0;y<40;++y)for(int x=0;x<40;++x)points.append({x0+x*spacing,y*spacing,z});return points;
    }
private slots:
    void reusedSelectionSnapshotMatchesFreshFit() {
        pointcloud::RobotBaseFrame source;source.fullPoints=patch();
        const auto retained=source.fullPoints;Q_UNUSED(retained);
        for(int i=0;i<source.fullPoints.size();++i){source.sourceIndices.append(i*2);source.scanRatios.append(float(i)/source.fullPoints.size());}
        const auto prepared=preparePlaneSelectionIndex(source.fullPoints);QVERIFY(prepared.index);
        const auto matched=matchReferencePlaneV2(source.fullPoints,prepared.index,{});
        QCOMPARE(matched.status,ReferencePlaneMatchStatusV2::Unique);
        QCOMPARE(matched.seedSelectionMs.size(),matched.attemptedSeeds);
        const auto &candidate=matched.candidates.first();
        auto token=std::make_shared<std::atomic_bool>(false);
        auto budget=std::make_shared<pcv::ProcessingBudget>(512u*1024u*1024u);
        const auto reused=pcv::ui::snapshotFromSingleClickV2(source,{candidate.seedIndex},0.4f,candidate.selection,token,budget);
        const auto fresh=pcv::ui::fitFullPlaneV2(source,{candidate.seedIndex},0.4f,3,true,prepared.index,token,2,budget);
        QVERIFY2(reused.ok,qPrintable(reused.error));QVERIFY(fresh.ok);
        QCOMPARE(reused.planeIndices,candidate.selection.planeIndices);QCOMPARE(reused.planeIndices,fresh.planeIndices);
        QCOMPARE(reused.selected.normal,fresh.selected.normal);QCOMPARE(reused.selected.center,fresh.selected.center);
        QCOMPARE(reused.selected.rmsMm,fresh.selected.rmsMm);QCOMPARE(reused.selected.seedSourceIndices,fresh.selected.seedSourceIndices);
        auto reusedParameters=reused.selected.selectionParameters,freshParameters=fresh.selected.selectionParameters;
        for(const char *key:{"algorithm_ms","local_fit_ms","grid_growth_ms","classification_ms","connectivity_ms","pca_ms","gap_merge_ms"}) {
            reusedParameters.remove(key);freshParameters.remove(key);
        }
        QCOMPARE(reusedParameters,freshParameters);
        QCOMPARE(reused.selected.cloud.sourceIndices,fresh.selected.cloud.sourceIndices);
        QCOMPARE(reused.selected.cloud.scanRatios,fresh.selected.cloud.scanRatios);
        for(int j=0;j<reused.planeIndices.size();++j) {
            const int i=reused.planeIndices.at(j);const auto &p=reused.selected.cloud.fullPoints.at(j);
            QCOMPARE(p.x,source.fullPoints.at(i).x);QCOMPARE(p.y,source.fullPoints.at(i).y);QCOMPARE(p.z,source.fullPoints.at(i).z);
        }
        QCOMPARE(reused.planeMask.vertices,fresh.planeMask.vertices);
        QCOMPARE(reused.planeMask.triangleIndices,fresh.planeMask.triangleIndices);
        QCOMPARE(reused.planeMask.boundaryVertices,fresh.planeMask.boundaryVertices);
        QCOMPARE(reused.planeMask.boundaryRanges,fresh.planeMask.boundaryRanges);
        QVERIFY(reused.performance.value("snapshot_copy_ms").isDouble());QVERIFY(reused.performance.value("mask_ms").isDouble());
        auto invalid=candidate.selection;invalid.planeIndices.append(invalid.planeIndices.first());
        QVERIFY(!pcv::ui::snapshotFromSingleClickV2(source,{candidate.seedIndex},0.4f,invalid,token,budget).ok);
        invalid=candidate.selection;invalid.planeIndices.append(source.fullPoints.size());
        QVERIFY(!pcv::ui::snapshotFromSingleClickV2(source,{candidate.seedIndex},0.4f,invalid,token,budget).ok);
        token->store(true);const auto cancelled=pcv::ui::snapshotFromSingleClickV2(source,{candidate.seedIndex},0.4f,candidate.selection,token,budget);
        QVERIFY(!cancelled.ok);QVERIFY(cancelled.cancelled);QVERIFY(cancelled.selected.cloud.fullPoints.isEmpty());
    }
    void uniqueAndNormalSign() {
        const auto points=patch();const auto extraShared=points;Q_UNUSED(extraShared);
        const auto index=preparePlaneSelectionIndex(points);QVERIFY2(index.index,qPrintable(index.error));
        const auto result=matchReferencePlaneV2(points,index.index,{});
        QCOMPARE(result.status,ReferencePlaneMatchStatusV2::Unique);QCOMPARE(result.candidates.size(),1);
        QCOMPARE(result.candidates.at(0).selection.planeIndices.size(),points.size());
        PlaneModel opposite;opposite.c=-1;
        const auto flipped=matchReferencePlaneV2(points,index.index,opposite);
        QCOMPARE(flipped.status,result.status);QCOMPARE(flipped.candidates.at(0).seedIndex,result.candidates.at(0).seedIndex);
        QCOMPARE(flipped.candidates.at(0).selection.planeIndices,result.candidates.at(0).selection.planeIndices);
        for(int tasks:{1,2,4}) {ReferencePlaneMatchOptionsV2 options;options.selection.parallelEnabled=true;options.selection.maxParallelTasks=tasks;
            const auto parallel=matchReferencePlaneV2(points,index.index,{},options);QCOMPARE(parallel.status,result.status);
            QCOMPARE(parallel.candidates.at(0).selection.planeIndices,result.candidates.at(0).selection.planeIndices);}
    }
    void separatedCoplanarRegionsAreAmbiguous() {
        auto points=patch();points.append(patch(100,0,0.25f));const auto immutable=points;
        const auto index=preparePlaneSelectionIndex(immutable);QVERIFY(index.index);
        const auto result=matchReferencePlaneV2(immutable,index.index,{});
        QCOMPARE(result.status,ReferencePlaneMatchStatusV2::Ambiguous);QVERIFY(result.candidates.size()>=2);
    }
    void thresholdsAndInvalidIndex() {
        const auto far=patch(0,20);const auto index=preparePlaneSelectionIndex(far);QVERIFY(index.index);
        QCOMPARE(matchReferencePlaneV2(far,index.index,{}).status,ReferencePlaneMatchStatusV2::NotFound);
        PlaneModel tilted;tilted.b=1;tilted.c=0;
        QCOMPARE(matchReferencePlaneV2(far,index.index,tilted).status,ReferencePlaneMatchStatusV2::NotFound);
        auto detached=far;detached.detach();
        QCOMPARE(matchReferencePlaneV2(detached,index.index,{}).status,ReferencePlaneMatchStatusV2::InvalidInput);
        ReferencePlaneMatchOptionsV2 options;options.maximumSeeds=0;
        QCOMPARE(matchReferencePlaneV2(far,index.index,{},options).status,ReferencePlaneMatchStatusV2::InvalidInput);
        options={};options.selection.isCancelled=[] {return true;};
        const auto cancelled=matchReferencePlaneV2(far,index.index,{},options);
        QCOMPARE(cancelled.status,ReferencePlaneMatchStatusV2::Cancelled);QVERIFY(cancelled.candidates.isEmpty());
    }
    void fullMappingAndSampling() {
        pcv::ui::SelectedPlaneTaskV2 task;
        for(int i=0;i<300013;++i)task.loaded.fullPoints.append({float(i),0,0});
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&task,{}));QCOMPARE(task.displayPoints.size(),300000);
        QCOMPARE(task.displayToFullIndices.first(),0);QCOMPARE(task.displayToFullIndices.last(),300012);
        QVERIFY(std::is_sorted(task.displayToFullIndices.cbegin(),task.displayToFullIndices.cend()));
        const auto token=std::make_shared<std::atomic_bool>(false);
        const auto budget=std::make_shared<pcv::ProcessingBudget>(256u*1024u*1024u);
        const auto invalid=pcv::ui::fitFullPlaneV2(task.loaded,{0},0.4f,3,true,{},token,1,budget);
        QVERIFY(!invalid.ok);QVERIFY(!invalid.error.isEmpty());
        const auto before=pcv::ui::loadedCloudDigestV2(task.loaded,token);
        task.loaded.fullPoints[1].x+=1;
        QVERIFY(before!=pcv::ui::loadedCloudDigestV2(task.loaded,token));
    }
    void configurableMillionPointDisplayLimit() {
        pcv::ui::SelectedPlaneTaskV2 task;
        constexpr int count=1000013;
        task.loaded.fullPoints.reserve(count);
        for(int i=0;i<1000000;++i)task.loaded.fullPoints.append({float(i),float(i%17),30});
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&task,{},1000000));
        QCOMPARE(task.displayPoints.size(),1000000);
        QCOMPARE(task.displayToFullIndices.size(),1000000);
        for(int i=1000000;i<count;++i)task.loaded.fullPoints.append({float(i),float(i%17),30});
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&task,{},1000000));
        QCOMPARE(task.displayPoints.size(),1000000);
        QCOMPARE(task.displayToFullIndices.first(),0);
        QCOMPARE(task.displayToFullIndices.last(),count-1);
        QVERIFY(std::is_sorted(task.displayToFullIndices.cbegin(),task.displayToFullIndices.cend()));
        for(int i=0;i<task.displayPoints.size();++i)
            QCOMPARE(task.displayPoints.at(i).x,float(task.displayToFullIndices.at(i)));
        const auto firstMapping=task.displayToFullIndices;
        pcv::ui::SelectedPlaneTaskV2 repeat;repeat.loaded.fullPoints=task.loaded.fullPoints;
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&repeat,{},1000000));
        QCOMPARE(repeat.displayToFullIndices,firstMapping);
        pcv::ui::SelectedPlaneTaskV2 defaultLimit;defaultLimit.loaded.fullPoints=task.loaded.fullPoints;
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&defaultLimit,{}));
        QCOMPARE(defaultLimit.displayPoints.size(),300000);
        QVERIFY(!pcv::ui::prepareDisplayCloudV2(&defaultLimit,{},0));
        pcv::ui::SelectedPlaneTaskV2 underLimit;
        underLimit.loaded.fullPoints.resize(17);
        QVERIFY(pcv::ui::prepareDisplayCloudV2(&underLimit,{},1000000));
        QCOMPARE(underLimit.displayPoints.size(),17);
    }
};
QTEST_GUILESS_MAIN(ReferencePlaneMatchTestsV2)
#include "reference_plane_match_tests.moc"
