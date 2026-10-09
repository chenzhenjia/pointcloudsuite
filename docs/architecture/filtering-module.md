# 点云过滤模块

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

过滤实现已迁入 `modules/10_pointcloud_io` / `pcv_m10_pointcloud_io`，公共头位于 `pcv/processing/`，当前实际命名空间为 `pcv::detail::filtering`（规范 namespace 迁移待实施）。`pcv_filtering` 与旧 `src/filtering` 仅保留兼容转发，不再新增实现。

## 算法边界

- 比例降采样按输入顺序保留真实点。
- 体素降采样明确区分 `FirstInputPoint` 和 `Centroid`。
- 统计滤波保留有效点和 source index；邻域不足或结果为空时按接口约定回退并返回诊断。
- 非法参数不得悄悄生成虚拟点。
- 共享过滤模块不得依赖应用或 Qt Widgets。

详细参数、默认值和测试入口以 `modules/10_pointcloud_io`、`include/pcv/processing` 和 `tests/` 为准。

最后核对日期：2026-10-09（文档静态核对）。
