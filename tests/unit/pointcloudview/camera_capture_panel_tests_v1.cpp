#include "features/camera_capture_panel_v1.h"
#include "cameradialog.h"
#include <pcv/camera/mock_frame_source.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonDocument>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)
namespace {
bool waitUntil(const std::function<bool()> &done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 10000) {
        QApplication::processEvents(); QThread::msleep(1);
    }
    QApplication::processEvents();
    return done();
}
void bind(CameraCapturePanelV1 &panel, pcv::camera::MockFrameSource &source) {
    panel.setConnectionState(true, QStringLiteral("Mock ready"));
    panel.startAcquisition = [&source](auto queue, const auto &configuration, QString *error) {
        const auto configured = source.configureCapture(configuration);
        if (!configured.ok()) { *error = configured.message; return false; }
        source.frameSlot().setCaptureQueue(queue); return source.start().ok();
    };
    panel.stopAcquisition = [&source] { source.stop(); source.frameSlot().setCaptureQueue({}); };
}
}
struct CameraCapturePanelTestAccess {
    static int run() {
        QTemporaryDir temp(QDir::tempPath() + QString::fromUtf8("/相机向导-XXXXXX"));
        CHECK(temp.isValid());
        pcv::camera::MockFrameSource source(8, 4);
        CameraCapturePanelV1 panel;
        CHECK(panel.m_steps->count() == 4 && panel.snapshot().step == 0);
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Disconnected);
        CHECK(!panel.m_next->isEnabled());
        panel.advanceStep(); CHECK(panel.snapshot().step == 0);
        bind(panel, source);
        panel.m_next->click(); CHECK(panel.snapshot().step == 1);
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Configuring);
        panel.m_directory->setText(temp.path());
        CHECK(!panel.m_next->isEnabled() && !panel.m_start->isEnabled());
        panel.startCapture(); CHECK(!panel.isBusy() && panel.m_session.isEmpty());
        panel.m_externalLight->setChecked(true);
        panel.startCapture(); CHECK(!panel.isBusy() && panel.m_session.isEmpty());
        panel.m_externalLightConfirmed->setChecked(true);
        panel.m_preflight->setChecked(true);
        CHECK(panel.m_sdkVerified->isChecked() && panel.m_gateInactive->isChecked());
        CHECK(panel.m_next->isEnabled());
        panel.m_next->click(); CHECK(panel.snapshot().step == 2);
        panel.m_start->click(); CHECK(panel.isCapturing());
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Capturing);
        CHECK(!panel.m_back->isEnabled() && !panel.m_directory->isEnabled() && !panel.m_preflight->isChecked());
        source.emitFrame(); panel.pollCapture(); panel.m_stop->click();
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Draining);
        CHECK(!panel.m_next->isEnabled() && !panel.m_stop->isEnabled());
        CHECK(waitUntil([&] { return !panel.isBusy(); }));
        CHECK(panel.m_frames.size() == 2 && panel.snapshot().step == 3);
        CHECK(panel.m_frames.at(0).intensityAvailable
              && QFile::exists(QDir(panel.m_frames.at(0).directory).filePath("intensity.png"))
              && QFile::exists(QDir(panel.m_frames.at(0).directory).filePath("intensity.mono8")));
        const auto savedTexture = pcv::camera::readCapturedFrame(panel.m_frames.at(0).directory);
        CHECK(savedTexture.ok && savedTexture.intensityWidth == 8 && savedTexture.intensityHeight == 4
              && savedTexture.intensityMono8.size() == 32);
        QString sessionError;
        const auto sessionRecords = pcv::camera::readCaptureSession(panel.m_session, &sessionError);
        CHECK(sessionError.isEmpty() && sessionRecords.size() == 2
              && sessionRecords.at(0).intensityAvailable);
        QFile sessionFile(QDir(panel.m_session).filePath("session.json"));
        CHECK(sessionFile.open(QIODevice::ReadOnly));
        const auto sessionMeta = QJsonDocument::fromJson(sessionFile.readAll()).object();
        CHECK(sessionMeta.value("intensity_enabled").toBool()
              && sessionMeta.value("external_light_enabled").toBool()
              && sessionMeta.value("external_light_operator_confirmed").toBool());
        CHECK(panel.m_session.contains("capture_v2_") && panel.m_table->item(0, 8)->text().size());
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Reviewing);
        CHECK(panel.m_table->item(0, 7)->text() == QString::fromUtf8("未填写"));
        CHECK(!panel.m_stitch->isEnabled());
        const auto xml = temp.filePath("handeye.xml"); QFile file(xml);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>"); file.close();
        panel.m_xml->setText(xml);
        panel.m_table->item(0, 5)->setText("0 0 0 0 0 0");
        panel.m_table->item(0, 6)->setText("0 -100 0 0 0 0");
        panel.m_table->setCurrentCell(1, 5); panel.m_copyPrevious->click();
        CHECK(panel.m_table->item(1, 6)->text() == panel.m_table->item(0, 6)->text());
        CHECK(panel.m_stitch->isEnabled());
        panel.m_table->item(1, 5)->setText("bad");
        CHECK(!panel.m_stitch->isEnabled() && !panel.validatePoses(true));
        CHECK(panel.m_table->currentRow() == 1 && panel.m_table->item(1, 7)->text() == QString::fromUtf8("格式错误"));
        panel.m_table->setCurrentCell(0, 5);
        panel.m_table->selectAll(); panel.copyPoseRow(true);
        CHECK(panel.validatePoses(true));
        panel.m_table->item(1, 0)->setCheckState(Qt::Unchecked);
        CHECK(!panel.m_stitch->isEnabled() && panel.m_table->item(1, 7)->text() == QString::fromUtf8("已排除"));
        panel.m_table->item(1, 0)->setCheckState(Qt::Checked);
        const auto firstId = panel.m_frames.at(0).id;
        panel.m_table->setCurrentCell(0, 0); panel.moveRow(1);
        CHECK(panel.m_frames.at(1).id == firstId);
        const QString imagePath = QDir(panel.m_frames.at(0).directory).filePath("intensity.png");
        QFile imageFile(imagePath); CHECK(imageFile.open(QIODevice::ReadOnly));
        const QByteArray imageBytes = imageFile.readAll(); imageFile.close();
        CHECK(imageFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        imageFile.write("broken"); imageFile.close();
        panel.stitch();
        CHECK(!panel.isBusy() && panel.m_status->text().contains(QString::fromUtf8("亮度图校验失败")));
        CHECK(imageFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(imageFile.write(imageBytes) == imageBytes.size()); imageFile.close();
        int published = 0;
        panel.canStitch = [] { return true; };
        panel.publishResult = [&published](const auto &result) { if (result.success && result.points.size() == 64) ++published; };
        panel.m_stitch->click();
        CHECK(panel.snapshot().state == CameraCapturePanelV1::State::Publishing);
        CHECK(waitUntil([&] { return !panel.isBusy(); }));
        CHECK(published == 1 && panel.snapshot().state == CameraCapturePanelV1::State::Completed);
        const auto session = panel.m_session;
        panel.m_xml->setText(temp.filePath("missing.xml")); panel.stitch();
        CHECK(!panel.isBusy() && published == 1 && panel.m_session == session);
        panel.m_xml->setText(xml);
        panel.stitch(); ++panel.m_revision;
        CHECK(waitUntil([&] { return !panel.isBusy(); })); CHECK(published == 1);
        panel.stitch(); panel.cancelAndWait(); QApplication::processEvents();
        CHECK(published == 1 && !panel.isBusy());

        // Queue failures retain saved frames and navigate to editable review.
        pcv::camera::MockFrameSource errorSource(8, 4);
        CameraCapturePanelV1 errors; bind(errors, errorSource);
        errors.m_directory->setText(temp.path()); errors.m_preflight->setChecked(true);
        errors.startCapture();
        CHECK(waitUntil([&] { return errors.m_recorder->savedFrames().size() == 1; }));
        errors.m_recorder->queue()->fail("test queue overflow"); errors.pollCapture();
        CHECK(waitUntil([&] { return !errors.isBusy(); }));
        CHECK(errors.snapshot().state == CameraCapturePanelV1::State::Error);
        CHECK(errors.snapshot().firstError == "test queue overflow" && errors.m_frames.size() == 1);
        CHECK(errors.snapshot().step == 3 && errors.m_table->isEnabled());

        // Fixed-row compatibility and close/join remain available.
        pcv::camera::MockFrameSource legacySource(8, 4);
        CameraCapturePanelV1 legacy; bind(legacy, legacySource);
        legacy.m_directory->setText(temp.path()); legacy.m_boundaryMode->setCurrentIndex(1);
        legacy.m_rows->setValue(4); legacy.m_preflight->setChecked(true); legacy.startCapture();
        legacy.cancelAndWait(); QApplication::processEvents();
        CHECK(!legacy.isBusy() && legacy.m_frames.size() == 1 && legacy.m_session.contains("capture_v1_"));
        return 0;
    }
};
struct CameraDialogTestAccess {
    static int run(const QString &snapshots) {
        CameraDialog dialog;
        dialog.m_devices.clear(); dialog.m_deviceList->clear();
        dialog.m_deviceList->addItem("Mock");
        dialog.show(); QApplication::processEvents();
        CHECK(!dialog.m_showAdvancedPreview && !dialog.m_capturePanel->isHidden());
        dialog.toggleConnection(); CHECK(dialog.m_connected);
        dialog.toggleAdvancedPreview(); CHECK(dialog.m_showAdvancedPreview && dialog.m_capturePanel->isHidden());
        int previews = 0, frozen = 0;
        dialog.cloudPublished = [&previews](const auto &points) { if (!points.isEmpty()) ++previews; };
        dialog.cloudFrozen = [&frozen](const auto &points) { if (!points.isEmpty()) ++frozen; };
        dialog.toggleStreaming(); CHECK(dialog.m_streaming);
        CHECK(waitUntil([&] { return previews > 0; }));
        dialog.freezeCurrentFrame(); CHECK(frozen == 1);
        dialog.toggleAdvancedPreview(); CHECK(!dialog.m_streaming && !dialog.m_showAdvancedPreview);
        CHECK(dialog.m_capturePanel->snapshot().step == 0);
        if (!snapshots.isEmpty()) {
            QDir().mkpath(snapshots);
            CHECK(dialog.grab().save(QDir(snapshots).filePath("camera-wizard-connection.png")));
        }
        dialog.close(); CHECK(!dialog.isCaptureBusy());
        return 0;
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const int panel = CameraCapturePanelTestAccess::run();
    if (panel) return panel;
    const QString snapshots = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    return CameraDialogTestAccess::run(snapshots);
}
