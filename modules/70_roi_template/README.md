# 70_roi_template

## 当前状态

接口已实现，应用仍负责部分 UI 编排。规范代码位于 `modules/70_roi_template`，target 为 `pcv_m70_roi_template`。

## 职责

临时扫描信息解析、工件坐标系、矩形 ROI、平面映射、模板点云和 Mask 流程。纯几何拟合调用 `pcv_m60_planefitting`，输出调用 `pcv_m80_planeoutput`。

## 接口与测试

主要接口为 `generateTempWorkpiece` 和临时工件相关接口。主要测试为 `temp_workpiece_interface_tests` 及几何回归测试。

最后核对日期：2026-08-31。
