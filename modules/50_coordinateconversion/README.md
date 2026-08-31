# 50_coordinateconversion

## 当前状态

已实现。规范代码位于 `modules/50_coordinateconversion/include` 和 `modules/50_coordinateconversion/src`，target 为 `pcv_m50_coordinateconversion`。

## 职责

读取手眼标定 XML，校验刚体矩阵，执行位姿插值和 `camera -> robot_base` 坐标转换，保留取消、源索引和扫描比例语义。

## 接口与测试

主要接口为 `loadHandEyeCalibration`、`transformLineScanToRobotBase`。主要测试为 `handeye_transform_tests`。

最后核对日期：2026-08-31。
