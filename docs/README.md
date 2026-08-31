# 文档索引

当前项目基线为 v0.3，统筹文档为 [pointcloudview_v0.3.md](requirements/pointcloudview_v0.3.md)。

## 文档分类

- `architecture/`：模块边界、依赖方向和代码构成。
- `requirements/`：当前需求和历史版本需求。
- `contracts/`：PNG、PLY、JSON 和位姿契约。
- `modules/`：十个模块的状态、接口、测试和变更记录。
- `development/`：Windows、Qt 和 CMake 构建说明。
- `user-guide/`：应用操作和验收说明。
- `research/`：历史调研和技术分析。

## 模块文档

- [10_pointcloudread](modules/10_pointcloudread.md)
- [20_pointcloudrender](modules/20_pointcloudrender.md)
- [30_pointcloudstitch](modules/30_pointcloudstitch.md)
- [40_pointcloudregistration](modules/40_pointcloudregistration.md)
- [50_coordinateconversion](modules/50_coordinateconversion.md)
- [60_planefitting](modules/60_planefitting.md)
- [70_roi_template](modules/70_roi_template.md)
- [80_planeoutput](modules/80_planeoutput.md)
- [90_interferenceplane](modules/90_interferenceplane.md)：未实现
- [100_qualityreport](modules/100_qualityreport.md)：未实现

模块文档采用追加式变更记录。当前状态以文档顶部和源码/CMake/测试为准。

最后核对日期：2026-08-31。
