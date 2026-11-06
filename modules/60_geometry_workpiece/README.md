# 60_geometry_workpiece

v0.5，核对2026-10-15。职责：平面/单击/边缘/ROI/WObj1、工件/扫描/纹理。target为pcv_m60_geometry_workpiece；当前接口、依赖、默认行为、测试归属与风险只维护在[模块文档](../../docs/modules/60_geometry_workpiece.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。

2026-11-05：PlaneSelectionIndexResult增加budgetAcquireMs计时，既有预算获取/索引数学不变；[加载计时v1](../../docs/development/scan_geometry_load_performance_v1.md)，构建测试由用户执行。

2026-11-05 性能v2：法向准备批次可指定，默认4096，新GUI请求65536，预算不足4096/串行回退；新增调度及每种子选面计时，拟合/索引数学不变。公共头变动须全量重编，测试源码交付待用户执行。[说明](../../docs/development/scan_geometry_load_performance_v2.md)。

2026-11-05 性能v3：可选瞬时法向邻居缓存（旧调用方默认关闭）、任务内连通workspace及完整面片稠密网格预算回退；补各阶段耗时，不改拟合/跨带规则或输出schema，公共DTO须全量重编。测试待用户执行。[说明](../../docs/development/scan_geometry_load_performance_v3.md)。

2026-11-05：掩码结果补有效double间距/原点，选项可固定网格原点（默认关闭），供结果区域修剪稳定重建；公共头须全量重编。[开发说明](../../docs/development/scan_geometry_result_regions_v1.md)。未运行验证。


## STEP 模板与自动匹配 v1

2026-11-05：新增多平面STEP角色模板、完整特征/区域及伴随包事务读写。 [开发与验收](../../docs/development/step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
