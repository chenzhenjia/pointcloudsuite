# 依赖与构建环境

## 必需环境

- Windows 10 或 Windows 11
- CMake 3.19 或更高版本
- 支持 C++17 的 MSVC x64
- Qt 6.5 或更高版本，需提供 Core、Gui、Widgets、Concurrent、OpenGL 和 OpenGLWidgets

## 构建方式

推荐使用仓库脚本：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <BuildDir> -CMakePath <cmake.exe>
```

也可以在 Qt Creator 中打开根目录 `CMakeLists.txt`，由 Kit 提供 Qt 路径。命令行环境可通过 `CMAKE_PREFIX_PATH`、`Qt6_DIR` 或 `-QtDir` 指定 Qt。

## 运行时部署

Windows 构建会使用 Qt 的 `windeployqt` 部署运行库和插件。构建、缓存、日志、导出和运行数据应放在源码树之外。

## 不使用的外部依赖

项目不要求 Python、PCL、Open3D、VTK、CUDA、数据库或云服务。

最后核对日期：2026-08-31。
