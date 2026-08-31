# 30_pointcloudstitch：点云拼接

状态：部分实现。当前实现位于 `apps/pointcloudstitch`，负责多帧流程编排、结果管理和接缝融合调用；配准算法归属 `40_pointcloudregistration`。

### 2026-08-31（M5.1 seam pipeline）

- 移除 `seamEnabled` 的“暂时禁用”入口占位判断，使 `stitchRawLineProfiles` 可以调用共享 seam fusion。
- 无真实投影重叠时允许 fail-closed 保留完整点云并返回诊断；真实重叠但无可用互相对应点时仍返回 `PCV_STITCH_001`，禁止生成正式输出。
- 验证：接口错误安全测试覆盖启用 seam 时的非法 PLY 输入和正式输出不被替换。
- 应用 seam 适配器已改用 `pcv_m40_pointcloudregistration` 的共享结果类型别名，保留旧包装入口但移除第二套数据模型。

接口/测试：`stitching_interface`、`applyTrajectorySeamFusion`；`stitching_interface_tests`、`seam_fusion_tests`。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m30_pointcloudstitch` 兼容入口；保留现有应用流程。
- M3/M4：`stitching_interface` 已迁移到模块源码，target 负责流程编排并依赖配准、坐标转换和读写模块。
- M5.1：应用 seam fusion 入口已增加 `ok/error/cancelled` 状态并在正式输出前 fail-closed；GUI 仍保留应用数据模型适配，待公共拼接类型完全收敛后删除适配层。
