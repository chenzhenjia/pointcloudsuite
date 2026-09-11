# PointCloudSuite

PointCloudSuite 是基于 Qt 6、C++17 和 CMake 的工业点云处理项目，提供点云查看、平面处理、多帧拼接、配准、坐标转换、成套输出、诊断工具和自动化测试。

## 当前基线

当前项目版本为 v0.3。源码、CMake 和测试是事实来源；历史版本文档仅用于追溯。

## 项目组成

- `pointcloudview`：点云读取、渲染、点选、平面拟合、ROI/模板和输出。
- `pointcloudstitch`：线扫点云坐标转换、多帧配准、接缝融合和结果编排。
- `pointcloudcli`：独立命令行工具；当前提供 `temp-workpiece run <job.json>` 临时工件四件套业务闭环，以及 `--help`、`--version`。
- 相机（海康 Mv3dLp）实验取流能力：已并入 `modules/10_pointcloud_read`（target `pcv_m10_pointcloud_read`），由 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制；仍属实验，需实机验收。
- `modules/10_pointcloud_read` 至 `modules/80_plane_output`：规范共享模块。
- `registration_diagnostic`：通过 `pcv::interface::stitchRawLineProfiles` 执行统一拼接流水线的诊断工具，支持通用多帧输入、位姿匹配和 `inspect` PLY header 检查。
- `tests/`：模块和接口回归测试。
- `docs/`：需求、架构、契约、开发和用户文档。

## 目录结构

```text
apps/          两个 Qt 桌面应用和 pointcloudcli
modules/       v0.3 规范模块
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

业务命令示例：

```powershell
pointcloudcli temp-workpiece run <job.json>
pointcloudcli temp-workpiece run <job.json> --allow-overwrite
```

Job JSON 使用 `pointcloudcli-temp-workpiece-job-v1`。CLI 会校验扫描 JSON、PLY/XML、三点平面和第二组三点一致性，自动建立 WObj1，并原子生成 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png` 和 `temp_workpiece_info.json`。实时相机、拼接、干涉平面和质量报告暂不属于本期 CLI。

根 CMake 默认启用 `PCV_BUILD_EXPERIMENTAL_CAMERA`；`scripts/build_windows.ps1` 默认将该开关传为 `OFF`，需要实验相机时请显式添加 `-ExperimentalCamera`。

## 代码边界

共享算法位于模块 `src/`，公共头位于模块 `include/`。应用可以依赖共享模块，共享模块不得依赖应用。Qt Designer 文件位于 `apps/pointcloudview/ui/` 和 `apps/pointcloudstitch/ui/`。

详细模块职责见 [v0.3 需求文档](docs/requirements/pointcloudview_v0.3.md) 和 [模块文档](docs/modules/)。

最后核对日期：2026-09-07。
