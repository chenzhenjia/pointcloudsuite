# 60_geometry_workpiece

## 当前状态

已实现。规范代码位于 `modules/60_geometry_workpiece`，target 为 `pcv_m60_geometry_workpiece`。旧 60/70 target 和 header 保留为兼容转发。

## 职责

平面拟合与诊断、平面/边缘几何、ROI 匹配、WObj 和临时工件流程。应用入口只负责参数转换、生命周期和结果映射。

## 依赖与测试

纯算法模块不得依赖 `apps/` 或 Qt Widgets。主要测试为 `plane_fitting_tests`；ROI/Mask 测试归入相应应用或接口测试。

最后核对日期：2026-09-07（模块重命名后复核）。
