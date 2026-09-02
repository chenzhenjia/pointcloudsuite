#pragma once

#include <pcv/camera/frame_source.h>
#include <pcv/core/point_types.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace pointcloud {

// One frame flowing through the pipeline. Nodes consume a frame and produce
// a new one; the payload is the point cloud plus provenance metadata.
struct LiveFrame {
    QVector<pointcloud::Point3D> points;
    // Source of the frame ("camera.lp" for live, "file.ply" for offline).
    QString sourceMode;
    std::uint32_t frameNumber = 0;
    qint64 timeStamp = 0;
    std::uint64_t streamGeneration = 0;
    bool hasFrame = false;
    QString error;
    bool ok = true;
};

// Context injected into node factories at pipeline construction time. Holds
// the application-level collaborators nodes need without owning them.
struct PipelineContext {
    // Returns the currently active frame source (nullptr when offline).
    std::function<pcv::camera::FrameSource *()> frameSource;
    // canvas.publish target; the application binds it to the canvas.
    std::function<bool(const LiveFrame &)> publish;
};

class PipelineNode {
public:
    virtual ~PipelineNode() = default;
    virtual QString type() const = 0;
    // Nodes are stateless between frames: run() must not keep per-frame state.
    virtual LiveFrame run(const LiveFrame &input) = 0;
};

using NodeFactory = std::function<std::unique_ptr<PipelineNode>(
    const QJsonObject &params, const PipelineContext &context)>;

// Registered node types. Registering a new algorithm is one line in
// PipelineNodeRegistry::builtin().
class PipelineNodeRegistry {
public:
    PipelineNodeRegistry();

    bool registerNode(const QString &type, NodeFactory factory);
    std::unique_ptr<PipelineNode> create(const QJsonObject &step,
                                         const PipelineContext &context) const;

    // The built-in nodes: camera.nextFrame, filter.voxel, canvas.publish.
    static PipelineNodeRegistry builtin();
    static QJsonObject defaultDocument();

private:
    QHash<QString, NodeFactory> m_factories;
};

// JSON-described pipeline ("steps" array). run() executes every node in
// order; when nodeIds is non-empty only those node ids run (for on-demand
// nodes such as user-confirmed plane fitting).
class PipelineRunner {
public:
    static std::unique_ptr<PipelineRunner> fromJson(
        const QJsonObject &document, const PipelineContext &context,
        QString *error = nullptr);
    static std::unique_ptr<PipelineRunner> fromJson(
        const QJsonObject &document, const PipelineContext &context,
        const PipelineNodeRegistry &registry, QString *error = nullptr);

    LiveFrame run(const LiveFrame &input,
                  const std::set<QString> &nodeIds = {}) const;

    bool empty() const { return m_nodes.empty(); }
    QStringList nodeIds() const;

private:
    std::vector<std::unique_ptr<PipelineNode>> m_nodes;
    QVector<QString> m_ids;
};

std::unique_ptr<PipelineRunner> createProductionPipeline(
    const PipelineContext &context, QStringList *warnings = nullptr);

} // namespace pointcloud
