#include "live_pipeline.h"

#include <pcv/camera/frame_converter.h>
#include <pcv/filtering/downsample.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>

#include <cmath>

namespace pointcloud {

namespace {

// ---------------------------------------------------------------------------
// Built-in nodes
// ---------------------------------------------------------------------------

class NextFrameNode final : public PipelineNode {
public:
    explicit NextFrameNode(const PipelineContext &context)
        : m_context(context)
    {
    }

    QString type() const override { return QStringLiteral("camera.nextFrame"); }

    LiveFrame run(const LiveFrame &input) override
    {
        // CameraDialog may already have converted this frame off the GUI
        // thread. In that case the acquisition node is a pass-through.
        if (input.hasFrame) return input;
        LiveFrame frame;
        pcv::camera::FrameSource *source = m_context.frameSource
            ? m_context.frameSource() : nullptr;
        if (!source) {
            frame.ok = false;
            frame.error = QStringLiteral("no active camera frame source");
            return frame;
        }
        auto cameraFrame = source->frameSlot().take();
        if (!cameraFrame) {
            frame.ok = false;
            frame.error = QStringLiteral("no frame available");
            return frame;
        }
        if (cameraFrame->kind == pcv::camera::FrameKind::Exception) {
            frame.ok = false;
            frame.error = QStringLiteral("camera exception: %1")
                              .arg(cameraFrame->exceptionDescription.isEmpty()
                                       ? QStringLiteral("device event") : cameraFrame->exceptionDescription);
            return frame;
        }
        // Route through the source so depth/profile frames use the SDK
        // conversion, matching the CameraDialog path.
        const pcv::camera::PointCloudConvertResult converted =
            source->convertFrame(*cameraFrame);
        if (!converted.ok) {
            frame.ok = false;
            frame.error = converted.error;
            return frame;
        }
        frame.points = converted.points;
        frame.sourceMode = QStringLiteral("camera.lp");
        frame.frameNumber = cameraFrame->frameNumber;
        frame.timeStamp = cameraFrame->timeStamp;
        frame.hasFrame = true;
        return frame;
    }

private:
    PipelineContext m_context;
};

class VoxelNode final : public PipelineNode {
public:
    explicit VoxelNode(const QJsonObject &params)
    {
        m_voxelSize = float(params.value(QStringLiteral("voxelSize")).toDouble(0.25));
        m_centroid = params.value(QStringLiteral("centroid")).toBool(false);
        m_enabled = params.value(QStringLiteral("enabled")).toBool(true);
    }

    QString type() const override { return QStringLiteral("filter.voxel"); }

    LiveFrame run(const LiveFrame &input) override
    {
        LiveFrame out = input;
        if (!out.ok || !m_enabled) return out;
        const pcv::detail::filtering::VoxelDownsampleResult downsampled =
            pcv::detail::filtering::voxelDownsample(
                input.points, m_voxelSize,
                m_centroid ? pcv::detail::filtering::VoxelRepresentative::Centroid
                           : pcv::detail::filtering::VoxelRepresentative::FirstInputPoint);
        out.points = downsampled.points;
        return out;
    }

private:
    float m_voxelSize = 0.25f;
    bool m_centroid = false;
    bool m_enabled = true;
};

class CanvasPublishNode final : public PipelineNode {
public:
    explicit CanvasPublishNode(const PipelineContext &context)
        : m_context(context)
    {
    }

    QString type() const override { return QStringLiteral("canvas.publish"); }

