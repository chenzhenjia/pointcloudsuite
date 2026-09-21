# 50_coordinate_conversion：坐标转换

状态：已实现。本模块为手眼标定、位姿插值、坐标变换的唯一数学实现来源。规范实现位于 `modules/50_coordinate_conversion/{include,src}`，模块 target 为 `pcv_m50_coordinate_conversion`；旧 target `pcv_m50_coordinateconversion` 保留为迁移期兼容入口（仅转发）。

契约：`camera -> robot_base`，位姿 `[X,Y,Z,A,B,C]`，旋转约定 `Rz(C)*Ry(B)*Rx(A)`；验证刚体矩阵、插值、取消和源索引。

接口/测试：`loadHandEyeCalibration`、`transformLineScanToRobotBase`；`handeye_transform_tests`。

最后核对日期：2026-09-21。本模块仍是 v0.4 坐标转换唯一实现；历史正文中的旧名称仅用于追溯。

## 变更记录

### 2026-09-21（应用状态修复与位姿显示契约校准）

- `apps/pointcloudview` 的 JSON Start/End 摘要改为 `[X,Y,Z,Rx,Ry,Rz]`，底层模块 50 的位姿字段和旋转数学未改变。
- PLY/JSON 临时工件入口继续共用 `transformLineScanToRobotBase()`；本轮仅修复应用会话生命周期和失败恢复，不新增坐标转换实现。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke 和真实标定样件仍待验证。

### 2026-09-20（PLY 线路复用：参数校验直接调用与取消回调透传）

- 需求：`apps/pointcloudview` 的“打开 PLY”入口必须复用本模块作为唯一坐标转换实现，并显示手眼标定方向和矩阵校验结果。
- 文件：`apps/pointcloudview/mainwindow.cpp`（PLY 线路参数对话框在选择/变更 XML 后立即调用 `loadHandEyeCalibration()` 与 `validateRigidTransform()`）；`modules/60_geometry_workpiece/src/temp_workpiece_interface.cpp`（转换选项显式固定 `layout = LineProfileXz`、`interpolateRotation = true`、`sampleStride = 1`，并把取消回调同时透传给 `readPlyCached()` 的 `PlyReadOptions::isCancelled`）。
- 行为/API：模块 50 的公共头、函数签名、位姿约定（`[X,Y,Z,A,B,C]`、`A=Rx`、`B=Ry`、`C=Rz`、`Rz(C)*Ry(B)*Rx(A)`）、单位（mm/deg）和错误语义均未变化；未新增导出符号，未新增第二套转换实现。
- 验证：历史记录为静态检查；2026-09-21 源码树外 Debug 全量构建成功，CTest `15/15` 通过。
- 风险：PLY 线路的 Start/End 插值方向与手眼方向仍需真实标定文件和生产样件验收。

### 2026-09-21（代码验收记录）

- Debug/Release 源码树外构建成功，双配置全量 CTest 均为 `15/15`；转换数学、公共位姿字段和错误码契约未改变。
- GUI smoke、真实手眼 XML 和机器人运动方向未在本次自动化环境中验证。

### 2026-09-06（模块重命名）

- 目录重命名：`modules/50_coordinate_conversion`，target 改为 `pcv_m50_coordinate_conversion`；旧 `pcv_m50_coordinateconversion` 保留为兼容 INTERFACE，转发到新 target。职责（手眼标定、位姿插值、坐标变换）与依赖边界不变。

### 2026-08-31

- 建立独立模块映射和兼容入口；暂不移动共享实现。
- M2/M3：手眼标定和线扫坐标转换源码已迁移到 `modules/50_coordinateconversion`，旧头文件通过 forwarding header 兼容。
