# 50_coordinate_conversion

## 当前状态

已实现。规范代码位于 `modules/50_coordinate_conversion/include` 和 `modules/50_coordinate_conversion/src`，target 为 `pcv_m50_coordinate_conversion`。旧 target `pcv_m50_coordinateconversion` 保留为迁移期兼容入口（仅转发）。

## 职责

手眼标定、位姿插值、坐标变换（唯一数学实现来源）。读取手眼标定 XML，校验刚体矩阵，执行位姿插值和 `camera -> robot_base` 坐标转换，保留取消、源索引和扫描比例语义。

## 接口与测试

主要接口为 `loadHandEyeCalibration`、`transformLineScanToRobotBase`。主要测试为 `handeye_transform_tests`。

最后核对日期：2026-09-07（模块重命名后复核）。
