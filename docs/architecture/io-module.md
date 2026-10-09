# 点云读写模块

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

规范实现位于 `modules/10_pointcloud_io`，target 为 `pcv_m10_pointcloud_io`；`pcv_m10_pointcloud_read`、`pcv_m10_pointcloudread`、`pcv_io`、`pcv_filtering` 和 `pcv_m80_plane_output` 仅为迁移期兼容入口。

## 支持格式

- ASCII PLY；
- binary little-endian PLY；
- binary big-endian PLY；
- 任意标量属性顺序；
- 可选 `nx`、`ny`、`nz` 法向属性。

读取器按属性名读取 `x`、`y`、`z`，拒绝缺坐标、不支持的类型、vertex list、坏 header、坏数字、非有限值和截断 payload。

## 缓存与取消

`pcv::detail::io::readPly` 和 `readPlyCached` 保持点序、bounds、source index、取消和错误语义。缓存校验格式版本、源文件状态、点数和 payload 长度，并使用 `QSaveFile` 写入。

共享读取器不访问 QWidget/OpenGL。过滤、PNG/PLY/JSON writer 和 `pcv::io::ArtifactTransaction` 同属本模块；事务提供 staging、相对路径校验、覆盖策略、取消、备份、提交和回滚。具体字段、计时和并行解析约束见 v0.3 数据契约与公共头文件。

最后核对日期：2026-10-09（文档静态核对）。
