#include "scan_geometry_fixture_v2.h"
#include <pcv/io/ply_reader.h>
#include <QTemporaryDir>
#include <QDataStream>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <pcv/workpiece/temp_workpiece_interface.h>
#include "scan_geometry_output_v1.h"
#include <QtTest>
#include <limits>
namespace pcv::demo {
struct ScanGeometrySessionTestAccessV2 {
    static void result(ScanGeometrySessionV2 &s) {
        s.m_hasResult=true;s.m_active=-1;s.m_result.success=true;s.m_result.points={{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
        s.m_result.cloudIds={0,0,1,1};s.m_result.sourceIndices={10,20,30,40};s.m_result.scanRatios={0,.2f,.5f,1};
        s.m_frames.resize(2);s.m_config.frames.resize(2);s.m_reference=0;
        auto &f=s.m_frames[0];f.confirmed=true;f.candidate.cloud.fullPoints=s.m_result.points;f.candidate.normal={0,0,1};f.candidate.center={.5f,.5f,0};
    }
    static void reviewFixture(ScanGeometrySessionV2 &s) {
        s.m_frames.resize(3);s.m_reference=0;s.m_active=0;
        for(auto &f:s.m_frames){f.hasCandidate=true;f.reviewed=true;f.confirmed=true;}
    }
    static SessionFrameV2 &frame(ScanGeometrySessionV2 &s,int i){return s.m_frames[i];}
    static void maskFrame(ScanGeometrySessionV2 &s) {
        s.m_frames.resize(1);s.m_active=0;
        auto &f=s.m_frames[0];
        f.workpieceMask.vertices={{0,0,0}};f.workpieceMask.triangleIndices={0,0,0};
        f.interferenceMask.vertices={{0,0,0}};f.interferenceMask.triangleIndices={0,0,0};
        f.pendingInterferenceMask.vertices={{0,0,0}};f.pendingInterferenceMask.triangleIndices={0,0,0};
        f.workpieceMaskError=QStringLiteral("workpiece");
        f.interferenceMaskError=QStringLiteral("interference");
        f.pendingInterferenceMaskError=QStringLiteral("pending");
        f.hasCandidate=true;
    }
    static void staleResult(ScanGeometrySessionV2 &s) {
        pcv::ui::SelectedPlaneTaskV2 task;task.ok=true;task.stitched.success=true;task.stitched.points={{5,5,5}};
        s.finish(-1,pcv::ui::SelectedPlaneTaskV2::Stitch,s.m_revision-1,s.m_referenceRevision,s.m_token,task);
    }
    static void pause(ScanGeometrySessionV2 &s,int frame) {s.m_matching=true;s.pauseAt(frame,QStringLiteral("测试异常"));}
    static bool sixFrameDisplayBudget(ScanGeometrySessionV2 &s) {
        bool filled=false;
        {
            QVector<std::shared_ptr<pcv::ProcessingBudget::Lease>> leases;
            for(int i=0;i<6;++i) {
                auto lease=s.m_budget->acquire(1000000u*64u);
                if(!lease)return false;
                leases.append(std::move(lease));
            }
            auto remaining=s.m_budget->acquire(512u*1024u*1024u-6u*1000000u*64u);
            if(!remaining||s.m_budget->acquire(1))return false;
            filled=s.m_budget->used()==512u*1024u*1024u;
        }
        return filled&&s.m_budget->used()==0;
    }
};
}
using namespace pcv::demo;
using AccessV2=ScanGeometrySessionTestAccessV2;
class ScanGeometrySessionTestsV2 : public QObject {
    Q_OBJECT
private slots:
    void sixFrameDisplayBudget() {
        ScanGeometrySessionV2 s;
        QVERIFY(AccessV2::sixFrameDisplayBudget(s));
    }
    void inputCounts_data() {QTest::addColumn<int>("count");for(int n:{2,6,7,13})QTest::newRow(qPrintable(QString::number(n)))<<n;}
    void inputCounts() {
        QFETCH(int,count);QTemporaryDir root;QVERIFY(root.isValid());InputConfigV2 config;
        QVERIFY(writeScanGeometryFixtureV2(root.path(),count,&config));ScanGeometrySessionV2 s;QString error;
        QVERIFY2(s.configure(config,&error),qPrintable(error));s.loadRound(0);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        for(int i=0;i<qMin(count,6);++i){QCOMPARE(s.frames().at(i).cloud.fullPoints.size(),625);QVERIFY(s.frames().at(i).index);}
        QVERIFY(!s.canStitch());
        if(count>6){s.loadRound((count-1)/6);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
            QVERIFY(s.frames().at(0).cloud.fullPoints.isEmpty());QCOMPARE(s.frames().last().cloud.fullPoints.size(),625);}
    }
    void realReadReferenceMatchReviewStitchAndMap() {
        QTemporaryDir root;QVERIFY(root.isValid());InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        config.poseOnly=true;ScanGeometrySessionV2 s;QString error;QVERIFY(s.configure(config,&error));
        s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);s.pick(312,false);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        QVERIFY(s.activeFrame()->hasCandidate);QVERIFY(s.confirmCandidate());QCOMPARE(s.reference(),0);
        QVERIFY(!s.activeFrame()->workpieceMask.triangleIndices.isEmpty());
        s.matchRemaining();QTRY_VERIFY_WITH_TIMEOUT(!s.busy()&&!s.matching(),60000);
        QVERIFY(s.frames().at(1).hasCandidate);QVERIFY(!s.canStitch());QVERIFY(!s.canReviewAll());
        s.activate(1);QVERIFY(s.confirmCandidate());QVERIFY(s.canReviewAll());QVERIFY(s.canStitch());
        s.stitch();QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY2(s.hasResult(),qPrintable(s.status()));
        QCOMPARE(s.result().points.size(),1250);QVERIFY(s.result().report.value("formal_output").toBool());
        QCOMPARE(s.result().report.value("quality_policy").toString(),QString("warn_only"));
        const auto ply=pcv::detail::io::readPly(s.result().outputPly);QVERIFY(ply.ok);QCOMPARE(ply.points.size(),1250);
        QFile map(s.result().mappingFile);QVERIFY(map.open(QIODevice::ReadOnly));QDataStream stream(&map);
        stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        quint32 magic,version;quint64 count;stream>>magic>>version>>count;QCOMPARE(magic,quint32(0x314D4350));QCOMPARE(count,quint64(1250));
        for(int i=0;i<1250;++i){qint32 cloud;qint64 index;float ratio;stream>>cloud>>index>>ratio;
            QCOMPARE(cloud,s.result().cloudIds.at(i));QCOMPARE(index,qint64(s.result().sourceIndices.at(i)));QCOMPARE(ratio,s.result().scanRatios.at(i));}
        QFile disk(s.result().outputPly);QVERIFY(disk.open(QIODevice::ReadOnly));const auto before=QCryptographicHash::hash(disk.readAll(),QCryptographicHash::Sha256);
        s.confirmGeometry();QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY2(s.geometryConfirmed(),qPrintable(s.status()));
        QVERIFY(QFileInfo::exists(s.savedGeometryPath()));QVERIFY(s.wobj1().valid);
        QCOMPARE(s.wobj1().originInRobotBase,QVector3D(6,6,30));
        s.erase({0,624,625},s.resultRevision());QVERIFY(!s.geometryConfirmed());QVERIFY(!s.wobj1().valid);QCOMPARE(s.result().points.size(),1250);
        disk.seek(0);QCOMPARE(QCryptographicHash::hash(disk.readAll(),QCryptographicHash::Sha256),before);
    }
    void reviewGatesAndReferenceInvalidation() {
        ScanGeometrySessionV2 s;AccessV2::reviewFixture(s);QVERIFY(s.canReviewAll());QVERIFY(s.canStitch());
        AccessV2::frame(s,1).problem=true;QVERIFY(!s.canStitch());
        AccessV2::frame(s,1).problem=false;AccessV2::frame(s,1).automatic=true;AccessV2::frame(s,2).automatic=true;
        s.clearPlane();QVERIFY(!s.frames().at(1).hasCandidate);QVERIFY(!s.frames().at(2).hasCandidate);QCOMPARE(s.reference(),-1);
        QVERIFY(!s.canStitch());
    }
    void highSideReminderIsSoftAndIgnoreIsPerCandidate() {
        ScanGeometrySessionV2 s;AccessV2::reviewFixture(s);
        AccessV2::frame(s,0).highSideCandidates.append({101,250,6.0f});
        AccessV2::frame(s,0).highSideCandidates.append({202,320,8.0f});
        QVERIFY(s.canStitch());
        QVERIFY(s.ignoreHighSideCandidate(101));
        QVERIFY(s.canStitch());
        QCOMPARE(s.frames().at(0).highSideCandidates.size(),1);
        QVERIFY(s.frames().at(0).dismissedHighSideSeeds.contains(101));
        QCOMPARE(s.frames().at(0).highSideCandidates.first().seedIndex,202);
    }
    void clearingPlaneAndInterferenceClearsTheirMaskState() {
        ScanGeometrySessionV2 s;AccessV2::maskFrame(s);
        s.clearPlane();
        QVERIFY(s.frames().at(0).workpieceMask.triangleIndices.isEmpty());
        QVERIFY(s.frames().at(0).workpieceMaskError.isEmpty());
        AccessV2::frame(s,0).interferenceIndices={0};
        s.clearInterference();
        QVERIFY(s.frames().at(0).interferenceMask.triangleIndices.isEmpty());
        QVERIFY(s.frames().at(0).pendingInterferenceMask.triangleIndices.isEmpty());
        QVERIFY(s.frames().at(0).interferenceMaskError.isEmpty());
        QVERIFY(s.frames().at(0).pendingInterferenceMaskError.isEmpty());
    }
    void trimmingRestoreUndoAndGeometry() {
        ScanGeometrySessionV2 s;AccessV2::result(s);QVERIFY(s.canConfirmGeometry());QVERIFY(s.generateWobj1());QVERIFY(s.wobj1().valid);
        s.erase({0,0,-1,100},s.resultRevision());QCOMPARE(s.removed().size(),1);QVERIFY(!s.geometryConfirmed());s.erase({1},s.resultRevision());QVERIFY(!s.canConfirmGeometry());
        s.restore();QVERIFY(s.removed().isEmpty());s.undo();QCOMPARE(s.removed().size(),2);s.undo();QCOMPARE(s.removed().size(),1);
        s.erase({0,1,2,3},s.resultRevision());QVERIFY(!s.canConfirmGeometry());s.undo();QVERIFY(s.canConfirmGeometry());
        QCOMPARE(s.result().points.size(),4);
        const auto before=s.removed();s.erase({2},s.resultRevision()+1);QCOMPARE(s.removed(),before);
    }
    void unresolvedFailureManualReviewAndExplicitSkip() {
        ScanGeometrySessionV2 s;AccessV2::reviewFixture(s);QVERIFY(s.canStitch());
        AccessV2::pause(s,1);QVERIFY(s.paused());QVERIFY(!s.canReviewAll());
        QVERIFY(s.confirmCandidate());QVERIFY(!s.canReviewAll());s.resumeMatching();QVERIFY(!s.matching());
        // Skipping is an explicit per-frame decision; the remaining reviewed
        // frames can proceed directly, provided at least two remain.
        s.activate(1);s.skipActive();QVERIFY(s.frames().at(1).skipped);QVERIFY(s.canStitch());
        s.activate(2);s.skipActive();QVERIFY(!s.canReviewAll());QVERIFY(!s.canStitch());
    }
    void cancellationRetryAndLateResult() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        ScanGeometrySessionV2 s;QString error;QVERIFY(s.configure(config,&error));s.loadRound(0);s.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(!s.hasResult());AccessV2::staleResult(s);QVERIFY(!s.hasResult());
        s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(s.activeFrame()->index);
    }
    void roundReloadRetainsSnapshotAndDetectsSourceChange() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),7,&config));
        ScanGeometrySessionV2 s;QString error;QVERIFY(s.configure(config,&error));s.loadRound(0);
        QVERIFY(s.runningCount()<=2);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);s.pick(312,false);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(s.confirmCandidate());s.matchRemaining();
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy()&&!s.matching(),90000);QVERIFY(s.frames().at(6).hasCandidate);
        QVERIFY(s.frames().at(0).cloud.fullPoints.isEmpty());QVERIFY(s.frames().at(0).hasCandidate);
        s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QVERIFY(s.frames().at(0).hasCandidate);QCOMPARE(s.reference(),0);
        s.loadRound(1);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        QFile source(config.frames.at(0).path);QVERIFY(source.open(QIODevice::ReadOnly));auto bytes=source.readAll();source.close();bytes.replace(" 30\n"," 31\n");
        QVERIFY(source.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(source.write(bytes),qint64(bytes.size()));source.close();
        s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);QCOMPARE(s.reference(),-1);
        QVERIFY(!s.frames().at(0).hasCandidate);QVERIFY(!s.frames().at(6).hasCandidate);QVERIFY(!s.canStitch());
    }
    void shutdownJoinsOutstandingLoads() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),6,&config));
        ScanGeometrySessionV2 s;QString error;QVERIFY(s.configure(config,&error));s.loadRound(0);s.shutdown();
        QVERIFY(!s.busy());QVERIFY(!s.hasResult());
    }
    void invalidPoseAndXmlReadFailure() {
        QTemporaryDir root;InputConfigV2 config;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&config));
        ScanGeometrySessionV2 s;QString error;config.frames[0].start.x=std::numeric_limits<double>::quiet_NaN();QVERIFY(!s.configure(config,&error));
        config.frames[0].start.x=0;QFile xml(config.calibration);QVERIFY(xml.open(QIODevice::WriteOnly|QIODevice::Truncate));xml.write("invalid");xml.close();
        QVERIFY(s.configure(config,&error));s.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        QVERIFY(s.frames().at(0).problem);QVERIFY(!s.canStitch());
    }
    void wobjRobotAxesAndDegenerateInputs() {
        const auto flat=pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes({1,2,3},{0,0,-1});
        QVERIFY(flat.valid);QCOMPARE(flat.originInRobotBase,QVector3D(1,2,3));QCOMPARE(flat.axisXInRobotBase,QVector3D(1,0,0));
        QCOMPARE(flat.axisYInRobotBase,QVector3D(0,1,0));QCOMPARE(flat.poseA,0.f);QCOMPARE(flat.poseB,0.f);QCOMPARE(flat.poseC,0.f);
        const auto tilted=pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes({4,5,6},{0,-1,1});
        QVERIFY(tilted.valid);QVERIFY(qAbs(tilted.poseA-45.f)<.001f);QVERIFY(qAbs(tilted.poseB)<.001f);QVERIFY(qAbs(tilted.poseC)<.001f);
        QVERIFY((QVector3D::crossProduct(tilted.axisXInRobotBase,tilted.axisYInRobotBase)-tilted.axisZInRobotBase).length()<1e-6f);
        const auto roundTrip=pointcloud::robotPoseToMatrix(pointcloud::matrixToRobotPose(tilted.workpieceToRobotBase));
        for(int i=0;i<16;++i)QVERIFY(qAbs(roundTrip.constData()[i]-tilted.workpieceToRobotBase.constData()[i])<1e-5f);
        QVERIFY(!pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes({},{}).valid);
        QVERIFY(!pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes({},{1,0,0}).valid);
        QVERIFY(!pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes({std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,1}).valid);
    }
    void scanJsonUsesSameTransformAndMapping() {
        QTemporaryDir root;InputConfigV2 manual;QVERIFY(writeScanGeometryFixtureV2(root.path(),2,&manual));
        InputConfigV2 imported;imported.outputRoot=manual.outputRoot;
        for(int i=0;i<2;++i) {
            QJsonObject json{{"schema_version",QString::fromLatin1(pcv::interface::kTempScanningSchema)},
                {"kind",QString::fromLatin1(pcv::interface::kTempScanningKind)},{"created_at","2026-10-29T08:00:00+08:00"},
                {"scan",QJsonObject{{"scan_id",QString::number(i)},{"point_cloud_file",QFileInfo(manual.frames.at(i).path).fileName()},
                    {"point_cloud_layout","line_profile_xz"},{"coordinate_frame","camera"},
                    {"robot_pose_start",QJsonArray{0,0,0,0,0,0}},{"robot_pose_end",QJsonArray{0,12,0,0,0,0}}}},
                {"calibration",QJsonObject{{"calibration_file","handeye.xml"},{"source_frame","camera"},{"target_frame","robot_base"}}}};
            const auto path=QDir(root.path()).filePath(QString("scan_%1.json").arg(i));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(json).toJson());file.close();
            InputFrameV2 frame;QString error;QVERIFY2(pcv::ui::parseSelectedPlaneScanJsonV2(path,&frame,&error),qPrintable(error));imported.frames.append(frame);
            QCOMPARE(frame.path,manual.frames.at(i).path);QCOMPARE(frame.calibration,manual.calibration);QCOMPARE(frame.end.y,12.);
        }
        ScanGeometrySessionV2 a,b;QString error;QVERIFY(a.configure(manual,&error));QVERIFY2(b.configure(imported,&error),qPrintable(error));
        a.loadRound(0);b.loadRound(0);QTRY_VERIFY_WITH_TIMEOUT(!a.busy()&&!b.busy(),60000);
        const auto &ca=a.frames().at(0).cloud;const auto &cb=b.frames().at(0).cloud;
        QCOMPARE(ca.sourceIndices,cb.sourceIndices);QCOMPARE(ca.scanRatios,cb.scanRatios);QCOMPARE(ca.fullPoints.size(),cb.fullPoints.size());
        for(int i=0;i<ca.fullPoints.size();++i){QCOMPARE(ca.fullPoints.at(i).x,cb.fullPoints.at(i).x);QCOMPARE(ca.fullPoints.at(i).y,cb.fullPoints.at(i).y);QCOMPARE(ca.fullPoints.at(i).z,cb.fullPoints.at(i).z);}
    }
    void geometryTransactionTrimMappingInterferenceAndRepeat() {
        QTemporaryDir root;ScanGeometrySessionV2 s;AccessV2::result(s);QVERIFY(s.generateWobj1());
        GeometryOutputRequestV1 request;request.result=s.result();request.wobj1=s.wobj1();request.removed={1};request.outputRoot=root.path();
        request.referenceFrame=0;request.resultRevision=12;request.frames={{11,"source_a.ply","scan_a.json",{7,9}},{12,"source_b.ply",{}, {}}};
        const auto saved=saveSelectedPlaneGeometryV1(request);QVERIFY2(saved.success,qPrintable(saved.error));
        QCOMPARE(QDir(saved.outputDirectory).entryList(QDir::Files).size(),7);
        const auto trimmed=pcv::detail::io::readPly(saved.trimmedPly);QVERIFY(trimmed.ok);QCOMPARE(trimmed.points.size(),3);
        QFile map(saved.trimmedMapping);QVERIFY(map.open(QIODevice::ReadOnly));QDataStream stream(&map);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        quint32 magic,version;quint64 count;stream>>magic>>version>>count;QCOMPARE(magic,quint32(0x314D4350));QCOMPARE(version,quint32(1));QCOMPARE(count,quint64(3));
        int j=0;for(int i:{0,2,3}) {qint32 cloud;qint64 index;float ratio;stream>>cloud>>index>>ratio;QCOMPARE(cloud,request.result.cloudIds.at(i));QCOMPARE(index,qint64(request.result.sourceIndices.at(i)));QCOMPARE(ratio,request.result.scanRatios.at(i));QCOMPARE(trimmed.points.at(j++).x,request.result.points.at(i).x);}
        QFile sidecar(saved.sidecar);QVERIFY(sidecar.open(QIODevice::ReadOnly));const auto object=QJsonDocument::fromJson(sidecar.readAll()).object();
        QCOMPARE(object.value("schema_version").toString(),QString("selected_plane_geometry_v1"));QCOMPARE(object.value("coordinate_frame").toString(),QString("robot_base"));
        QCOMPARE(object.value("wobj1").toObject().value("reference_frame_id").toString(),QString("11"));
        QCOMPARE(object.value("trim").toObject().value("deleted_result_indices").toArray(),QJsonArray{1});
        const auto interference=object.value("interference").toArray();QCOMPARE(interference.at(0).toObject().value("interference_full_indices").toArray(),QJsonArray({7,9}));
        QCOMPARE(interference.at(1).toObject().value("interference_point_count").toInt(),0);
        const auto repeated=saveSelectedPlaneGeometryV1(request);QVERIFY(repeated.success);QVERIFY(repeated.sidecar!=saved.sidecar);QVERIFY(QFileInfo::exists(saved.sidecar));
        const auto before=QDir(root.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        request.isCancelled=[]{return true;};const auto cancelled=saveSelectedPlaneGeometryV1(request);QVERIFY(!cancelled.success);QVERIFY(cancelled.cancelled);
        QCOMPARE(QDir(root.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot),before);
        request.isCancelled={};request.result.scanRatios.clear();QVERIFY(!saveSelectedPlaneGeometryV1(request).success);
    }
};
QTEST_GUILESS_MAIN(ScanGeometrySessionTestsV2)
#include "scan_geometry_session_tests_v2.moc"
