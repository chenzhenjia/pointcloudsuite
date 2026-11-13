# 10_pointcloud_io

v0.5，核对2026-10-15。职责：IO、缓存、预处理、事务及实验相机。target为pcv_m10_pointcloud_io；当前接口、依赖、默认行为、测试归属与风险只维护在[模块文档](../../docs/modules/10_pointcloud_io.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。

2026-11-05：缓存读取路径补头读取计时，缓存命中仍保留严格源指纹；冷读headerElapsedMs含两次实际头读取，不改缓存契约。[说明](../../docs/development/scan_geometry_load_performance_v1.md)，未运行构建测试。
