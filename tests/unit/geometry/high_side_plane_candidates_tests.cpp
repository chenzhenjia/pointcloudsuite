#include <pcv/planefitting/high_side_plane_candidates_v1.h>
#include <QtTest>

using namespace pcv::planefitting;

class HighSidePlaneCandidatesTestsV1 final : public QObject {
    Q_OBJECT
    static QVector<pointcloud::Point3D> twoLayers(float rise=5.0f) {
        QVector<pointcloud::Point3D> points;
        for (int layer=0;layer<2;++layer)
            for (int y=0;y<40;++y)
                for (int x=0;x<40;++x)
                    points.append({x*0.4f,y*0.4f,layer?rise:0.0f});
        return points;
    }
    static QVector<pointcloud::Point3D> tiltedLayers() {
        QVector<pointcloud::Point3D> points;
        const float verticalRise=5.0f/0.89442719f;
        for (int layer=0;layer<2;++layer)
            for (int y=0;y<40;++y)
                for (int x=0;x<40;++x) {
                    const float px=x*0.4f;
                    points.append({px,y*0.4f,px*0.5f+(layer?verticalRise:0.0f)});
                }
        return points;
    }
private slots:
    void findsUpperLayerByWorldZ() {
        const auto points=twoLayers();
        const auto prepared=preparePlaneSelectionIndex(points);
        QVERIFY2(prepared.index,qPrintable(prepared.error));
        HighSidePlaneSearchOptionsV1 options;
        const auto found=findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0},{},options);
        QCOMPARE(found.status,HighSidePlaneSearchStatusV1::Success);
        QCOMPARE(found.candidates.size(),1);
        QVERIFY(found.attemptedSeeds>1);
        QCOMPARE(found.candidates.first().pointCount,1600);
        QVERIFY(qAbs(found.candidates.first().heightMm-5.0f)<0.1f);
        const auto shiftedBase=findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,1.0f},{},options);
        QCOMPARE(shiftedBase.status,HighSidePlaneSearchStatusV1::Success);
        QCOMPARE(shiftedBase.candidates.size(),1);
        QVERIFY(qAbs(shiftedBase.candidates.first().heightMm-4.0f)<0.1f);
    }
    void appliesHeightPointCountAndExclusionThresholds() {
        const auto points=twoLayers();
        const auto prepared=preparePlaneSelectionIndex(points);
        QVERIFY(prepared.index);
        HighSidePlaneSearchOptionsV1 options;options.minimumHeightMm=6.0f;
        QVERIFY(findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0},{},options).candidates.isEmpty());
        options.minimumHeightMm=2.0f;options.minimumPointCount=2000;
        QVERIFY(findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0},{},options).candidates.isEmpty());
        options.minimumPointCount=100;
        QVector<int> excluded;for(int i=1600;i<points.size();++i)excluded.append(i);
        QVERIFY(findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0},excluded,options).candidates.isEmpty());
    }
    void usesWorldZForTiltedWorkpiece() {
        const auto points=tiltedLayers();
        const auto prepared=preparePlaneSelectionIndex(points);
        QVERIFY2(prepared.index,qPrintable(prepared.error));
        const QVector3D workpieceCenter(7.8f,7.8f,3.9f);
        const auto found=findHighSidePlaneCandidatesV1(points,prepared.index,workpieceCenter,{});
        QCOMPARE(found.status,HighSidePlaneSearchStatusV1::Success);
        QCOMPARE(found.candidates.size(),1);
        const float expected=5.0f/0.89442719f;
        QVERIFY(qAbs(found.candidates.first().heightMm-expected)<0.2f);
        HighSidePlaneSearchOptionsV1 options;options.minimumHeightMm=expected+0.1f;
        QVERIFY(findHighSidePlaneCandidatesV1(points,prepared.index,workpieceCenter,{},options).candidates.isEmpty());
    }
    void horizontalPlaneAndCancellationAreSupported() {
        const auto points=twoLayers();
        const auto prepared=preparePlaneSelectionIndex(points);
        QVERIFY(prepared.index);
        QCOMPARE(findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0},{}).status,
                 HighSidePlaneSearchStatusV1::Success);
        HighSidePlaneSearchOptionsV1 options;options.selection.isCancelled=[] {return true;};
        QCOMPARE(findHighSidePlaneCandidatesV1(points,prepared.index,{0,0,0}, {},options).status,
                 HighSidePlaneSearchStatusV1::Cancelled);
    }
};

QTEST_GUILESS_MAIN(HighSidePlaneCandidatesTestsV1)
#include "high_side_plane_candidates_tests.moc"
