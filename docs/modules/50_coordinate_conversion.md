# 50_coordinate_conversion：坐标转换

状态：已实现。本模块为手眼标定、位姿插值、坐标变换的**唯一数学实现来源**。规范实现位于
`modules/50_coordinate_conversion/{include,src}`，模块 target 为 `pcv_m50_coordinate_conversion`；旧 target
`pcv_m50_coordinateconversion` 保留为迁移期兼容入口（仅转发）。

契约（唯一坐标链）：

- `RTmatDepth2robot` 表示 `T_flange_depth`，即位姿字段 `HandEyeCalibration::flangeFromDepth` 的含义是 **flange_from_depth**。
- `p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`，即
  `T_base_depth = T_base_flange * T_flange_depth`（由 `composeBaseFromDepth()` 单一实现）。
- **禁止**默认对手眼矩阵求逆；**禁止**增加固定 Z 轴翻转、镜像或 180° 旋转补丁。方向不正确时必须回到标定文件、
  Start/End 位姿或真实安装方向查因，不得在代码里打补丁。
- 控制器位姿顺序 `[X,Y,Z,A,B,C]`，`A=Rx`、`B=Ry`、`C=Rz`，旋转约定 `Rz(C)*Ry(B)*Rx(A)`，长度 mm、角度 deg。
- `DepthPointLayout::LineProfileXz`：输入 Y 只用于 `ratio = input.y / signedTravel`；实际参与刚体变换的相机点
  必须是 `[X,0,Z]`（`lineProfileCameraPoint()`）；Start/End 决定扫描运动与统一方向，`signedTravel` 保留符号
  （负行程是合法反向扫描）。
- `DepthPointLayout::FullXyz`：保留完整 XYZ，不清零任何坐标，也不要求扫描行程。
- 点和法向使用同一个刚体变换；法向只做旋转，**不做平移**。

接口/测试：`loadHandEyeCalibration`、`robotPoseToMatrix`、`matrixToRobotPose`、`interpolateRobotTransform`、
`computeScanTravel`、`composeBaseFromDepth`、`lineProfileCameraPoint`、`lineProfileScanRatio`、
`transformPointToRobotBase`、`transformLineScanToRobotBase`；回归测试 `handeye_transform_tests`，
跨路径一致性测试在 `temp_workpiece_interface_tests`（JSON/临时工件入口 vs 多帧入口逐点比对）。

最后核对日期：2026-09-24。

## 变更记录

### 2026-09-24（手眼标定统一与点云倒转修复）

- 需求：以“打开扫描 JSON”路径为手眼标定黄金基准，统一离线 JSON、PLY、多帧和拼接路径的
  camera→robot_base 坐标转换，消除因重复实现或 LineProfileXz 语义不一致造成的点云上下/法向反转风险。
- 根因（两条，都不是矩阵方向问题）：
  1. 坐标转换存在第二套实现：`apps/pointcloudstitch/regressionrunner.cpp` 自带 `RTmatDepth2robot` XML 解析和
     `poseMatrix()`；`apps/pointcloudview/pointcloudprocessor.cpp` 自带 `validateRigidTransform()`、
     `interpolateRigidTransform()`、`RigidTransformLut`、`scanProgress()`，并在点云循环里内联
     `baseFromFlange * flangeFromDepth` 与法向旋转。
  2. LineProfileXz 语义不一致：旧合并路径把输入 Y 当作相机空间坐标保留，并按 `VertexOrder`/`LocalX/Y/Z`
     进度轴与 `EmbeddedMotion`/`Auto` 启发式决定位姿，与 JSON 黄金路径的 `y/signedTravel` + `[X,0,Z]`
     语义不同，可能在同一份 PLY 上得到不同的上下/法向结果。
- 修复：本模块成为唯一数学实现，并新增公共入口集中校验与诊断：
  - `composeBaseFromDepth()` 固定 `T_base_depth = T_base_flange * T_flange_depth`；
  - `computeScanTravel()` 统一由 Start/End 平移差计算主导轴与**带符号**行程；
  - `lineProfileCameraPoint()` / `lineProfileScanRatio()` 显式固化 LineProfileXz 契约；
  - `transformPointToRobotBase()` 变为“校验 + 旋转（法向）/旋转加平移（点）”的单一入口，并新增可选错误输出；
  - `RobotCloudStatus` 状态枚举和 `robotCloudStatusCode()`/`robotCloudStatusText()` 让以下失败可区分：
    手眼标定矩阵无效、Start 位姿无效、End 位姿无效、Start/End 没有有效行程、输入点云为空、
    LineProfileXz 扫描进度超出允许范围、输入点全部无效或不符合 LineProfileXz 契约、取消。
- 调用方清理：`apps/pointcloudstitch/regressionrunner.cpp` 删除重复 XML 解析与 `poseMatrix()`，改用
  `loadHandEyeCalibration()` / `robotPoseToMatrix()`（命令行、报告 JSON 与输出文件保持兼容）；
  `apps/pointcloudview/pointcloudprocessor.cpp` 删除重复校验、插值、进度启发式与内联矩阵乘法，改为调用本模块
  `transformLineScanToRobotBase()` 完成整帧转换，ICP、体素和拼接算法未改动，合并缓存版本号提升到 9。
- 行为/API：已发布的矩阵方向、位姿顺序、旋转约定、单位与既有错误文案保持不变；新增符号均为增量
  （`ScanTravelInfo`、`RobotCloudStatus`、`composeBaseFromDepth`、`computeScanTravel`、`lineProfileCameraPoint`、
  `lineProfileScanRatio`、`robotCloudStatusCode`、`robotCloudStatusText`），`transformPointToRobotBase()` 追加可选的
  `QString *error` 尾参。
- 验证：`handeye_transform_tests` 覆盖乘法方向、黄金链路平移/旋转、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、
  正负行程与反向扫描、FullXyz、法向同旋转不平移、位姿约定与往返、非有限/非正交/非单位/行列式非 +1 的拒绝、
  无行程/空点云/全部无效/超范围/取消和状态码；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口
  逐点一致性比对。源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，4 项直接相关测试 `4/4`，
  `pointcloudview` Debug/Release 启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史
  baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio
  release redistributable 发出警告，便携部署未验收；真实 XML 物理方向、机器人 A/B/C 现场定义、相机安装方向和
  实时相机预览仍未完成现场验证。

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
