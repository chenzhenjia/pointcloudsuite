# PointCloudSuite

PointCloudSuite 是基于 Qt 6、C++17 和 CMake 的工业点云处理项目，提供点云查看、平面处理、多帧拼接、配准、坐标转换、成套输出、诊断工具和自动化测试。

## 当前基线

当前项目进入 v0.4 模块重整阶段。模块边界和 UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准；字段、JSON/PLY 和坐标契约继续以 v0.3 需求文档为准。源码、CMake 和测试是事实来源；历史版本文档仅用于追溯。

## 项目组成

- `pointcloudview`：点云读取、渲染、点选、平面拟合、ROI/模板和输出。
- `pointcloudstitch`：线扫点云坐标转换、多帧配准、接缝融合和结果编排。
- `pointcloudcli`：独立命令行工具；当前提供 `inspect ply`、`read ply`、`temp-workpiece run <job.json>`，以及 `--help`、`--version`。
- `intensity_viewer`：实验性亮度图预览与手动 PNG 保存工具，仅在 `PCV_BUILD_EXPERIMENTAL_CAMERA=ON` 且至少构建一个桌面应用时生成。
- 相机（海康 Mv3dLp）实验取流能力：已并入 `modules/10_pointcloud_io`（target `pcv_m10_pointcloud_io`），由 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制；仍属实验，需实机验收。
- 规范共享模块为 `10_pointcloud_io`、`20_pointcloud_render`、`30_pointcloud_stitch`、`40_pointcloud_matching`（规划占位）、`50_coordinate_conversion` 和 `60_geometry_workpiece`。
- `40_pointcloud_matching` 当前只保留兼容聚合入口，尚未实现基准模板、移动判定、机器人执行或正式输出闭环。
- `registration_diagnostic`：通过 30 的统一拼接接口执行诊断；新调用优先 `StitchingRequest` / `stitch()`，旧 `stitchRawLineProfiles` 仅兼容。支持通用多帧输入、位姿匹配和 `inspect` PLY header 检查。
- `tests/`：模块和接口回归测试。
- `docs/`：需求、架构、契约、开发和用户文档。

## 目录结构

```text
apps/          Qt 桌面应用、实验性 intensity_viewer 和 pointcloudcli
modules/       v0.4 规范模块与兼容入口
src/           迁移期基础实现和兼容聚合 target
include/pcv/   公共头和 forwarding header
tests/          CTest 测试
tools/          可选诊断工具
configs/       配置模板
examples/      非生产示例
scripts/       Windows 构建和测试脚本
docs/           项目文档
```

## 环境要求

- Windows 10/11
- MSVC x64，支持 C++17
- CMake 3.19+
- Qt 6.5+，包含 Core、Gui、Widgets、Concurrent、OpenGL 和 OpenGLWidgets
- 源码、构建、输入和输出路径支持 Unicode；构建目录建议放在源码树之外

项目不依赖 Python、Open3D、PCL、VTK、CUDA、数据库或云服务。

## 构建与测试

在 Developer PowerShell 中执行：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

Debug 预设会启用测试和诊断工具：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

CLI 默认不构建。以下配置会关闭两个 GUI 应用并只构建 `pointcloudcli`；该 CLI-only 组合不配置 Qt Widgets/OpenGL：

```powershell
cmake -S . -B <CliBuildDir> `
  -DPCV_BUILD_POINTCLOUDVIEW=OFF `
  -DPCV_BUILD_POINTCLOUDSTITCH=OFF `
  -DPCV_BUILD_CLI=ON `
  -DPCV_BUILD_TESTS=ON
cmake --build <CliBuildDir> --config Debug --target pointcloudcli
ctest --test-dir <CliBuildDir> -C Debug -R "cli_" --output-on-failure
```

`PCV_BUILD_CLI=OFF` 是默认值，因此既有 GUI 构建和产物不变。

也可以使用脚本自动探测 MSVC、CMake 和 Qt，并生成 Release 便携目录：

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

`-Deploy` 仅支持 `-CliOnly -Config Release`，会将 `pointcloudcli.exe`、Qt/VC 运行库、`data` 和 `README.txt` 放入目标目录，并在清空 PATH 的子进程中运行 15 项 CLI 契约测试。目标目录必须在源码树外且不能是盘符根目录；已有目录会在安全检查后清空重建。也可双击 `scripts\build_pointcloudcli_release.bat` 使用默认构建目录。便携目录可整体复制到另一台 Windows x64 机器；业务输入文件需随任务目录另行提供。

业务命令示例：

```powershell
pointcloudcli temp-workpiece run <job.json>
pointcloudcli temp-workpiece run <job.json> --allow-overwrite
pointcloudcli inspect ply input.ply --json
pointcloudcli read ply input.ply --json
pointcloudcli temp-workpiece run job.json --data-dir .\data
```

Job JSON 使用 `pointcloudcli-temp-workpiece-job-v1`。CLI 会校验扫描 JSON、PLY/XML、三点平面和第二组三点一致性，自动建立 WObj1，并原子生成 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png` 和 `temp_workpiece_info.json`。实时相机、拼接、干涉平面和质量报告暂不属于本期 CLI。

`inspect ply` 只读取 PLY Header，`read ply` 完整读取点云并输出格式、点数、Bounds 和 intensity 摘要；两个命令默认不写文件。Python 可通过 `subprocess.run` 调用并解析 JSON：

```python
import json
import subprocess

result = subprocess.run(
    ["pointcloudcli.exe", "read", "ply", "input.ply", "--json"],
    capture_output=True, text=True, check=False)
if result.returncode != 0:
    raise RuntimeError(json.loads(result.stderr)["message"])
print(json.loads(result.stdout)["loaded_points"])
```

根 CMake 默认启用 `PCV_BUILD_EXPERIMENTAL_CAMERA`；`scripts/build_windows.ps1` 默认将该开关传为 `OFF`，需要实验相机时请显式添加 `-ExperimentalCamera`。

## 代码边界

共享算法位于模块 `src/`，公共头位于模块 `include/`。应用可以依赖共享模块，共享模块不得依赖应用。Qt Designer 文件位于 `apps/pointcloudview/ui/`、`apps/pointcloudstitch/ui/` 和实验性 `apps/intensity_viewer/ui/`。

详细模块职责见 [v0.4 模块与 UI 交接文档](docs/architecture/module_ui_integration_v0.4.md)、[v0.3 数据契约](docs/requirements/pointcloudview_v0.3.md) 和 [模块文档](docs/modules/)。

最后核对日期：2026-09-17。
