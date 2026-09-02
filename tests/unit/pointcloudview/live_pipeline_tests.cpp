#include "live_pipeline.h"

#include <pcv/camera/mock_frame_source.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {
bool require(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

QJsonObject step(const QString &id, const QString &type, const QJsonObject &params = {})
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("type"), type);
    if (!params.isEmpty()) object.insert(QStringLiteral("params"), params);
    return object;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Full live chain: mock source -> voxel -> publish.
    {
        pcv::camera::MockFrameSource source(32, 32);
        source.start();

        int publishCount = 0;
        pointcloud::PipelineContext context;
        context.frameSource = [&source]() { return static_cast<pcv::camera::FrameSource *>(&source); };
        context.publish = [&publishCount](const pointcloud::LiveFrame &frame) {
            if (frame.ok && !frame.points.isEmpty()) ++publishCount;
            return true;
        };

        QJsonArray steps;
        steps.append(step(QStringLiteral("acquire"), QStringLiteral("camera.nextFrame")));
        steps.append(step(QStringLiteral("denoise"), QStringLiteral("filter.voxel"),
                          QJsonObject{{QStringLiteral("voxelSize"), 0.5}}));
        steps.append(step(QStringLiteral("publish"), QStringLiteral("canvas.publish")));
        QJsonObject document;
        document.insert(QStringLiteral("steps"), steps);

        QString error;
        auto runner = pointcloud::PipelineRunner::fromJson(document, context, &error);
        if (!require(runner != nullptr, "pipeline should parse the live chain")) return 1;
        if (!require(runner->nodeIds() == QStringList{QStringLiteral("acquire"),
                                                      QStringLiteral("denoise"),
                                                      QStringLiteral("publish")},
                     "node ids should preserve JSON order")) return 1;

        pointcloud::LiveFrame input;
        const auto out = runner->run(input);
        if (!require(out.ok && publishCount == 1,
                     "pipeline should acquire, downsample and publish one frame")) return 1;
        if (!require(out.sourceMode == QStringLiteral("camera.lp"),
                     "frame source mode should be camera.lp")) return 1;
        source.stop();
    }

    // Unknown node type is rejected at parse time.
    {
        QJsonArray steps;
        steps.append(step(QStringLiteral("bad"), QStringLiteral("no.such.node")));
        QJsonObject document;
        document.insert(QStringLiteral("steps"), steps);
        QString error;
        auto runner = pointcloud::PipelineRunner::fromJson(document, {}, &error);
        if (!require(runner == nullptr,
                     "unknown node type must fail parsing")) return 1;
    }

    // Strict ordering and duplicate-id validation.
    {
        QJsonObject document;
        document.insert(QStringLiteral("steps"), QJsonArray{
            step(QStringLiteral("a"), QStringLiteral("camera.nextFrame")),
            step(QStringLiteral("a"), QStringLiteral("canvas.publish"))});
        QString error;
        auto runner = pointcloud::PipelineRunner::fromJson(document, {}, &error);
        if (!require(!runner && error.contains(QStringLiteral("duplicated")),
                     "duplicate pipeline ids must be rejected")) return 1;
    }

    // A pre-converted frame passes through camera.nextFrame without consuming
    // another frame from the source slot.
    {
        pcv::camera::MockFrameSource source(8, 8);
        source.start();
        pointcloud::PipelineContext context;
        context.frameSource = [&source]() { return static_cast<pcv::camera::FrameSource *>(&source); };
        context.publish = [](const pointcloud::LiveFrame &) { return true; };
        auto runner = pointcloud::PipelineRunner::fromJson(
            pointcloud::PipelineNodeRegistry::defaultDocument(), context);
        pointcloud::LiveFrame input;
        input.ok = true;
        input.hasFrame = true;
        input.points = {pointcloud::Point3D{1, 2, 3, 0, 0, 1}};
        const auto out = runner->run(input);
        if (!require(out.hasFrame && out.points.size() == 1,
                     "pre-converted live frame must not be consumed again")) return 1;
        source.stop();
    }

    // on-demand node filtering: only the requested node id runs.
    {
        pcv::camera::MockFrameSource source(8, 8);
        source.start();
        int publishCount = 0;
        pointcloud::PipelineContext context;
        context.frameSource = [&source]() { return static_cast<pcv::camera::FrameSource *>(&source); };
        context.publish = [&publishCount](const pointcloud::LiveFrame &) { ++publishCount; return true; };

        QJsonArray steps;
        steps.append(step(QStringLiteral("acquire"), QStringLiteral("camera.nextFrame")));
        steps.append(step(QStringLiteral("publish"), QStringLiteral("canvas.publish")));
        QJsonObject document;
        document.insert(QStringLiteral("steps"), steps);
        auto runner = pointcloud::PipelineRunner::fromJson(document, context);

        pointcloud::LiveFrame input;
        const auto out = runner->run(input, {QStringLiteral("publish")});
        // acquire skipped -> input stays not-ok from missing source frame? It
        // is still ok: publish increments without a point cloud.
        if (!require(publishCount == 1, "filtered run should only execute publish")) return 1;
        if (!require(out.ok, "filtered run should keep ok flag")) return 1;
        source.stop();
    }

    std::cout << "live_pipeline_tests: PASS\n";
    return 0;
}
