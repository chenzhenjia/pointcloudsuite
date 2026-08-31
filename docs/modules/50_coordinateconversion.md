# 50_coordinateconversion：坐标转换

状态：已实现。规范实现位于 `modules/50_coordinateconversion/{include,src}`，模块 target 为 `pcv_m50_coordinateconversion`。

契约：`camera -> robot_base`，位姿 `[X,Y,Z,A,B,C]`，旋转约定 `Rz(C)*Ry(B)*Rx(A)`；验证刚体矩阵、插值、取消和源索引。

接口/测试：`loadHandEyeCalibration`、`transformLineScanToRobotBase`；`handeye_transform_tests`。

最后核对日期：2026-08-31。

## 变更记录

### 2026-08-31

- 建立独立模块映射和兼容入口；暂不移动共享实现。
- M2/M3：手眼标定和线扫坐标转换源码已迁移到 `modules/50_coordinateconversion`，旧头文件通过 forwarding header 兼容。
