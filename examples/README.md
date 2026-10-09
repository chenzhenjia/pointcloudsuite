# 示例

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

本目录包含非生产配置和输出示例，用于说明文件结构和契约，不代表生产数据、精度基准或性能基准。

- `configs/pointcloud_2d_output.example.jsonc`：平面输出配置示例；当前字段以 v0.3 数据契约和 v0.4 的 10 `ArtifactTransaction` 输出边界为准。
- `jobs/minimal_plane_output_v0.1/`：历史版本的最小 PNG、JSON、robot_base 平面 PLY 示例。

示例不得包含构建输出、运行缓存、日志、凭据或大体积回归点云。历史示例保留原始版本标识，不作为当前版本输入模板。

最后核对日期：2026-10-09（文档静态核对）。
