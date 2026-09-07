# 10_pointcloud_read

## 当前状态

已实现。规范代码位于 `modules/10_pointcloud_read/include` 和 `modules/10_pointcloud_read/src`，target 为 `pcv_m10_pointcloud_read`。旧 target `pcv_m10_pointcloudread`、`pcv_m05_camerainput` 与兼容 target `pcv_io` 保留为迁移期兼容入口（仅转发）。原 `05_camerainput` 已并入本模块。

## 职责

点云输入（PLY 读写与缓存）与相机帧采集转换。PLY ASCII、binary little-endian、binary big-endian 读写，属性校验、缓存、取消、进度、点序和 bounds 保持；相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现（`PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力，无 SDK 仍可构建纯函数转换与 Mock 源）。

## 依赖与测试

不得依赖应用或 Qt Widgets。主要测试为 `ply_reader_tests`、`cloud_cache_tests`，以及相机侧 `frame_converter_tests`、`frame_source_tests`。

最后核对日期：2026-09-07（模块重命名与 05 并入后复核）。