    LiveFrame run(const LiveFrame &input) override
    {
        if (!input.ok) return input;
        if (m_context.publish && !m_context.publish(input)) {
            LiveFrame failed = input;
            failed.ok = false;
            failed.error = QStringLiteral("canvas publish failed");
            return failed;
        }
        return input;
    }

private:
    PipelineContext m_context;
};

} // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

PipelineNodeRegistry::PipelineNodeRegistry() = default;

bool PipelineNodeRegistry::registerNode(const QString &type, NodeFactory factory)
{
    if (type.isEmpty() || !factory) return false;
    m_factories.insert(type, std::move(factory));
    return true;
}

std::unique_ptr<PipelineNode> PipelineNodeRegistry::create(
    const QJsonObject &step, const PipelineContext &context) const
{
    const QString type = step.value(QStringLiteral("type")).toString();
    auto it = m_factories.constFind(type);
    if (it == m_factories.constEnd()) return nullptr;
    const QJsonObject params = step.value(QStringLiteral("params")).toObject();
    return it.value()(params, context);
}

PipelineNodeRegistry PipelineNodeRegistry::builtin()
{
    PipelineNodeRegistry registry;
    registry.registerNode(
        QStringLiteral("camera.nextFrame"),
        [](const QJsonObject &, const PipelineContext &context) {
            return std::make_unique<NextFrameNode>(context);
        });
    registry.registerNode(
        QStringLiteral("filter.voxel"),
        [](const QJsonObject &params, const PipelineContext &) {
            return std::make_unique<VoxelNode>(params);
        });
    registry.registerNode(
        QStringLiteral("canvas.publish"),
        [](const QJsonObject &, const PipelineContext &context) {
            return std::make_unique<CanvasPublishNode>(context);
        });
    return registry;
}

QJsonObject PipelineNodeRegistry::defaultDocument()
{
    QJsonArray steps;
    steps.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("acquire")},
                             {QStringLiteral("type"), QStringLiteral("camera.nextFrame")}});
    steps.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("voxel")},
                             {QStringLiteral("type"), QStringLiteral("filter.voxel")},
                             {QStringLiteral("params"), QJsonObject{
                                 {QStringLiteral("enabled"), false},
                                 {QStringLiteral("voxelSize"), 0.25},
                                 {QStringLiteral("centroid"), false}}}});
    steps.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("publish")},
                             {QStringLiteral("type"), QStringLiteral("canvas.publish")}});
    return QJsonObject{{QStringLiteral("schema_version"),
                        QStringLiteral("pointcloudview-live-pipeline-v1")},
                       {QStringLiteral("steps"), steps}};
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

std::unique_ptr<PipelineRunner> PipelineRunner::fromJson(
    const QJsonObject &document, const PipelineContext &context,
    QString *error)
{
    return fromJson(document, context, PipelineNodeRegistry::builtin(), error);
}

std::unique_ptr<PipelineRunner> PipelineRunner::fromJson(
    const QJsonObject &document, const PipelineContext &context,
    const PipelineNodeRegistry &registry, QString *error)
{
    auto runner = std::make_unique<PipelineRunner>();
    for (const QString &key : document.keys()) {
        if (key != QStringLiteral("schema_version") && key != QStringLiteral("steps")) {
            if (error) *error = QStringLiteral("unknown pipeline document field: %1").arg(key);
            return nullptr;
        }
    }
    const QJsonArray steps = document.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty()) {
        if (error) *error = QStringLiteral("pipeline has no steps");
        return nullptr;
    }
    QSet<QString> ids;
    int voxelCount = 0;
    for (qsizetype index = 0; index < steps.size(); ++index) {
        const QJsonValue &stepValue = steps.at(index);
        if (!stepValue.isObject()) {
            if (error) *error = QStringLiteral("pipeline step must be an object");
            return nullptr;
        }
        const QJsonObject step = stepValue.toObject();
        const QString id = step.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            if (error) *error = QStringLiteral("pipeline step is missing id");
            return nullptr;
        }
        if (ids.contains(id)) {
            if (error) *error = QStringLiteral("pipeline step id is duplicated: %1").arg(id);
            return nullptr;
        }
        ids.insert(id);
        const QString type = step.value(QStringLiteral("type")).toString();
        if (type.isEmpty()) {
            if (error) *error = QStringLiteral("pipeline step is missing type");
            return nullptr;
        }
        for (const QString &key : step.keys()) {
            if (key != QStringLiteral("id") && key != QStringLiteral("type")
                && key != QStringLiteral("params")) {
                if (error) *error = QStringLiteral("unknown pipeline step field: %1").arg(key);
                return nullptr;
            }
        }
        if (index == 0 && type != QStringLiteral("camera.nextFrame")) {
            if (error) *error = QStringLiteral("camera.nextFrame must be the first step");
            return nullptr;
        }
        if (index == steps.size() - 1 && type != QStringLiteral("canvas.publish")) {
            if (error) *error = QStringLiteral("canvas.publish must be the last step");
            return nullptr;
        }
        if (type == QStringLiteral("filter.voxel")) {
            if (++voxelCount > 1) {
                if (error) *error = QStringLiteral("filter.voxel may appear only once");
                return nullptr;
            }
            const QJsonValue paramsValue = step.value(QStringLiteral("params"));
            if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
                if (error) *error = QStringLiteral("filter.voxel params must be an object");
                return nullptr;
            }
            const QJsonObject params = paramsValue.toObject();
            for (const QString &key : params.keys()) {
                if (key != QStringLiteral("enabled") && key != QStringLiteral("voxelSize")
                    && key != QStringLiteral("centroid")) {
                    if (error) *error = QStringLiteral("unknown filter.voxel parameter: %1").arg(key);
                    return nullptr;
                }
            }
            const double voxel = params.value(QStringLiteral("voxelSize")).toDouble(0.25);
            if (!std::isfinite(voxel) || voxel <= 0.0 || voxel > 1000.0) {
                if (error) *error = QStringLiteral("filter.voxel voxelSize is out of range");
                return nullptr;
            }
        }
        auto node = registry.create(step, context);
        if (!node) {
            if (error) {
                *error = QStringLiteral("unknown or invalid node type: %1")
                             .arg(step.value(QStringLiteral("type")).toString());
            }
            return nullptr;
        }
        runner->m_ids.push_back(id);
        runner->m_nodes.push_back(std::move(node));
    }
    return runner;
}

