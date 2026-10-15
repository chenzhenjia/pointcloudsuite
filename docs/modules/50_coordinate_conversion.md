# 50_coordinate_conversion：唯一坐标数学

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
