# 70_roi_template

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

兼容入口。实现已迁入 `modules/60_geometry_workpiece`；`pcv_m70_roi_template` 和旧 header 仅转发。

## 职责

临时扫描、工件坐标系、ROI、模板和 Mask 的规范职责归 60；正式输出事务归 10。本目录不得新增重复实现。

## 接口与测试

主要接口为 `pcv::interface::prepareTempWorkpiece` / `finalizeTempWorkpiece` 及临时工件几何接口。主要测试为 `temp_workpiece_interface_tests`、`roi_matching_tests` 及几何回归测试。

最后核对日期：2026-10-09（文档静态核对）（兼容入口同步；规范实现归 60，事务输出归 10）。
