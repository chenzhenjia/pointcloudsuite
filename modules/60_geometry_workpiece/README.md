# 60_geometry_workpiece

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

已实现。规范代码位于 `modules/60_geometry_workpiece`，target 为 `pcv_m60_geometry_workpiece`。旧 60/70 target 和 header 保留为兼容转发。

## 职责

平面拟合与诊断、平面/边缘几何、ROI 匹配、WObj、临时工件、扫描路径规划和严格原始栅格亮度映射。扫描规划不包含全剖/点剖刀路算法或机器人执行。应用入口只负责参数转换、生命周期和结果映射。

## 依赖与测试

纯算法模块不得依赖 `apps/` 或 Qt Widgets。主要测试为 `plane_fitting_tests`、`plane_fitting_equivalence_tests_v2`、`roi_matching_tests`、`temp_workpiece_interface_tests`、`scan_planner_tests` 和 `texture_mapping_tests`；应用实验入口另有 `texture_experiment_tests`。

最后核对日期：2026-10-09（文档静态核对）（60 聚合平面、边缘、ROI、WObj 和临时工件后复核）。
