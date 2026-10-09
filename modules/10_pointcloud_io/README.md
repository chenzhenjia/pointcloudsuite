# 10_pointcloud_io

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

已实现。规范代码位于 `modules/10_pointcloud_io`，target 为 `pcv_m10_pointcloud_io`。旧 target `pcv_m10_pointcloud_read`、`pcv_m10_pointcloudread`、`pcv_m05_camerainput`、`pcv_io`、`pcv_filtering` 和 `pcv_m80_plane_output` 保留为兼容转发。

## 职责

PLY/缓存、相机帧采集转换、过滤预处理、PNG/PLY/JSON writer 和通用 `pcv::io::ArtifactTransaction`。事务支持输出根与相对路径校验、覆盖策略、取消、备份、提交和回滚。

## 依赖与测试

不得依赖应用或 Qt Widgets。主要测试为 `ply_reader_tests`、`cloud_cache_tests`、`downsample_tests`、`statistical_filter_tests`、`plane_output_tests`、`artifact_transaction_tests`，以及相机侧 `frame_converter_tests`、`frame_source_tests`。

最后核对日期：2026-10-09（文档静态核对）（过滤、writer、事务输出和 UI 交接边界同步后复核）。
