# 60_planefitting：平面拟合

状态：部分实现，抽取中。基础三点/n 点平面模型拟合已位于 `modules/60_planefitting`；完整 RANSAC/PCA 细化、连通域和一致性逻辑仍在 `apps/pointcloudview/pointcloudprocessor.*`。

接口/测试：`extractPlaneFromThreePoints`、n 点拟合和一致性校验；`pointcloudprocessor_obstacle_tests`（当前为几何/Mask 回归）。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
- M5.2 首轮：新增独立 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` API；应用入口已通过兼容包装调用基础拟合，ROI/连通域逻辑暂未迁移。
