# 40_pointcloudregistration：点云配准

状态：已实现。规范实现位于 `modules/40_pointcloudregistration/{include,src}`，负责 ICP、相邻帧配准、结构点覆盖和诊断；模块 target 为 `pcv_m40_pointcloudregistration`。

接口/测试：`multiframe_registration`、`seam_fusion`；`multiframe_registration_tests`、`seam_fusion_tests`。

## 变更记录

### 2026-08-31

- 建立模块边界和兼容入口；配准实现暂留 `src/registration`。
- M3：多帧 ICP 与接缝融合源码已迁移到 `modules/40_pointcloudregistration`，旧 `pcv_registration` 保留兼容聚合。
- M5.1：共享 `seam_fusion` 已从固定禁用占位改为真实投影重叠检查、融合带裁剪和候选最近邻插值；应用层入口保持兼容包装。当前仍需补充多帧、无重叠、取消和 bounded-memory 回归。
