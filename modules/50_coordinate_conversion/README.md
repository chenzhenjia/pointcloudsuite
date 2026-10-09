# 50_coordinate_conversion

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

已实现。规范代码位于 `modules/50_coordinate_conversion/include` 和 `modules/50_coordinate_conversion/src`，target 为 `pcv_m50_coordinate_conversion`。旧 target `pcv_m50_coordinateconversion` 保留为迁移期兼容入口（仅转发）。

## 职责

手眼标定、位姿插值、坐标变换（唯一数学实现来源）。读取手眼标定 XML，校验刚体矩阵，执行位姿插值和 `camera -> robot_base` 坐标转换，保留取消、源索引和扫描比例语义。

## 接口与测试

当前 namespace 为 `pointcloud::`，主要接口为 `loadHandEyeCalibration`、`composeBaseFromDepth`、`computeScanTravel`、`transformLineScanToRobotBase`。手眼链固定 `T_base_depth = T_base_flange * T_flange_depth`；`LineProfileXz` 使用 `[X,0,Z]`，Y 仅表示 `y/signedTravel` 扫描进度。主要测试为 `handeye_transform_tests`。

最后核对日期：2026-10-09（文档静态核对）（v0.4 唯一坐标转换实现边界复核）。
