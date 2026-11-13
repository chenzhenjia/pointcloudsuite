#pragma once

#include <pcv/stitching/multiframe_registration.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/stitching/seam_fusion_types.h>
#include <pcv/stitching/world_cloud_types.h>

#include <QFutureWatcher>
#include <QMainWindow>
#include <QMatrix4x4>
#include <QVector>

#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class StitchingWindow; }
QT_END_NAMESPACE

struct StitchingPublication {
    QVector<pointcloud::Point3D> points;
    QString formalPly;
    QString previewPly;
    QString mappingFile;
    QString reportJson;
    QString outputDirectory;
    QVector<quint8> textureGrayscale;
    QVector<quint8> textureValid;
    qsizetype textureMappedPointCount = 0;
    qsizetype textureSkippedPointCount = 0;
    QString textureManifest;
    QString intensitySourceDirectory;
};

struct StitchTaskResult {
    pointcloud::MultiFrameRegistrationResult merge;
    pointcloud::IcpOptions icpOptions;
    pointcloud::SeamFusionResult seamFusion;
    QString outputDirectory;
    QString mergedFile;
    QString previewFile;
    QString reportFile;
    QString mappingFile;
    QStringList retainedSourceFiles;
    QStringList diagnosticPreIcpFiles;
    QStringList diagnosticPostIcpFiles;
    qsizetype previewCount = 0;
    qsizetype pointsBeforeOutputDownsample = 0;
    float outputDownsampleVoxelSize = 0.0f;
    qint64 seamFusionMs = 0;
    qint64 outputMs = 0;
    QString error;
    bool cancelled = false;
    bool registrationAccepted = false;
    bool multiFrameConsistent = false;
    bool transformOnly = false;
    bool registrationApplied = false;
    bool seamFusionApplied = false;
    bool formalOutput = false;
    bool ok = false;
};

class StitchingWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit StitchingWindow(QWidget *parent = nullptr);
    ~StitchingWindow() override;

    bool isBusy() const;
    void cancelAndWait();
    void startProcessRequest(const pcv::interface::StitchingRequest &request, const QString &jobId);
    std::function<bool()> canStartTask;

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void stitchingSucceeded(const StitchingPublication &publication);
    void processJobFailed(const QString &jobId, const QString &code, const QString &message);

private slots:
    void browseCalibration();
    void browseOutputDirectory();
    void addPlyFiles();
    void addPlyFolder();
    void removeSelectedRows();
    void clearFiles();
    void startStitching();
    void cancelStitching();
    void stitchingFinished();

private:
    friend struct StitchingUiTestAccess;
    void addPlyPath(const QString &path);
    void moveSelectedRow(int offset);
    void setBusy(bool busy);
    bool readCalibration(QMatrix4x4 *transform, QString *error) const;
    QVector<pcv::stitching::WorldCloudInput> collectInputs(QString *error) const;
    pointcloud::IcpOptions collectIcpOptions(QString *error) const;
    pcv::interface::StitchingRequest collectRequest(QString *error) const;
    void updateProcessingModeUi();
    void appendLog(const QString &text);

    Ui::StitchingWindow *ui = nullptr;
    QFutureWatcher<StitchTaskResult> *m_watcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    quint64 m_taskRevision = 0;
    bool m_stopping = false;
    std::unique_ptr<pcv::interface::StitchingRequest> m_processRequest;
    QString m_processJobId;
};
