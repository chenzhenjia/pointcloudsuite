# 50_coordinate_conversion

v0.5，核对2026-10-15。职责：唯一手眼/位姿/坐标数学。target为pcv_m50_coordinate_conversion；当前接口、依赖、默认行为、测试归属与风险只维护在[模块文档](../../docs/modules/50_coordinate_conversion.md)。

[整体架构](../../docs/architecture/software_architecture_v0.5.md) · [数据契约](../../docs/contracts/data_contracts_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。共享模块不依赖apps；公共兼容头保留，无引用旧target已按授权清理，实际保留项见[清理清单](../../docs/development/application_target_cleanup_v1.md)。本轮仅静态检查，构建/CTest/GUI由用户执行，历史通过不代表当前重验。


## STEP 模板与自动匹配 v1

2026-11-05：模板定位新增Horn刚体估计、双平面对齐及6DoF约束秩，旧手眼数学不变。 [开发与验收](../../docs/development/step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
