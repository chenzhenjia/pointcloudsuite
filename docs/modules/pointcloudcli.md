# pointcloudcli：临时工件命令行业务入口

状态：已实现 `temp-workpiece run` 首期业务闭环。target 为 `pointcloudcli`，仅在 `PCV_BUILD_CLI=ON` 时生成；默认 `OFF`，不改变 `pointcloudview` 或 `pointcloudstitch` 的构建行为。

该 target 使用 C++17 和 Qt Core/Gui/Concurrent 访问已有业务模块，不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target。CLI-only 组合仍可独立配置。

## 命令契约

| 调用 | 行为 | 退出码 |
|---|---|---:|
| `pointcloudcli --help` | 输出用法和支持选项 | `0` |
| `pointcloudcli --version` | 输出 `pointcloudcli <PROJECT_VERSION>` | `0` |
| `pointcloudcli` | 输出缺少命令行选项错误和用法 | `2` |
| `pointcloudcli --unknown` | 输出未知选项错误和用法 | `2` |
| `pointcloudcli temp-workpiece run <job.json>` | 执行临时工件四件套 | `0` 或阶段错误码 |

业务流程为扫描 JSON/PLY/XML 校验、camera 到 robot_base 转换、第一组三点平面拟合、第二组三点一致性验证、自动 WObj1、ROI/Mask 和四件套原子输出。Job JSON 所有相对路径相对于 Job 文件目录，索引空间为转换后的 `robot_base` 点云。

版本号由 CMake `PROJECT_VERSION` 在编译时注入。当前不实现实时相机采集、拼接、干涉平面、质量报告、安装包或发布包配置；`registration_diagnostic` 仍为独立拼接诊断工具。

## 测试

`PCV_BUILD_CLI=ON` 且 `PCV_BUILD_TESTS=ON` 时注册 8 项测试：`cli_help`、`cli_version`、`cli_empty`、`cli_unknown`，以及 `cli_temp_workpiece_rectangle`、`cli_temp_workpiece_indices`、`cli_temp_workpiece_overwrite`、`cli_temp_workpiece_traversal`。测试通过 CMake 进程包装器校验进程启动、stdout/stderr、精确退出码、版本输出、四件套生成、ROI 模式、覆盖保护和路径穿越拒绝；业务阶段错误通过稳定 JSON 和退出码契约返回。

## 变更记录

### 2026-09-03

- 新增独立 `pointcloudcli` target 和 `PCV_BUILD_CLI` 开关；默认关闭，GUI target 和 UI 源码未修改。
- 建立 `--help`、`--version`、空参数和未知参数的稳定命令行契约及 CTest 覆盖。
- 增加 Job JSON v1、临时工件业务编排、公共 WObj1/ROI/Mask 几何接口和取消处理。

最后核对日期：2026-09-04。
