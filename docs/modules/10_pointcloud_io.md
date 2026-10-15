# 10_pointcloud_io：IO、预处理与事务输出

版本 v0.5，核对日期 2026-10-15。target：`pcv_m10_pointcloud_io`；依赖 pcv_core、pcv_infrastructure、Qt Core/Gui，不依赖 apps/QWidget。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [日志](../changelog/v0.5.md)。

## 当前 API

| header | namespace | capability |
|---|---|---|
| pcv/io/ply_reader.h、cloud_cache.h、ply_writer.h | pcv::detail::io | inspectPlyHeader/readPly/readPlyCached/writeBinaryPly |
| pcv/processing/downsample.h、statistical_filter.h | pcv::detail::filtering | 降采样与统计过滤 |
| pcv/io/artifact_transaction.h | pcv::io | ArtifactTransaction |
| pcv/io/plane_output.h | pcv::output | writePlaneOutput 旧三件套入口 |
| pcv/camera/* | pcv::camera | 帧转换、Mock、最新帧槽、组帧、SDK 可选 |

PLY 支持 ASCII/LE/BE，按属性名读取坐标/法向，保留点序、source index、bounds、取消与非有限统计。readPly 只读；缓存入口生成/验证 canonical Binary LE，写入或回读失败为失败，不沿用历史仅警告说法。

## 输出与相机边界

ArtifactTransaction 提供 stageBinaryPly/stageGrayscalePng/stageJson/stageBytes/commit/abort，默认不覆盖，校验输出根及相对路径，使用 staging、备份和失败回滚。Mask writer 转 Grayscale8 后非零变 255；JSON bytes 不自动做 schema 校验。旧 writePlaneOutput 仍有自己的事务实现，在 10 内但未复用该类，后续 P2 收敛。

相机为 PCV_BUILD_EXPERIMENTAL_CAMERA 控制的实验能力，根默认 ON、构建脚本默认 OFF；SDK 头/库存在才编译真机源，无 SDK 保留纯函数与 Mock。LatestFrameSlot 为 latest-frame-wins；SDK 数据须复制到受控生命周期，真机 C16/ABC16 SDK 转换失败不发布近似坐标，完整扫描门禁及停止/重连需现场验收。实时预览历史 300000 点显示预算不代表完整生产点云。

## 兼容与验收

旧 05、10 read、pcv_io/filtering、80/output target/header 保留转发，禁止重复实现。当前 namespace 门面迁移未完成。

测试归属：ply_reader、cloud_cache、artifact_transaction、downsample、statistical_filter、plane_output；相机另有 frame_converter/frame_source/intensity_image 及应用 pipeline 测试，注册依构建选项。后续覆盖 LE/BE/ASCII、法向/强度、缓存失败、路径拒绝、覆盖/取消/回滚。历史 DP2240 试验见[历史归档](../archive/history_pre_v0.5.md)，不能替代当前现场验收。本轮未构建或运行测试。
