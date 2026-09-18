# 文档索引

当前模块重整基线为 v0.4；协同真相源为 [module_ui_integration_v0.4.md](architecture/module_ui_integration_v0.4.md)，数据契约仍沿用 [pointcloudview_v0.3.md](requirements/pointcloudview_v0.3.md)。

## 文档分类

- `architecture/`：模块边界、依赖方向和代码构成。
- `requirements/`：当前需求和历史版本需求。
- `contracts/`：PNG、PLY、JSON 和位姿契约。
- `modules/`：规范模块、实验相机、CLI 及未实现模块的状态、接口、测试和变更记录。
- `development/`：Windows、Qt 和 CMake 构建说明。
- `user-guide/`：应用操作和验收说明。
- `research/`：历史调研和技术分析。

## 模块文档

- [10_pointcloud_io](modules/10_pointcloud_read.md)：点云读写、缓存、相机帧采集与转换、过滤和事务输出（文档文件名保留旧名用于兼容）
- [20_pointcloud_render](modules/20_pointcloud_render.md)
- [30_pointcloud_stitch](modules/30_pointcloud_stitch.md)
- [40_pointcloud_matching](modules/40_pointcloud_matching.md)
- [50_coordinate_conversion](modules/50_coordinate_conversion.md)
- [60_geometry_workpiece](modules/60_plane_fitting.md)：平面、边缘、ROI、WObj 和临时工件（文档文件名保留旧名用于兼容）
- [70_roi_template](modules/70_roi_template.md)：兼容入口，规范实现已并入 60
- [80_plane_output](modules/80_plane_output.md)：兼容入口，规范实现已并入 10
- [pointcloudcli](modules/pointcloudcli.md)：独立 CLI 构建、PLY 读取摘要与临时工件业务入口
- [90_interferenceplane](modules/90_interferenceplane.md)：未实现
- [100_qualityreport](modules/100_qualityreport.md)：未实现

模块文档采用追加式变更记录。当前状态以文档顶部和源码、CMake、测试为准；文档中的历史测试数量仅用于追溯，不代表本次验证结果。

### 名称迁移说明

2026-09-17 v0.4 模块重整：`05_camerainput` 并入 `10_pointcloud_io`；多帧 ICP 归属 30，ROI/临时工件几何归属 60，80 输出归属 10，40 改为规划占位。旧 target/header 仅转发。

2026-09-06 第一阶段模块重命名：`10_pointcloudread`、
`20_pointcloudrender`、`30_pointcloudstitch`、`40_pointcloudregistration`、
`50_coordinateconversion`、`60_planefitting`、`80_planeoutput` 分别改为
`10_pointcloud_read`、`20_pointcloud_render`、`30_pointcloud_stitch`、
`40_pointcloud_matching`、`50_coordinate_conversion`、`60_plane_fitting`、
`80_plane_output`。旧 target 保留为兼容入口；历史文档中的旧名称仅用于追溯。

最后核对日期：2026-09-17。
