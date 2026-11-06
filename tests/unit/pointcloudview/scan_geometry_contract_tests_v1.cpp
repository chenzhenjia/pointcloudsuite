#include "scan_geometry_fixture_v2.h"
#include "scan_geometry_output_v1.h"
#include <pcv/io/ply_reader.h>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <limits>

using namespace pcv::demo;
namespace {
bool fixture(const QString &root, GeometryOutputRequestV1 *r, bool vertical=false) {
    InputConfigV2 config;
    if (!writeScanGeometryFixtureV2(root,2,&config)) return false;
    r->contract={};auto &c=r->contract;
    c.runtimeRoot=QDir(root).filePath(QStringLiteral("运行 数据_v1"));c.jobId="JOB-TEST";c.runId="RUN-001";
    c.stepId="STEP-001";c.workpieceId="WP-001";c.controllerId="ROBOT-001";c.maskPixelSizeMm=0.25;
    r->result.success=true;r->result.report={{"coordinate_frame","robot_base"},{"formal_output",true},{"quality_policy","warn_only"}};
    r->wobj1=pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes(vertical?QVector3D(6,-30,6):QVector3D(6,6,30),vertical?QVector3D(0,-1,0):QVector3D(0,0,1));
    r->outputRoot=config.outputRoot;r->referenceFrame=0;r->resultRevision=7;
    const auto token=std::make_shared<std::atomic_bool>(false);
    const auto budget=std::make_shared<pcv::ProcessingBudget>(512u*1024u*1024u);
    for(int i=0;i<config.frames.size();++i) {
        auto input=config.frames.at(i);input.calibration=config.calibration;
        if(vertical){input.start.rx=input.end.rx=90;input.end.y=0;input.end.z=12;}
        const auto loaded=pcv::ui::loadSelectedPlaneFrameV2(input.path,input.calibration,input.start,input.end,token,1,budget);
        if(!loaded.ok)return false;
        GeometryContractFrameV1 frame;frame.input=input;frame.selected.cloud=loaded.loaded;
        frame.selected.normal=vertical?QVector3D(0,-1,0):QVector3D(0,0,1);frame.selected.center=r->wobj1.originInRobotBase;
        frame.sourceDigest=pcv::ui::loadedCloudDigestV2(loaded.loaded,token);
        r->contractFrames.append(frame);r->frames.append({quint64(i+1),input.path,{}, {}});
        r->result.corrections.append(QMatrix4x4());
        for(int j=0;j<loaded.loaded.fullPoints.size();++j) {
            r->result.points.append(loaded.loaded.fullPoints.at(j));r->result.cloudIds.append(i);
            r->result.sourceIndices.append(loaded.loaded.sourceIndices.at(j));r->result.scanRatios.append(loaded.loaded.scanRatios.at(j));
        }
    }
    return true;
}
QJsonObject read(const QString &path) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();
}
QJsonArray pose(const pointcloud::RobotPose &p){return {p.x,p.y,p.z,p.rx,p.ry,p.rz};}
bool scan(GeometryOutputRequestV1 &r) {
    const auto prefix=QStringLiteral("jobs/%1/runs/%2/").arg(r.contract.jobId,r.contract.runId);
    const auto plan=prefix+"07-plan-compiler/process_plan_global_scan.json";
    r.contract.scanPathOutputRef=prefix+"02-robot-api/scan_path_output.json";
    QJsonArray points;for(const auto &f:r.contractFrames){points.append(pose(f.input.start));points.append(pose(f.input.end));}
    const QJsonObject p{{"schema","sr2026.process-plan"},{"schema_version",1},{"job_id",r.contract.jobId},
        {"run_id",r.contract.runId},{"step_id",r.contract.stepId},{"workpiece_id",r.contract.workpieceId},
        {"process",QJsonObject{{"business_code","global_scan"}}}};
    const QJsonObject s{{"schema","sr2026.scan-path-output"},{"schema_version",1},
        {"job_id",r.contract.jobId},{"run_id",r.contract.runId},{"step_id",r.contract.stepId},{"business_code","global_scan"},
        {"source_plan_ref",plan},{"execution_status","completed"},{"coordinate_frame","base"},
        {"path_count",r.contractFrames.size()},{"points_per_path",2},{"points",points},{"generated_at","2026-11-05T10:00:00+08:00"}};
    pcv::io::ArtifactTransaction tx(r.contract.runtimeRoot,false);
    return tx.stageJson(plan,QJsonDocument(p).toJson())&&tx.stageJson(r.contract.scanPathOutputRef,QJsonDocument(s).toJson())&&tx.commit();
}
}
class ScanGeometryContractTestsV1:public QObject {
    Q_OBJECT
private slots:
    void partialAssetsAndLegacySidecar() {
        const auto evidence=qEnvironmentVariable("PCV_SCAN_CONTRACT_EVIDENCE");
        QTemporaryDir root(evidence.isEmpty()?QDir::tempPath()+"/pcv-contract-XXXXXX":QDir(evidence).filePath("partial-XXXXXX"));
        if(!evidence.isEmpty())root.setAutoRemove(false);
        GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r));
        r.removed={0,1,624};
        const auto saved=saveSelectedPlaneGeometryV1(r);QVERIFY2(saved.success,qPrintable(saved.error));
        const auto o=read(saved.contractFile);QString error;QVERIFY2(validatePointCloudResultV1(o,&error),qPrintable(error));
        QCOMPARE(o.value("status").toString(),QString("partial"));QVERIFY(o.value("template_ref").isNull());
        const auto input=o.value("inputs").toObject();QVERIFY(!input.contains("scan_path_output_ref"));
        const auto output=o.value("outputs").toObject();QVERIFY(!output.contains("probe_targets"));
        const auto resolve=[&](const QString &ref){return QDir(r.contract.runtimeRoot).filePath(ref);};
        auto base=pcv::detail::io::readPly(resolve(output.value("base_cloud_ref").toString()));QVERIFY(base.ok);QCOMPARE(base.points.size(),1250);
        const auto geo=output.value("template_geometry").toObject();
        auto trimmed=pcv::detail::io::readPly(resolve(geo.value("point_cloud_ref").toString()));QVERIFY(trimmed.ok);QCOMPARE(trimmed.points.size(),1247);
        auto mask=geo.value("plane_mask").toObject();QImage image(resolve(mask.value("ref").toString()));QVERIFY(!image.isNull());
        QCOMPARE(image.format(),QImage::Format_Grayscale8);
        bool has128=false;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            const auto v=image.constScanLine(y)[x];QVERIFY(v==0||v==128||v==255);has128|=v==128;
        }QVERIFY(has128);
        QCOMPARE(read(saved.sidecar).value("schema_version").toString(),QString("selected_plane_geometry_v1"));
        QVERIFY(QFileInfo(QDir(saved.outputDirectory).filePath("selected_plane_robot_base.pcvmap")).isFile());
        QVERIFY(!QFileInfo(QDir(r.contract.runtimeRoot).filePath("workpieces/WP-001/current.json")).exists());
        const auto second=saveSelectedPlaneGeometryV1(r);QVERIFY(!second.success);QCOMPARE(read(saved.contractFile),o);
    }
    void completedRequiresActualScanAndEndpoints() {
        QTemporaryDir root;GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r));QVERIFY(scan(r));
        const auto saved=saveSelectedPlaneGeometryV1(r);QVERIFY2(saved.success,qPrintable(saved.error));
        QCOMPARE(read(saved.contractFile).value("status").toString(),QString("completed"));
        r.contract.runId="RUN-002";const auto invalid=saveSelectedPlaneGeometryV1(r);QVERIFY(!invalid.success);
        QVERIFY(!QFileInfo(QDir(pointCloudContractDirectoryV1(r.contract)).filePath("point_cloud_result.json")).exists());
    }
    void verticalPlaneAndMaskOrientation() {
        QTemporaryDir root;GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r,true));
        const auto saved=saveSelectedPlaneGeometryV1(r);QVERIFY2(saved.success,qPrintable(saved.error));
        const auto o=read(saved.contractFile);QString error;QVERIFY(validatePointCloudResultV1(o,&error));
        QCOMPARE(o.value("outputs").toObject().value("template_geometry").toObject().value("coordinate_frame").toString(),QString("base"));
        QImage image(QDir(saved.outputDirectory).filePath("plane_mask.png"));QVERIFY(!image.isNull());
        QVERIFY(image.width()>1 && image.height()>1);
        const auto points=pcv::detail::io::readPly(QDir(saved.outputDirectory).filePath("template_candidate.ply"));QVERIFY(points.ok);
        for(const auto &p:points.points)QVERIFY(std::abs(p.y+30)<0.001f);
    }
    void endpointMismatchDoesNotPublish() {
        QTemporaryDir root;GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r));QVERIFY(scan(r));
        r.contractFrames[0].input.end.x+=1;
        const auto result=saveSelectedPlaneGeometryV1(r);QVERIFY(!result.success);QVERIFY(result.error.contains("endpoints"));
        QVERIFY(!QFileInfo(QDir(pointCloudContractDirectoryV1(r.contract)).filePath("stitched_base.ply")).exists());
    }
    void cancellationAndChangedSource() {
        QTemporaryDir root;GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r));
        int checks=0;r.isCancelled=[&]{return ++checks>16;};
        const auto stopped=saveSelectedPlaneGeometryV1(r);QVERIFY(!stopped.success);QVERIFY(stopped.cancelled);
        QVERIFY(!QFileInfo(QDir(pointCloudContractDirectoryV1(r.contract)).filePath("point_cloud_result.json")).exists());
        r.isCancelled={};r.contractFrames[0].sourceDigest="changed";
        const auto changed=saveSelectedPlaneGeometryV1(r);QVERIFY(!changed.success);QVERIFY(changed.error.contains("changed"));
    }
    void maskForbiddenAndFrameValidation() {
        QTemporaryDir root;GeometryOutputRequestV1 r;QVERIFY(fixture(root.path(),&r));
        auto &mesh=r.contractFrames[0].interferenceMask;
        mesh.vertices={{4,4,31},{8,4,31},{8,8,31},{4,8,31}};mesh.triangleIndices={0,1,2,0,2,3};
        const auto saved=saveSelectedPlaneGeometryV1(r);QVERIFY2(saved.success,qPrintable(saved.error));
        QImage image(QDir(saved.outputDirectory).filePath("plane_mask.png"));bool has255=false;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)has255|=image.constScanLine(y)[x]==255;
        QVERIFY(has255);
        auto o=read(saved.contractFile);auto out=o.value("outputs").toObject();auto geo=out.value("template_geometry").toObject();
        geo["coordinate_frame"]="workpiece";out["template_geometry"]=geo;o["outputs"]=out;QString error;
        QVERIFY(!validatePointCloudResultV1(o,&error));
        auto c=r.contract;c.jobId="../escape";QVERIFY(!validateScanGeometryContractV1(c,&error));
        c=r.contract;c.scanPathOutputRef="D:/scan.json";QVERIFY(!validateScanGeometryContractV1(c,&error));
        c=r.contract;c.maskPixelSizeMm=std::numeric_limits<double>::infinity();QVERIFY(!validateScanGeometryContractV1(c,&error));
    }
};
QTEST_GUILESS_MAIN(ScanGeometryContractTestsV1)
#include "scan_geometry_contract_tests_v1.moc"
