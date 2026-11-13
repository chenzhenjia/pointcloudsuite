# PointCloudSuite 文档索引 v0.5

更新2026-10-15。基线为HEAD75d3e3b加当前未提交/未跟踪实现，非发布声明；源码/公共头/CMake/测试定义实际能力。本轮纯文档静态整理，不执行构建/CTest/GUI。

## 推荐阅读顺序

1. [target.md](../target.md)：业务目标、当前状态、责任和简要更新。
2. [需求基线](requirements/pointcloudview_v0.5.md)：已实现、实验和未实现能力。
3. [软件整体架构](architecture/software_architecture_v0.5.md)：五层、依赖、四条业务链、取消/发布。
   [pointcloudview 架构与算法导读](development/pointcloudview_architecture_analysis_v1.md)：窗口职责、关键函数和算法流程图。
4. [当前数据契约](contracts/data_contracts_v0.5.md)与[UI/模块协同](architecture/module_ui_integration_v0.5.md)。
5. [用户指南](user-guide/POINTCLOUDVIEW_REQUIREMENTS.md)与[Windows构建](development/build-windows.md)。
6. [开发方案](development/development_plan_v0.5.md)与[路线图](development/roadmap_v0.5.md)。
7. [集中版本日志](changelog/v0.5.md)、[历史归档](archive/history_pre_v0.5.md)、[每份文档迁移去向](archive/document_migration_v0.5.md)。

连续采集纹理功能的实现、会话格式、外置光源提示和现场验收边界见[连续采集纹理点云 v1](development/camera_capture_texture_v1.md)。

## 模块与应用

| entry | current_document |
|---|---|
| 10 IO/预处理/事务/实验相机 | [10_pointcloud_io](modules/10_pointcloud_io.md) |
| 20 画布/渲染 | [20_pointcloud_render](modules/20_pointcloud_render.md) |
| 30 整帧/所选平面拼接 | [30_pointcloud_stitch](modules/30_pointcloud_stitch.md) |
| 40 规划占位/兼容聚合 | [40_pointcloud_matching](modules/40_pointcloud_matching.md) |
| 50 唯一坐标数学 | [50_coordinate_conversion](modules/50_coordinate_conversion.md) |
| 60 几何/工件/扫描/纹理 | [60_geometry_workpiece](modules/60_geometry_workpiece.md) |
| CLI | [pointcloudcli](modules/pointcloudcli.md) |
| 原工作台 / 共享UI | [View](../apps/pointcloudview/README.md) / [stitching_ui](../apps/stitching_ui/README.md) |
| 独立 STEP 模板制作 v1 | [开发与验收](development/step_template_builder_v1.md) |
| 离线 PLY 单页 / 所选平面拼接 v2 | [操作说明](../apps/scan_geometry_demo_v1/README_v2.md) / [开发与用户验收](development/scan_geometry_real_selected_plane_v2.md) |

70/80只保留兼容公共头，无构建target，详细职责归60/10；90/100并入路线图，不再各建空占位页。应用/无引用target清单和用户构建矩阵见[清理说明](development/application_target_cleanup_v1.md)。[features](../apps/pointcloudview/features/README.md)与[adapters](../apps/pointcloudview/adapters/README.md)明确同事工作边界。

## 历史、研究与示例

v0.1～v0.4需求和v0.4协同旧路径保留短指引；详细历史只维护在归档/版本日志，当前模块页不复制测试记录。研究保留：[相机SDK](research/camera_sdk_direct_canvas_plan.md)、[RZ90跨向拼接](research/rz90_cross_direction_stitching_research.md)，不视为当前稳定功能。示例见[examples](../examples/README.md)。

基线57份Markdown加1份txt，合并后删除18份Markdown和1份txt，新增10份Markdown，最终49份；无空文档可删。链接/内容迁移与静态结果见版本日志。软件CMake版本和真实schema不改，历史26/26与合成耗时不能当作本轮或现场验收。

- [新GUI加载计时与日志v1](development/scan_geometry_load_performance_v1.md)：分段观测、先显后索引、操作任务日志与用户性能验收（未运行构建测试）。

- [真实日志耗时优化v2](development/scan_geometry_load_performance_v2.md)：匹配结果复用、法向批次/预算回退、映射快路径、切帧计时与日志汇总；用户执行运行验证。

- [点云加载与选面性能v3](development/scan_geometry_load_performance_v3.md)：先预览后校验、法向邻居表、连通缓冲复用、完整面片稠密路径及用户验收。

- [最终工件模板蓝红显示v1](development/scan_geometry_result_regions_v1.md)：结果区域生成、修剪同步、取消重试与用户验收。


## STEP 模板制作与匹配后端 v1

当前制作入口为独立 `step_template_builder_v1`，提供工件信息、STEP点云、选面修剪和模板草案保存；匹配算法与旧契约保留在模块40/60，暂不提供匹配页。 [制作开发与验收](development/step_template_builder_v1.md) · [历史匹配后端说明](development/step_template_matching_v1.md)。
