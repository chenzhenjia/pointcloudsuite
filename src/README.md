# 共享源码

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

`src/` 保存基础设施和迁移期兼容聚合 target。v0.4 规范算法实现位于 `modules/`；过滤、writer 和事务实现已物理迁入 10。

- `src/core`：`pointcloud::Point3D` 等基础类型。
- `src/infrastructure`：运行目录、缓存和日志路径能力。
- `src/filtering`：历史兼容转发；规范过滤实现位于 `modules/10_pointcloud_io` 的 `pcv/processing/` 头路径，实际 namespace 仍为 `pcv::detail::filtering`。
- `src/io`、`src/registration`、`src/interface`、`src/output`：迁移期兼容 target 和旧实现边界。

共享实现不得依赖 `apps/` 或 Qt Widgets。新增公共算法应放入对应 `modules/<编号>_<名称>/src`，公共头放入模块 `include/`。

最后核对日期：2026-10-09（文档静态核对）。
