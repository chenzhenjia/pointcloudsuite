# 10_pointcloud_io：IO、预处理与事务输出 v0.4

核对日期：2026-10-09。状态：实现已有，公共 namespace 迁移未完成；本轮仅静态文档整理。规范实现为 `modules/10_pointcloud_io`，target 为 `pcv_m10_pointcloud_io`。

## 1. 职责与依赖

包含 PLY Header/完整读取、缓存、writer、降采样/统计过滤、相机帧转换/Mock/组帧/亮度图以及 `pcv::io::ArtifactTransaction`。依赖 core、infrastructure、Qt Core/Gui，不依赖应用或 QWidget。相机由 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制；真机 SDK/现场取流属于实验能力。

输入支持 ASCII、binary little-endian/big-endian PLY，按属性名读取，保留点序、sourceIndices、bounds、取消与诊断。完整读取 `readPly()` 不自动写文件；`readPlyCached()` 会生成并验证 canonical Binary LE PLY，写入/回读失败使缓存入口失败。历史“canonical 失败仅警告”已不是当前行为。

## 2. 当前 API 与目标状态

| header | current_api | target_state |
|---|---|---|
| `pcv/io/ply_reader.h`、`cloud_cache.h`、`ply_writer.h` | `pcv::detail::io`：`inspectPlyHeader/readPly/readPlyCached/writeBinaryPly` | 增量提供 `pcv::io` 门面，生产调用逐批迁移 |
| `pcv/processing/downsample.h`、`statistical_filter.h` | `pcv::detail::filtering` | 公共 IO/预处理门面；头路径不等于 namespace |
| `pcv/io/plane_output.h` | `pcv::output::writePlaneOutput` | `pcv::io` 门面，旧入口保留兼容 |
| `pcv/io/artifact_transaction.h` | `pcv::io::ArtifactTransaction` | 已落地唯一最终文件操作边界 |
| `pcv/camera/*` | `pcv::camera` | 保持相机领域命名与实验开关 |

事务至少提供 `stageBinaryPly`、`stageGrayscalePng`、`stageJson`、`stageBytes`、`commit/abort`；构造时设置输出根目录、默认禁止覆盖及取消回调。相对路径受校验，失败/取消回滚，允许覆盖时使用备份。调用方通过 `error()/committed()` 获取状态，不把失败 staging 当作正式输出。

`stageGrayscalePng` 会把非零像素转换为 255，用于二值 Mask；不能用它保存保真亮度纹理。`stageJson` 接收序列化 bytes 并复用 `stageBytes`，领域 schema 校验由调用方负责，不声明事务自动校验 JSON schema。

## 3. 数据与兼容边界

正式 PLY 使用 binary little-endian，Mask 为 Grayscale8 0/255；坐标和 JSON schema 由领域结果及既有契约规定。30/60 可以编排事务，但不得绕开 10 直接提交最终文件。

旧 `pcv_m10_pointcloud_read`、`pcv_m10_pointcloudread`、`pcv_m05_camerainput`、`pcv_io`、`pcv_filtering`、80/output target 与 forwarding header 保留。兼容层不新增算法；`[[deprecated]]` 尚未覆盖所有旧入口，不能写作弃用标记已全部完成。公共结构体扩展需要全量重建，保留既有字段与默认值。

## 4. 验证与开发记录

已有测试：`ply_reader_tests`、`cloud_cache_tests`、`downsample_tests`、`statistical_filter_tests`、`plane_output_tests`、`artifact_transaction_tests`；实验相机另有 `frame_converter_tests`、`frame_source_tests`、`intensity_image_tests` 等，是否注册取决于构建选项。

本次未运行测试/构建。后续迁移覆盖 LE/BE/ASCII、法向/intensity/source index、canonical 失败、相对路径拒绝、覆盖保护、取消和备份回滚，并登记实际结果。

历史正文保留在 [10_pointcloud_read.md](10_pointcloud_read.md)、[05_camerainput.md](05_camerainput.md)、[80_plane_output.md](80_plane_output.md)。当前需求见 [v0.4 基线](../requirements/pointcloudview_v0.4.md)，开发批次见 [收敛方案](../development/module_convergence_v0.4.md)。2026-10-09 仅新建规范文档入口，未改变代码/API。
