# 配置说明

`pointcloudview.config.json` 是版本化配置模板。配置用于指定业务数据目录和点云处理默认参数。

配置查找顺序：

1. `pointcloudview --config <absolute path>`
2. 环境变量 `PCV_CONFIG_FILE=<absolute path>`
3. 程序目录下的 `config/pointcloudview.json`
4. Qt 用户配置目录

首次启动且没有配置时会生成用户配置模板。每个字段独立校验；缺失或非法字段回退到内置默认值，并写入启动日志。

默认数据目录由模板和程序默认值定义，运行数据应包含 `runtime_data`、`cache`、`logs` 和 `exports`，不得写入源码树。JSON 配置不支持注释。

最后核对日期：2026-08-31。
