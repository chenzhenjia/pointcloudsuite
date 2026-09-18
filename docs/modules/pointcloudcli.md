# pointcloudcli：点云读取与临时工件命令行业务入口

状态：已实现 `inspect ply`、`read ply` 和 `temp-workpiece run` 业务入口。target 为 `pointcloudcli`，仅在 `PCV_BUILD_CLI=ON` 时生成；默认 `OFF`，不改变 `pointcloudview` 或 `pointcloudstitch` 的构建行为。

该 target 使用 C++17 和 Qt Core/Gui/Concurrent，规范依赖为 `pcv_m10_pointcloud_io`、`pcv_m50_coordinate_conversion` 和 `pcv_m60_geometry_workpiece`；不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target。CLI-only 组合仍可独立配置。

## 命令契约

| 调用 | 行为 | 退出码 |
|---|---|---:|
| `pointcloudcli --help` | 输出用法和支持选项 | `0` |
| `pointcloudcli --version` | 输出 `pointcloudcli <PROJECT_VERSION>` | `0` |
| `pointcloudcli` | 输出缺少命令行选项错误和用法 | `2` |
| `pointcloudcli --unknown` | 输出未知选项错误和用法 | `2` |
| `pointcloudcli inspect ply <file.ply> [--json]` | 检查 PLY Header | `0` 或 `10` |
| `pointcloudcli read ply <file.ply> [--json]` | 读取 PLY 并输出摘要 | `0` 或 `10` |
| `pointcloudcli temp-workpiece run <job.json> [--data-dir <path>] [--allow-overwrite]` | 执行临时工件四件套 | `0` 或阶段错误码 |

业务流程为扫描 JSON/PLY/XML 校验、camera 到 robot_base 转换、第一组三点平面拟合、第二组三点一致性验证、自动 WObj1、ROI/Mask 和四件套原子输出。Job JSON 所有相对路径相对于 Job 文件目录，索引空间为转换后的 `robot_base` 点云。

`inspect ply` 只读取 Header；`read ply` 调用 `pcv::detail::io::readPly`，输出 ASCII、binary little-endian/big-endian 格式、声明/有效点数、非有限 XYZ 数、Bounds 和 intensity 摘要，且不写出新文件。带 `--json` 时成功摘要写入 stdout，PLY/业务阶段失败通过 JSON 写入 stderr；基础用法错误仍输出错误文本和用法。版本号由 CMake `PROJECT_VERSION` 在编译时注入。当前不实现实时相机采集、CLI 拼接、PLY 转换写出、干涉平面、质量报告、安装程序或签名发布；`registration_diagnostic` 仍为独立拼接诊断工具。

`temp-workpiece run` 的运行数据目录按 `--data-dir`、环境变量 `PCV_DATA_DIR`、exe 同级 `data`、`%LOCALAPPDATA%\PointCloudSuite\data` 顺序选择首个可写目录，并通过 `pcv::runtime::configureDataDirectory()` 配置缓存位置。`--data-dir` 对 `inspect ply` 或 `read ply` 属于契约错误（退出码 `2`）。

Release 便携目录可由 `scripts\build_windows.ps1 -CliOnly -Config Release -Deploy` 生成；部署目录包含 `pointcloudcli.exe`、Qt/VC 运行库、`data` 和 `README.txt`，并通过清空 PATH 的子进程回归 15 项 CLI 契约。目标目录必须在源码树外且不能是盘符根目录；已有目录会在安全检查后清空重建。`PCV_BUILD_CLI` 默认仍为 `OFF`。

## 测试

`PCV_BUILD_CLI=ON` 且 `PCV_BUILD_TESTS=ON` 时注册 15 项参数、读取和业务测试。读取测试覆盖 ASCII、binary little-endian/big-endian Header、缺少文件、无效 Header、完整读取摘要、Bounds 和 intensity；业务测试覆盖四件套生成、ROI 模式、覆盖保护和路径穿越拒绝。PLY/业务阶段失败通过稳定 JSON 和退出码契约返回，基础参数错误按用法契约输出。

## 变更记录

### 2026-09-03

- 新增独立 `pointcloudcli` target 和 `PCV_BUILD_CLI` 开关；默认关闭，GUI target 和 UI 源码未修改。
- 建立 `--help`、`--version`、空参数和未知参数的稳定命令行契约及 CTest 覆盖。
- 增加 Job JSON v1、临时工件业务编排、公共 WObj1/ROI/Mask 几何接口和取消处理。

### 2026-09-13

- 新增 `inspect ply`、`read ply`、`--data-dir` 可写目录回退和 15 项 CLI CTest；补充 `-CliOnly` Release 便携部署。

最后核对日期：2026-09-17。本轮文档同步未运行 CTest。
