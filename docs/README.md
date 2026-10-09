# 文档索引

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

正式需求与状态基线为 [pointcloudview_v0.4.md](requirements/pointcloudview_v0.4.md)，模块/UI 协同规则见 [module_ui_integration_v0.4.md](architecture/module_ui_integration_v0.4.md)，既有数据契约与历史增量仍沿用 [pointcloudview_v0.3.md](requirements/pointcloudview_v0.3.md)。业务目标和所有代码更新记录进入 [target.md](../target.md)。

## 推荐阅读顺序

1. [README.md](../README.md)：项目组成、构建入口和当前能力。
2. [v0.4 需求基线](requirements/pointcloudview_v0.4.md)：已实现/目标状态、兼容债务、业务与验收边界。
3. [架构概览](architecture/overview.md) 与 [UI 协同文档](architecture/module_ui_integration_v0.4.md)：依赖、namespace 映射、owner 白名单、adapter 与交接包。
4. 下方规范模块文档：公共头、实际 API、测试归属和历史链接。
5. [v0.4 开发方案](development/module_convergence_v0.4.md)：公共边界、生产调用迁移与独立验收顺序。
6. [Windows 构建说明](development/build-windows.md) 与 [使用/验收说明](user-guide/POINTCLOUDVIEW_REQUIREMENTS.md)：后续验证与现场操作。

本次 2026-10-09 整理核对全部既有 47 个 Markdown 文档并新增 4 个规范文档。只运行静态检查；不构建、不运行 CTest/GUI，不把历史测试数量作为本次通过。

## 文档分类

- `architecture/`：模块边界、依赖方向和代码构成。
- `requirements/`：当前需求和历史版本需求。
- `contracts/`：PNG、PLY、JSON 和位姿契约。
- `modules/`：规范模块、实验相机、CLI 及未实现模块的状态、接口、测试和变更记录。
- `development/`：Windows、Qt 和 CMake 构建说明。
- `user-guide/`：应用操作和验收说明。
- `research/`：历史调研和技术分析。

## 模块文档

- [10_pointcloud_io](modules/10_pointcloud_io.md)：点云读写、缓存、相机帧采集与转换、过滤和事务输出
- [20_pointcloud_render](modules/20_pointcloud_render.md)
- [30_pointcloud_stitch](modules/30_pointcloud_stitch.md)
- [40_pointcloud_matching](modules/40_pointcloud_matching.md)
- [50_coordinate_conversion](modules/50_coordinate_conversion.md)
- [60_geometry_workpiece](modules/60_geometry_workpiece.md)：平面、边缘、ROI、WObj、临时工件、扫描规划与严格栅格纹理
- [70_roi_template](modules/70_roi_template.md)：兼容入口，规范实现已并入 60
- [80_plane_output](modules/80_plane_output.md)：兼容入口，规范实现已并入 10
- [pointcloudcli](modules/pointcloudcli.md)：独立 CLI 构建、PLY 读取摘要与临时工件业务入口
- [90_interferenceplane](modules/90_interferenceplane.md)：未实现
- [100_qualityreport](modules/100_qualityreport.md)：未实现

旧 [10_pointcloud_read.md](modules/10_pointcloud_read.md)、[60_plane_fitting.md](modules/60_plane_fitting.md) 和 [05_camerainput.md](modules/05_camerainput.md) 保留正文并作为历史/兼容索引。70/80 文档记录兼容边界，90/100 仅为未实现占位。历史需求、研究、契约 proposal 与示例保持原版本，顶部链接到当前入口。

模块文档采用追加式变更记录。当前状态以文档顶部和源码、CMake、测试为准；文档中的历史测试数量仅用于追溯，不代表本次验证结果。

### 名称迁移说明

2026-09-17 v0.4 模块重整：`05_camerainput` 并入 `10_pointcloud_io`；多帧 ICP 归属 30，ROI/临时工件几何归属 60，80 输出归属 10，40 改为规划占位。旧 target/header 仅转发。

2026-09-06 第一阶段模块重命名：`10_pointcloudread`、
`20_pointcloudrender`、`30_pointcloudstitch`、`40_pointcloudregistration`、
`50_coordinateconversion`、`60_planefitting`、`80_planeoutput` 分别改为
`10_pointcloud_read`、`20_pointcloud_render`、`30_pointcloud_stitch`、
`40_pointcloud_matching`、`50_coordinate_conversion`、`60_plane_fitting`、
`80_plane_output`。旧 target 保留为兼容入口；历史文档中的旧名称仅用于追溯。

最后核对日期：2026-10-09（文档静态核对）。
