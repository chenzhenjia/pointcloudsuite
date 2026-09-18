# 70_roi_template

## 当前状态

兼容入口。实现已迁入 `modules/60_geometry_workpiece`；`pcv_m70_roi_template` 和旧 header 仅转发。

## 职责

临时扫描、工件坐标系、ROI、模板和 Mask 的规范职责归 60；正式输出事务归 10。本目录不得新增重复实现。

## 接口与测试

主要接口为 `generateTempWorkpiece` 和临时工件相关接口。主要测试为 `temp_workpiece_interface_tests`、`roi_matching_tests` 及几何回归测试。

最后核对日期：2026-09-17（兼容入口同步；规范实现归 60，事务输出归 10）。
