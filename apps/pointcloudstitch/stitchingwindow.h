#pragma once

#include "pointcloudprocessor.h"
#include "seamfusion.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QMatrix4x4>
#include <QVector>

#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class StitchingWindow; }
QT_END_NAMESPACE

struct StitchTaskResult {
    pointcloud::WorldCloudMergeResult merge;
    pointcloud::IcpOptions icpOptions;
    SeamFusionResult seamFusion;
    QString outputDirectory;
    QString mergedFile;
    QString previewFile;
    QString reportFile;
    QString mappingFile;
    QStringList retainedSourceFiles;
    qsizetype previewCount = 0;
    qsizetype pointsBeforeOutputDownsample = 0;
    float outputDownsampleVoxelSize = 0.0f;
    QString error;
    bool cancelled = false;
    bool registrationAccepted = false;
    bool multiFrameConsistent = false;
    bool transformOnly = false;
    bool ok = false;
};

class StitchingWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit StitchingWindow(QWidget *parent = nullptr);
    ~StitchingWindow() override;

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
    void addPlyPath(const QString &path);
    void setBusy(bool busy);
    bool readCalibration(QMatrix4x4 *transform, QString *error) const;
    QVector<pointcloud::WorldCloudInput> collectInputs(QString *error) const;
    pointcloud::IcpOptions collectIcpOptions(QString *error) const;
    void updateProcessingModeUi();
    void appendLog(const QString &text);

    Ui::StitchingWindow *ui = nullptr;
    QFutureWatcher<StitchTaskResult> *m_watcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
};
