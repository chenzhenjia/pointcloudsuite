# 60_planefitting：平面拟合

状态：算法实现已下沉，兼容层保留。`modules/60_planefitting` 现提供完整三点/n 点拟合、RANSAC/PCA 细化、连通域和诊断；应用入口仅负责参数转换和结果映射。旧 `extractPlaneFromPointsLegacy` 源码暂保留待独立清理提交，生产调用已不再使用。

接口/测试：`extractPlaneFromThreePoints`、n 点拟合和一致性校验；`pointcloudprocessor_obstacle_tests`（当前为几何/Mask 回归）。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
- M5.2 首轮：新增独立 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` API；应用入口暂继续调用完整 legacy 算法以保留连通域和 PCA 诊断，待字段完整迁移后再切换。
- M5.2 第二轮：迁移 `calculateBoundsCenter` 及越界/非有限点校验，应用入口通过兼容包装调用模块实现。
- M5.2 契约补齐：`Options` 和 `Result` 已覆盖连通域、RANSAC/PCA 迭代、随机种子、预览延迟分类及断开区域诊断字段；完整算法仍待从 processor 迁移，当前新增字段不改变旧入口行为。
- M5.2 算法迁移完成：`pcv::planefitting::fit` 实现 PCA 初始模型、RANSAC、PCA 重分类、预览延迟分类和连通域筛选；`extractPlaneFromPoints` 改为兼容 forwarding wrapper。新增模块实现与应用级几何回归均通过。
- M5.4：删除 `extractPlaneFromPointsLegacy()`，应用入口继续保留兼容参数转换；处理器不再包含第二套平面拟合算法。
