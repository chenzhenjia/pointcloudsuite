# 架构概览

PointCloudSuite v0.3 是多应用 CMake 工作区。应用依赖共享模块，共享模块不得反向依赖应用。

## 分层结构

```text
apps/
  pointcloudview       Qt Widgets/OpenGL 查看与处理
  pointcloudstitch     Qt 多帧拼接界面
modules/
  10_pointcloudread    PLY 读写与缓存
  20_pointcloudrender  Qt/OpenGL 点云画布
  30_pointcloudstitch  拼接流程编排
  40_pointcloudregistration ICP、结构校验、seam fusion
  50_coordinateconversion 手眼标定和坐标转换
  60_planefitting      纯平面算法
  70_roi_template      ROI、模板和临时工件接口
  80_planeoutput       PNG、PLY、JSON 成套输出
src/                  基础设施、过滤和兼容聚合 target
tests/                CTest 测试
tools/                可选诊断工具
```

## 依赖规则

- 共享算法放在模块 `src/`，公共头放在模块 `include/`。
- `20_pointcloudrender` 是唯一允许依赖 Qt Widgets/OpenGL 的共享模块。
- `30_pointcloudstitch` 只负责流程编排。
- `40_pointcloudregistration` 是 ICP、相邻帧配准和 seam fusion 的唯一算法来源。
- `60_planefitting` 只负责纯几何拟合和诊断。
- 应用负责 UI、异步任务和用户提示。
- 运行数据不得写入源码树。

## 当前入口

`pointcloudview` 使用 `pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、`pcv_m60_planefitting`、`pcv_m70_roi_template` 和 `pcv_m80_planeoutput` 等模块。

`pointcloudstitch` 使用 `pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration` 和 `pcv_m50_coordinateconversion` 等模块。

`registration_diagnostic` 通过 `pcv::interface::stitchRawLineProfiles` 调用统一拼接接口。

最后核对日期：2026-08-31。
