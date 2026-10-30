# 50_coordinate_conversion：唯一坐标数学

2026-10-30 FullXyz深度栅格行进度：`CloudTransformOptions::useFullXyzRasterRowProgress` 对 `FullXyz` 按 `rasterSourceIndices[i] / rasterWidth / (rasterHeight-1)` 计算每点扫描比例，保留XYZ和稀疏源像素映射；校验尺寸、映射长度、严格递增与范围。正向/反向Start-End共用同一栅格行比例；旧 `LineProfileXz` 和默认FullXyz行为不变。用于 `scan_geometry_demo_v1` SDK点云按行位姿变换。[实现与验证状态](../development/scan_geometry_demo_v1.md#2026-10-30-sdk深度缓存与按需物化)。

2026-10-16 SDK整图采集v2继续复用既有行序进度；不同扫描可有不同height，每帧独立row/(height-1)，无效点过滤后仍用原始行号，不使用相机Y。本轮50数学/API不改，匀速/首末曝光对应为现场前提。[当前说明](../development/camera_capture_sdk_frame_v2.md)。

2026-10-16 连续扫描行序进度：CloudTransformOptions尾部追加useRasterRowProgress、rasterWidth/Height、rasterSourceIndices，显式开启按row/(height-1)插值，刚体输入仍[X,0,Z]，输出保留原栅格来源。旧默认Y/signedTravel不变，需现场核对均匀行程及首末对应。[实现与验收](../development/camera_capture_pose_stitch_v1.md)。

版本 v0.5，核对日期 2026-10-15。target pcv_m50_coordinate_conversion，依赖 pcv_core、Qt Core/Gui。头 pcv/registration/handeye_transform.h，实际 namespace pointcloud；未来 pcv::coordinates 门面尚未全部迁移。[数据契约](../contracts/data_contracts_v0.5.md) · [日志](../changelog/v0.5.md)。

## 唯一坐标链

RTmatDepth2robot 表示 T_flange_depth（HandEyeCalibration::flangeFromDepth）。p_robot_base=T_base_flange(Start/End 插值)*T_flange_depth*p_camera，composeBaseFromDepth 统一组合。禁止默认求逆或固定 Z 翻转/镜像/180° 补丁；方向问题需核对标定、机器人位姿和真实安装。

控制器 [X,Y,Z,A,B,C]，A=Rx/B=Ry/C=Rz，Rz(C)*Ry(B)*Rx(A)，mm/deg。LineProfileXz 相机点 [X,0,Z]，Y 只用于 y/signedTravel，负行程合法；FullXyz 保留 XYZ 且不要求线扫行程。点和法向使用同一旋转，法向不平移。

## 公共入口与调用边界

loadHandEyeCalibration、robotPoseToMatrix、matrixToRobotPose、interpolateRobotTransform、computeScanTravel、composeBaseFromDepth、lineProfileCameraPoint、lineProfileScanRatio、transformPointToRobotBase、transformLineScanToRobotBase；RobotCloudStatus 及状态 code/text 保持既有错误/取消语义。

30/60、view、stitch 和回放工具必须复用本模块，不保留另一份 XML/SLERP/矩阵乘法实现。所选平面请求已经在 robot_base，30 不再次转换；原始坐标纹理实验不使用手眼。头路径 registration 不代表算法仍归 40。

## 兼容、验证与风险

旧 pcv_m50_coordinateconversion/头保留转发。handeye_transform_tests 覆盖矩阵方向、XYZ/线扫、负行程、法向、刚体验证、异常和取消；temp_workpiece_interface_tests 覆盖 JSON/多帧逐点一致性。历史真实样本 4,389,670 点最大差/RMS=0 仅属于当次输入与程序，见[历史归档](../archive/history_pre_v0.5.md)。

本轮不运行动态测试。真实 XML 的物理方向、控制器 ABC 定义、相机安装与机器人安全仍需现场验证。

批量完整转换优化（2026-10-16）：CloudTransformOptions尾部追加collectSamplePoints=true、parallelEnabled=false、maxParallelTasks=4和可选processingBudget。复用同一逐点计算体，连续区间私有缓冲、固定窗口和原序归并；预算不足直接串行追加，批量入口关闭未使用samplePoints。过滤/ratio/查表/矩阵/法向精度与计算顺序保持；串行/1/2/4精确对照和取消回归源码已补但未执行，不能宣称已证明等价或提速。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)。


2026-10-16 整合v2：RobotCloudResult尾部新增mergeMs与actualWorkerCount仅诊断；并行窗口先join全部future再原序归并，原逐点数学和默认选项不变。新增诊断不写manifest。串行/1/2/4/零预算回归已补未运行，见[完整处理v3](../development/full_resolution_processing_acceleration_v3.md)。
