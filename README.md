# PointCloudSuite

C++17 / Qt 6 点云处理项目。文档基线 **v0.5（2026-10-15）**：HEAD `75d3e3b` 加当前未提交/未跟踪能力；软件 CMake 版本仍为 1.0.0，发布与现场验收未完成。

## 从这里开始

- [需求与能力状态](docs/requirements/pointcloudview_v0.5.md)
- [软件整体架构](docs/architecture/software_architecture_v0.5.md)：五层、依赖与四条业务链。
- [数据契约](docs/contracts/data_contracts_v0.5.md)与[UI/模块协同](docs/architecture/module_ui_integration_v0.5.md)。
- [target.md](target.md)：目标、责任、优先级和简要更新。
- [文档总索引](docs/README.md)、[后续开发方案](docs/development/development_plan_v0.5.md)。

## 当前软件组成

pointcloudview 提供点云查看、临时工件/ROI、扫描规划、纹理、所选平面拼接和进程内存显示。pointcloudstitch 与 view 共用应用层 pcv_stitching_ui，提供整帧配准/仅手眼转换。CLI 负责只读 PLY 摘要与临时工件；诊断工具独立；相机及 intensity_viewer 为实验能力。

规范模块为 10 IO、20 渲染、30 拼接、50 坐标、60 几何/工件；40 是位置确认规划兼容占位，70/80 仅转发。90/100 尚未实现。整帧 PoseOverlapFine/2.5D 已回滚；所选平面是独立 warn_only 入口，不能混称。

## 目录与构建

```text
apps/       应用入口、共享拼接 UI、features/adapters
modules/    规范领域模块及兼容转发
include/    core/infrastructure 与兼容公共头
src/        基础设施与兼容 target
tests/      模块、应用与契约测试
tools/      诊断工具
docs/       v0.5 规范、日志、历史与迁移清单
scripts/    Windows 构建/部署工具
```

Windows/MSVC x64、Qt 6.5+、CMake 3.19+；preset 格式另要求 CMake 3.25+。不依赖 Python/Open3D/PCL/VTK/CUDA 算法运行时。构建、缓存、日志、导出必须位于源码树外；推荐命令和构建选项风险见 [Windows 构建](docs/development/build-windows.md)。

## 验证与开发边界

本轮只文档静态审计，不运行构建、CTest、GUI 或性能测试。历史测试日志按日期集中在[版本记录](docs/changelog/v0.5.md)，不当作当前软件重验。真实样本/GUI、单击至完整高亮两秒、相机与机器人、便携部署均有待验收项。

共享模块不依赖 apps；坐标数学仅来自 50；新业务正式输出通过 10 ArtifactTransaction，旧 10 平面 writer 事务类收敛仍待办。UI 和模块所有权、临时 adapter、命名与兼容清理以协同规范和 [AGENTS.md](AGENTS.md)为准。没有提交、推送或上传。
