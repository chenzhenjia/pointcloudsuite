# 30_pointcloudstitch：点云拼接

状态：部分实现。当前实现位于 `apps/pointcloudstitch`，负责多帧流程编排、结果管理和接缝融合调用；配准算法归属 `40_pointcloudregistration`。

接口/测试：`stitching_interface`、`applyTrajectorySeamFusion`；`stitching_interface_tests`、`seam_fusion_tests`。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m30_pointcloudstitch` 兼容入口；保留现有应用流程。
- M3/M4：`stitching_interface` 已迁移到模块源码，target 负责流程编排并依赖配准、坐标转换和读写模块。
