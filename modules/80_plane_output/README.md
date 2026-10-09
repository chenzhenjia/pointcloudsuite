# 80_plane_output

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

兼容入口。实现已迁入 `modules/10_pointcloud_io`；`pcv_m80_plane_output`、`pcv_m80_planeoutput` 与 `pcv_output` 仅转发。

## 职责

PNG、PLY、JSON 成套输出。输出 Grayscale8 PNG、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。三件文件先写入 staging 目录，成功后成套提交，失败时回滚。

## 接口与测试

主要接口为 `writePlaneOutput`。主要测试为 `plane_output_tests`。输出路径、错误码和 JSON 字段以 v0.3 契约为准。

最后核对日期：2026-10-09（文档静态核对）（兼容入口同步；规范 writer 与事务归 10）。
