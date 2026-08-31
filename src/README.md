# 共享源码

`src/` 保存基础设施、过滤和迁移期兼容聚合 target。v0.3 规范算法实现位于 `modules/`。

- `src/core`：`pointcloud::Point3D` 等基础类型。
- `src/infrastructure`：运行目录、缓存和日志路径能力。
- `src/filtering`：比例、体素和统计滤波。
- `src/io`、`src/registration`、`src/interface`、`src/output`：迁移期兼容 target 和旧实现边界。

共享实现不得依赖 `apps/` 或 Qt Widgets。新增公共算法应放入对应 `modules/<编号>_<名称>/src`，公共头放入模块 `include/`。

最后核对日期：2026-08-31。
