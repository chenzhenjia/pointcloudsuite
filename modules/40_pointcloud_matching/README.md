# 40_pointcloud_matching

v0.5，更新2026-11-05。`pcv_m40_pointcloud_matching` 为 STATIC，增加基于平面与边界的模板任意位姿匹配 v1，PUBLIC 保留30/60聚合依赖，新增50刚体估计依赖。机器人移动判定及控制业务仍未实现。[模块文档](../../docs/modules/40_pointcloud_matching.md) · [本轮说明](../../docs/development/step_template_matching_v1.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。