LiveFrame PipelineRunner::run(const LiveFrame &input,
                              const std::set<QString> &nodeIds) const
{
    LiveFrame frame = input;
    const bool filterActive = !nodeIds.empty();
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        if (filterActive && nodeIds.find(m_ids[i]) == nodeIds.end()) continue;
        frame = m_nodes[i]->run(frame);
        if (!frame.ok) break;
    }
    return frame;
}

QStringList PipelineRunner::nodeIds() const
{
    return m_ids;
}

std::unique_ptr<PipelineRunner> createProductionPipeline(
    const PipelineContext &context, QStringList *warnings)
{
    QString path;
    const QStringList arguments = QCoreApplication::arguments();
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--live-pipeline") && index + 1 < arguments.size()) {
            path = QDir::cleanPath(arguments.at(index + 1));
            break;
        }
        if (argument.startsWith(QStringLiteral("--live-pipeline="))) {
            path = QDir::cleanPath(argument.mid(16));
            break;
        }
    }
    const QString env = qEnvironmentVariable("PCV_LIVE_PIPELINE_FILE");
    if (path.isEmpty() && !env.trimmed().isEmpty()) path = QDir::cleanPath(env);
    if (path.isEmpty()) {
        path = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("config/live_pipeline.json"));
        if (!QFileInfo::exists(path)) path.clear();
    }
    QJsonObject document;
    bool externalValid = false;
    if (!path.isEmpty()) {
        QFile file(path);
        QJsonParseError parseError;
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
                document = parsed.object();
                externalValid = true;
            } else if (warnings) {
                warnings->push_back(QStringLiteral("live pipeline JSON invalid: %1").arg(parseError.errorString()));
            }
        } else if (warnings) {
            warnings->push_back(QStringLiteral("cannot read live pipeline file: %1").arg(path));
        }
    }
    if (!externalValid) document = PipelineNodeRegistry::defaultDocument();
    if (document.value(QStringLiteral("schema_version")).toString()
            != QStringLiteral("pointcloudview-live-pipeline-v1")) {
        if (warnings) warnings->push_back(QStringLiteral("live pipeline schema_version is invalid; using built-in default"));
        document = PipelineNodeRegistry::defaultDocument();
    }
    QString error;
    auto runner = PipelineRunner::fromJson(document, context, &error);
    if (!runner) {
        if (warnings) warnings->push_back(QStringLiteral("live pipeline rejected: %1; using built-in default").arg(error));
        runner = PipelineRunner::fromJson(PipelineNodeRegistry::defaultDocument(), context, &error);
    }
    return runner;
}

} // namespace pointcloud
