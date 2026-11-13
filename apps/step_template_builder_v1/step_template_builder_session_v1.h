#pragma once
#include <pcv/workpiece/template_package_v1.h>
#include <pcv/render/pointcloud_canvas_contract.h>
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QHash>
#include <QRectF>
#include <atomic>

namespace pcv::stepbuilder {
// Owns immutable sampling, source indices and one cancellable background job.
class StepTemplateBuilderSessionV1 final : public QObject {
    Q_OBJECT
public:
    explicit StepTemplateBuilderSessionV1(QObject *parent=nullptr);
    ~StepTemplateBuilderSessionV1() override;
    bool busy() const { return m_busy; }
    bool confirmed() const { return m_confirmed; }
    bool canProcess() const;
    bool canUndo() const { return !m_history.isEmpty(); }
    const pcv::workpiece::StepModelResult &model() const { return m_model; }
    const pcv::workpiece::CadCloudResult &cloud() const { return m_cloud; }
    const pcv::workpiece::TemplatePackageV1 &package() const { return m_package; }
    const QVector<pointcloud::Point3D> &displayPoints() const { return m_display; }
    const QVector<int> &displayMap() const { return m_mapping; }
    const QSet<int> &workFaces() const { return m_work; }
    const QSet<int> &interferenceFaces() const { return m_interference; }
    qsizetype removedCount() const { return m_removed.size(); }
    qsizetype pendingCount() const { return m_pending.size(); }
    const pcv::workpiece::WorkpieceTemplateResult &saved() const { return m_saved; }
    void importStep(const QString &file,double spacing);
    void invalidateSampling();
    void toggleFace(int faceId,bool interference);
    void clearRoles();
    void selectRectangle(pcv::render::CloudProjectionSnapshotV1 view,QRectF rectangle);
    void clearPending();
    void erasePending();
    void undo();
    void restore();
    void process();
    void confirm();
    void save(const QString &name,const QString &material,const QString &root);
    void metadataChanged();
    void cancel();
    void shutdown();
    QString workpieceId() const { return m_workpieceId; }
    QString status() const { return m_status; }
signals:
    void changed();
    void displayChanged();
    void savedVersion(const QString &path);
private:
    friend struct StepTemplateBuilderSessionTestAccessV1;
    enum class Kind { Import, Selection, Trim, Process, Save };
    struct Result {
        bool ok=false;
        QString error;
        pcv::workpiece::StepModelResult model;
        pcv::workpiece::CadCloudResult cloud;
        pcv::workpiece::TemplatePackageV1 package;
        pcv::workpiece::WorkpieceTemplateResult saved;
        QSet<int> removed,pending;
        QVector<int> mapping;
        QVector<pointcloud::Point3D> display;
        QHash<int,qsizetype> retained;
    };
    using Job=std::function<Result(const std::shared_ptr<std::atomic_bool> &)>;
    void start(Kind kind,const QString &status,Job job);
    void trimTo(QSet<int> removed,int historyAction,QSet<int> delta={});
    void invalidateGeometry();
    static bool buildDisplay(Result &,const pcv::workpiece::CadCloudResult &,
        const std::shared_ptr<std::atomic_bool> &);
    pcv::workpiece::StepModelResult m_model;
    pcv::workpiece::CadCloudResult m_cloud;
    pcv::workpiece::TemplatePackageV1 m_package;
    pcv::workpiece::WorkpieceTemplateResult m_saved;
    QVector<pointcloud::Point3D> m_display;
    QVector<int> m_mapping;
    QHash<int,qsizetype> m_retained;
    QSet<int> m_work,m_interference,m_removed,m_pending;
    QVector<QSet<int>> m_history;
    QString m_workpieceId,m_status;
    bool m_busy=false,m_confirmed=false,m_closing=false;
    quint64 m_revision=0;
    int m_historyAction=0;
    QSet<int> m_historyDelta;
    QThreadPool m_pool;
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
