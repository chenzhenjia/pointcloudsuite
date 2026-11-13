# 50 坐标与刚体数学

Target：`pcv_m50_coordinate_conversion`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：loadHandEyeCalibration、robotPoseToMatrix、interpolateRobotTransform、composeBaseFromDepth、transformLineScanToRobotBase、rigid_estimation。

位姿[X,Y,Z,A,B,C]，A=Rx/B=Ry/C=Rz，Rz(C)*Ry(B)*Rx(A)。T_base_depth=T_base_flange*T_flange_depth。LineProfileXz为[X,0,Z]，负行程合法，法向只旋转。禁止固定翻转/镜像补丁。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
