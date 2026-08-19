# PointCloudSuite

PointCloudSuite 是基于 Qt 6 和 C++17 的完整点云处理项目，包含点云查看处理
主程序、点云拼接程序、共享算法源码、诊断工具和自动化测试。本仓库以源码工程
形式交付，不是 SDK 安装包。

## 项目组成

- `pointcloudview`：点云读取、显示、处理和分析主程序。
- `pointcloudstitch`：手眼转换、多帧配准、拼接和融合程序。
- 共享模块：PLY 读取与缓存、降采样、统计滤波、运行目录管理。
- 辅助能力：注册诊断工具、单元测试、构建脚本和产品文档。

## 目录结构

```text
PointCloudSuite/
|-- apps/                  # 两个桌面程序的入口、界面和专属业务代码
|   |-- pointcloudview/
|   `-- pointcloudstitch/
|-- src/                   # 可复用算法和基础设施实现
|-- include/pcv/           # src 中共享模块对应的项目头文件
|-- tests/                 # 单元测试
|-- tools/                 # 可选诊断工具
|-- docs/                  # 架构、需求、开发和用户文档
|-- examples/              # 示例配置与非生产样例
|-- configs/               # 项目配置
|-- scripts/               # Windows 构建和测试脚本
|-- cmake/                 # 项目级 CMake 辅助文件
|-- CMakeLists.txt
`-- CMakePresets.json
```

各目录中的 `README.md` 说明该部分的职责和维护边界。

## 环境要求

- Windows 10/11
- CMake 3.19 或更高版本
- 支持 C++17 的 MSVC
- Qt 6.5 或更高版本，所需组件为 Core、Gui、Widgets、Concurrent、
  OpenGL 和 OpenGLWidgets

源码路径和构建路径必须只包含 ASCII 字符。

## 构建

在 Visual Studio Developer PowerShell 中执行：

```powershell
.\scripts\build_windows.ps1 -Config Release
```

构建调试版本并启用测试：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
```

也可以使用根目录的 `CMakePresets.json`。构建输出应放在源码目录之外，
运行产生的缓存、日志和导出文件位于操作系统应用数据目录。

## 源码边界

- 通用算法放入 `src/`，对应头文件放入 `include/pcv/`。
- 界面、程序入口和仅被单个程序使用的流程放入对应的 `apps/<name>/`。
- `apps` 可以依赖共享模块，共享模块不能反向依赖 `apps`。
- 两个程序保持独立可执行目标，不复制共享算法实现。

详细架构见 [docs/architecture/overview.md](docs/architecture/overview.md)，
各程序需求见 [docs/requirements](docs/requirements)。
