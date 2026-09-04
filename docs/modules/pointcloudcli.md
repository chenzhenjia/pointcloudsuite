# pointcloudcli：命令行入口骨架

状态：已实现构建与参数入口骨架。target 为 `pointcloudcli`，仅在 `PCV_BUILD_CLI=ON` 时生成；默认 `OFF`，不改变 `pointcloudview` 或 `pointcloudstitch` 的构建行为。

该 target 仅使用 C++17 标准库，不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target。关闭两个 GUI 应用的 CLI-only 组合也不会配置 Qt 或共享模块。

## 命令契约

| 调用 | 行为 | 退出码 |
|---|---|---:|
| `pointcloudcli --help` | 输出用法和支持选项 | `0` |
| `pointcloudcli --version` | 输出 `pointcloudcli <PROJECT_VERSION>` | `0` |
| `pointcloudcli` | 输出缺少命令行选项错误和用法 | `2` |
| `pointcloudcli --unknown` | 输出未知选项错误和用法 | `2` |

版本号由 CMake `PROJECT_VERSION` 在编译时注入。当前不实现点云读取、配准、拼接、临时工件 JSON、平面拟合、文件输出、Qt 事件循环或 GUI 状态。

## 测试

`PCV_BUILD_CLI=ON` 且 `PCV_BUILD_TESTS=ON` 时注册：`cli_help`、`cli_version`、`cli_empty`、`cli_unknown`。测试通过 CMake 进程包装器校验进程启动、stdout/stderr、精确退出码及版本输出。

## 变更记录

### 2026-09-03

- 新增独立 `pointcloudcli` target 和 `PCV_BUILD_CLI` 开关；默认关闭，GUI target 和 UI 源码未修改。
- 建立 `--help`、`--version`、空参数和未知参数的稳定命令行契约及 CTest 覆盖。

最后核对日期：2026-09-04。
