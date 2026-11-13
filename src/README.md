# 共享基础设施与兼容源码入口

v0.5，2026-10-15。core/infrastructure定义点类型、配置和运行路径；IO/filtering/registration/interface/output旧target为INTERFACE，转发规范模块，不新增实现。

读取/过滤/writer归10，坐标数学归50，拼接归30，几何/工件归60；头路径不等于namespace，过滤实际为pcv::detail::filtering。算法放modules，不反向依赖apps。[架构](../docs/architecture/software_architecture_v0.5.md) · [兼容规则](../docs/architecture/module_ui_integration_v0.5.md)。
