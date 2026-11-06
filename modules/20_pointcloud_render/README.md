# 20_pointcloud_render

v0.5，核对2026-10-15。职责：画布、OpenGL、拾取与灰度覆盖。target为pcv_m20_pointcloud_render；当前接口、依赖、默认行为、测试归属与风险只维护在[模块文档](../../docs/modules/20_pointcloud_render.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。

2026-11-05：可选 displayTiming CPU分段反馈及独立displayCloudRevision，旧调用方默认关闭。无glFinish，不改变缓冲策略；新GUI首显按frameSwapped核对。测试与风险见 [加载计时说明](../../docs/development/scan_geometry_load_performance_v1.md)，本轮未运行构建测试。

2026-11-05 性能v2：递增展示映射使用二分反查，乱序保留QHash，mapping_strategy随可选发布计时输出；旧默认外观/GPU拾取不变。测试源码已补，未构建/运行。[开发说明](../../docs/development/scan_geometry_load_performance_v2.md)。


## STEP 模板与自动匹配 v1

2026-11-05：CAD实例级角色颜色/背景及清空接口，旧默认兼容。 [开发与验收](../../docs/development/step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
