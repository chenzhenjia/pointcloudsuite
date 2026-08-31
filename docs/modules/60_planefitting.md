# 60_planefitting：平面拟合

状态：已实现，抽取中。当前实现位于 `apps/pointcloudview/pointcloudprocessor.*`，目标为独立 n 点拟合、RANSAC/PCA、连通域和第二组三点一致性模块。

接口/测试：`extractPlaneFromThreePoints`、n 点拟合和一致性校验；`pointcloudprocessor_obstacle_tests`（当前为几何/Mask 回归）。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
