# PointCloudSuite

C++17 / Qt 6 点云处理项目。文档基线 **v0.5（2026-10-15）**：HEAD `75d3e3b` 加当前未提交/未跟踪能力；软件 CMake 版本仍为 1.0.0，发布与现场验收未完成。

## 从这里开始

- [需求与能力状态](docs/requirements/pointcloudview_v0.5.md)
- [软件整体架构](docs/architecture/software_architecture_v0.5.md)：五层、依赖与四条业务链。
- [数据契约](docs/contracts/data_contracts_v0.5.md)与[UI/模块协同](docs/architecture/module_ui_integration_v0.5.md)。
- [target.md](target.md)：目标、责任、优先级和简要更新。
- [文档总索引](docs/README.md)、[后续开发方案](docs/development/development_plan_v0.5.md)。

## 当前软件组成

桌面入口为 `scan_geometry_demo_v1`（扫描与几何选择GUI）和 `pointcloudview`（原工作台）。前者使用真实选面任务，后者保留点云查看、相机/RAW采集、整帧及所选平面拼接、临时工件/ROI、扫描规划、纹理和内存显示。CLI、诊断及转换工具保留，默认不构建。独立拼接程序、独立亮度查看器及旧模拟代码已清理，见[清理说明](docs/development/application_target_cleanup_v1.md)。

规范模块为 10 IO、20 渲染、30 拼接、50 坐标、60 几何/工件；40 是位置确认规划兼容聚合，70/80只保留兼容公共头，已无构建target。90/100尚未实现。整帧 PoseOverlapFine/2.5D 已回滚；所选平面是独立 warn_only 入口，不能混称。

## 目录与构建

```text
apps/       应用入口、共享拼接 UI、features/adapters
modules/    规范领域模块及兼容转发
include/    core/infrastructure 与兼容公共头
src/        core 与 infrastructure
tests/      模块、应用与契约测试
tools/      诊断工具
docs/       v0.5 规范、日志、历史与迁移清单
scripts/    Windows 构建/部署工具
```

Windows/MSVC x64、Qt 6.5+、CMake 3.19+；preset 格式另要求 CMake 3.25+。不依赖 Python/Open3D/PCL/VTK/CUDA 算法运行时。构建、缓存、日志、导出必须位于源码树外；推荐命令和构建选项风险见 [Windows 构建](docs/development/build-windows.md)。

## 验证与开发边界

本轮应用/target清理只做静态检查，构建、CTest及GUI验收由用户执行，命令见[清理说明](docs/development/application_target_cleanup_v1.md)。历史测试日志按日期集中在[版本记录](docs/changelog/v0.5.md)，不当作当前软件重验。真实样本/GUI、单击至完整高亮两秒、相机与机器人、便携部署均有待验收项。

共享模块不依赖 apps；坐标数学仅来自 50；新业务正式输出通过 10 ArtifactTransaction，旧 10 平面 writer 事务类收敛仍待办。UI 和模块所有权、临时 adapter、命名与兼容清理以协同规范和 [AGENTS.md](AGENTS.md)为准。没有提交、推送或上传。
