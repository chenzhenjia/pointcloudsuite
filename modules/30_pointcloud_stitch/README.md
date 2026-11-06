# 30_pointcloud_stitch

v0.5，核对2026-10-15。职责：整帧/所选平面拼接与诊断。target为pcv_m30_pointcloud_stitch；当前接口、依赖、默认行为、测试归属与风险只维护在[模块文档](../../docs/modules/30_pointcloud_stitch.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。
