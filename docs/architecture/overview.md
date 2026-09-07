# 架构概览

PointCloudSuite v0.3 是多应用 CMake 工作区。应用依赖共享模块，共享模块不得反向依赖应用。

## 分层结构

```text
apps/
  pointcloudview        Qt Widgets/OpenGL 查看与处理（含实验相机 UI）
  pointcloudstitch      Qt 多帧拼接界面
  pointcloudcli         独立命令行参数入口骨架
modules/
  10_pointcloud_read    点云读写、缓存；相机帧采集与转换（05 已并入）
  20_pointcloud_render  Qt/OpenGL 点云画布
  30_pointcloud_stitch  多帧拼接、seam fusion、正式输出（唯一拼接编排）
  40_pointcloud_matching 平面/ROI 点匹配、覆盖校验、robot_base 完整回传
  50_coordinate_conversion 手眼标定、位姿插值、坐标变换（唯一数学实现）
  60_plane_fitting      纯平面算法与几何诊断
  70_roi_template       ROI、模板和临时工件接口
  80_plane_output       PNG、PLY、JSON 成套输出
src/                  基础设施、过滤和兼容聚合 target
tests/                CTest 测试
tools/                可选诊断工具
```

历史目录名（如 `05_camerainput`、`10_pointcloudread`、`30_pointcloudstitch`、
`40_pointcloudregistration`）仅在兼容层与历史文档中出现，当前规范名称见上表。

## 依赖规则

- 共享算法放在模块 `src/`，公共头放在模块 `include/`。
- `20_pointcloud_render` 是唯一允许依赖 Qt Widgets/OpenGL 的共享模块。
- `30_pointcloud_stitch` 是多帧拼接的唯一编排边界：读取、坐标转换调用、
  多帧匹配调用、seam fusion、拼接诊断和正式输出均由其收敛。
- `40_pointcloud_matching` 是平面/ROI 点匹配与 `robot_base` 完整回传模块；
  保留底层 ICP/对应点搜索能力，但不再负责最终拼接输出或 seam fusion 编排。
- seam fusion 实现与公共类型位于 `30_pointcloud_stitch` 的 stitching 目录。
- `50_coordinate_conversion` 是旋转、位姿插值和手眼变换的唯一数学实现来源。
- `60_plane_fitting` 只负责纯几何拟合和诊断。
- 应用负责 UI、异步任务和用户提示。
- 运行数据不得写入源码树。

## 数据流

```text
10 输入/相机采集
    ↓
20 渲染与交互
    ↓
60 平面拟合
    ↓
70 ROI 生成
    ↓
40 ROI 点匹配并完整回传 robot_base
    ↓
30 多帧拼接、seam fusion、正式输出
```

## 当前入口

`pointcloudview` 使用 `pcv_m10_pointcloud_read`、`pcv_m20_pointcloud_render`、
`pcv_m60_plane_fitting`、`pcv_m70_roi_template`、`pcv_m30_pointcloud_stitch` 和
`pcv_m80_plane_output` 等模块。

`pointcloudstitch` 使用 `pcv_m10_pointcloud_read`、`pcv_m30_pointcloud_stitch`、
`pcv_m40_pointcloud_matching`、`pcv_m50_coordinate_conversion` 和
`pcv_m80_plane_output` 等模块。

`registration_diagnostic` 通过 `pcv::interface::stitchRawLineProfiles` 调用统一拼接接口。

`pointcloudcli` 仅在 `PCV_BUILD_CLI=ON` 时生成，当前提供 `--help`、`--version` 及参数错误契约，不链接 Qt 或共享点云模块。

## 兼容层

旧 target（`pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、
`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`、
`pcv_m60_planefitting`、`pcv_m80_planeoutput`，以及 `pcv_io`、`pcv_registration`、
`pcv_interface`、`pcv_output`）保留为迁移期兼容入口，只做 target/header 转发。
新代码一律使用规范 target；删除旧入口前需完成引用审计、全量构建、CTest、GUI 和真实夹具验收。

最后核对日期：2026-09-07（模块重命名、30/40 职责重划与 ROI 匹配接口收敛后）。
