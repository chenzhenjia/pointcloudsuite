# PointCloudSuite

PointCloudSuite 是基于 Qt 6、C++17 和 CMake 的工业点云处理项目，提供点云查看、平面处理、多帧拼接、配准、坐标转换、成套输出、诊断工具和自动化测试。

## 当前基线

当前项目版本为 v0.3。源码、CMake 和测试是事实来源；历史版本文档仅用于追溯。

## 项目组成

- `pointcloudview`：点云读取、渲染、点选、平面拟合、ROI/模板和输出。
- `pointcloudstitch`：线扫点云坐标转换、多帧配准、接缝融合和结果编排。
- `modules/10_pointcloudread` 至 `modules/80_planeoutput`：规范共享模块。
- `registration_diagnostic`：通过 `pcv::interface::stitchRawLineProfiles` 执行统一拼接流水线的诊断工具。
- `tests/`：模块和接口回归测试。
- `docs/`：需求、架构、契约、开发和用户文档。

## 目录结构

```text
apps/          两个 Qt 桌面应用
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
- 源码和构建路径使用 ASCII

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

## 代码边界

共享算法位于模块 `src/`，公共头位于模块 `include/`。应用可以依赖共享模块，共享模块不得依赖应用。Qt Designer 文件位于 `apps/pointcloudview/ui/` 和 `apps/pointcloudstitch/ui/`。

详细模块职责见 [v0.3 需求文档](docs/requirements/pointcloudview_v0.3.md) 和 [模块文档](docs/modules/)。

最后核对日期：2026-08-31。
