# 60_geometry_workpiece：工件几何

状态：已实现，兼容入口保留。规范实现位于 `modules/60_geometry_workpiece/{include,src}`，模块 target 为 `pcv_m60_geometry_workpiece`；`pcv_m60_plane_fitting`、`pcv_m60_planefitting` 和 `pcv_m70_roi_template` 仅转发。职责包括平面拟合与诊断、平面/边缘几何、ROI、WObj 和临时工件流程；正式文件通过 10 的事务 API 提交。应用入口仅负责参数转换、生命周期和结果映射。本文件文件名保留旧名称以避免历史链接失效。

接口/测试：`pcv::geometry`、`pcv::workpiece`，以及兼容的 `pcv::planefitting::fit`；`plane_fitting_tests`、`roi_matching_tests`、`temp_workpiece_interface_tests` 和几何回归测试。

最后核对日期：2026-09-17。纯算法实现不得依赖 `apps/` 或 Qt Widgets。本文件历史正文中的旧名称与旧职责为当时状态，仅用于追溯。

## 变更记录

### 2026-09-06（模块重命名）

- 目录重命名：`modules/60_plane_fitting`，target 改为 `pcv_m60_plane_fitting`；旧 `pcv_m60_planefitting` 保留为兼容 INTERFACE，转发到新 target。职责（平面拟合与几何诊断）不变。

### 2026-08-31

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
- M5.2 首轮：新增独立 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` API；应用入口暂继续调用完整 legacy 算法以保留连通域和 PCA 诊断，待字段完整迁移后再切换。
- M5.2 第二轮：迁移 `calculateBoundsCenter` 及越界/非有限点校验，应用入口通过兼容包装调用模块实现。
- M5.2 契约补齐：`Options` 和 `Result` 已覆盖连通域、RANSAC/PCA 迭代、随机种子、预览延迟分类及断开区域诊断字段；完整算法仍待从 processor 迁移，当前新增字段不改变旧入口行为。
- M5.2 算法迁移完成：`pcv::planefitting::fit` 实现 PCA 初始模型、RANSAC、PCA 重分类、预览延迟分类和连通域筛选；`extractPlaneFromPoints` 改为兼容 forwarding wrapper。新增模块实现与应用级几何回归均通过。
- M5.4：删除 `extractPlaneFromPointsLegacy()`，应用入口继续保留兼容参数转换；处理器不再包含第二套平面拟合算法。
