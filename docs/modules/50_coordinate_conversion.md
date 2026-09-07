# 50_coordinate_conversion：坐标转换

状态：已实现。本模块为手眼标定、位姿插值、坐标变换的唯一数学实现来源。规范实现位于 `modules/50_coordinate_conversion/{include,src}`，模块 target 为 `pcv_m50_coordinate_conversion`；旧 target `pcv_m50_coordinateconversion` 保留为迁移期兼容入口（仅转发）。

契约：`camera -> robot_base`，位姿 `[X,Y,Z,A,B,C]`，旋转约定 `Rz(C)*Ry(B)*Rx(A)`；验证刚体矩阵、插值、取消和源索引。

接口/测试：`loadHandEyeCalibration`、`transformLineScanToRobotBase`；`handeye_transform_tests`。

最后核对日期：2026-09-07。本文件历史正文中的旧名称（`modules/50_coordinateconversion`、`pcv_m50_coordinateconversion`）为当时名称，仅用于追溯。

## 变更记录

### 2026-09-06（模块重命名）

- 目录重命名：`modules/50_coordinate_conversion`，target 改为 `pcv_m50_coordinate_conversion`；旧 `pcv_m50_coordinateconversion` 保留为兼容 INTERFACE，转发到新 target。职责（手眼标定、位姿插值、坐标变换）与依赖边界不变。

### 2026-08-31

- 建立独立模块映射和兼容入口；暂不移动共享实现。
- M2/M3：手眼标定和线扫坐标转换源码已迁移到 `modules/50_coordinateconversion`，旧头文件通过 forwarding header 兼容。
