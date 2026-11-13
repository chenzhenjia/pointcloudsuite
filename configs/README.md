# 配置说明

v0.5，2026-10-15。pointcloudview.config.json为配置模板；字段以当前infrastructure解析为准。本次不改JSON配置。

配置优先级为pointcloudview --config绝对路径、PCV_CONFIG_FILE、程序目录config/pointcloudview.json、Qt用户配置目录。首次无配置生成用户模板；字段独立校验，非法项回内置默认并记启动日志。JSON配置不支持注释。

运行数据、cache/logs/exports均放源码树外。CLI业务--data-dir有独立优先级，不能和GUI配置混用。[数据契约](../docs/contracts/data_contracts_v0.5.md) · [架构](../docs/architecture/software_architecture_v0.5.md)。
