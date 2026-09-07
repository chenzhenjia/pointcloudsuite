# 30_pointcloud_stitch

## 当前状态

已实现。规范代码位于 `modules/30_pointcloud_stitch`，target 为 `pcv_m30_pointcloud_stitch`。旧 target `pcv_m30_pointcloudstitch` 保留为迁移期兼容入口（仅转发）。

## 职责

多帧拼接唯一编排：读取、坐标转换调用、多帧匹配调用、seam fusion、拼接诊断、正式输出与失败回滚。seam fusion 实现与公共类型位于本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；多帧匹配/底层配准由 `pcv_m40_pointcloud_matching` 提供，模块 40 不再负责 seam fusion 编排与最终拼接输出。

## 接口与测试

统一接口为 `pcv::interface::stitchRawLineProfiles`。主要测试为 `stitching_interface_tests`、`seam_fusion_tests`。应用只负责 UI、输入组织、进度和结果展示。

最后核对日期：2026-09-07（模块重命名与 30/40 职责重划后复核）。
