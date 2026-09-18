# 30_pointcloud_stitch

## 当前状态

已实现。规范代码位于 `modules/30_pointcloud_stitch`，target 为 `pcv_m30_pointcloud_stitch`。旧 target `pcv_m30_pointcloudstitch` 保留为迁移期兼容入口（仅转发）。

## 职责

多帧拼接唯一编排：读取、坐标转换调用、ICP/多帧配准、seam fusion、拼接诊断和 manifest。seam fusion 与多帧配准实现均位于本模块 `pcv::stitching`；最终文件通过 10 的 `pcv::io::ArtifactTransaction` 提交。40 仅为规划占位和兼容聚合入口。

## 接口与测试

统一接口为分组式 `pcv::interface::StitchingRequest` / `StitchingResult` 和 `stitch()`；旧 `StitchingOptions` / `stitchRawLineProfiles` 为弃用兼容入口。主要测试为 `stitching_interface_tests`、`multiframe_registration_tests`、`seam_fusion_tests` 和并行等价测试。应用只负责 UI、输入组织、进度和结果展示。

最后核对日期：2026-09-17（ICP 归属、分组接口和事务输出边界复核）。
