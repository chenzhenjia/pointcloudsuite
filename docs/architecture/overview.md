# 架构概览

PointCloudSuite 是一个多应用 CMake 工作区，应用依赖共享库，共享库不反向依赖应用。

```text
apps -> shared libraries in src -> Qt and platform services
tests -> shared libraries and selected compatibility sources
tools -> shared libraries and diagnostic entry points
```

## 当前共享模块

- `pcv_core`：点云基础值类型。
- `pcv_infrastructure`：缓存、日志和导出目录的操作系统路径管理。
- `pcv_io`：ASCII/二进制 PLY 读取、取消、进度和校验缓存文件。
- `pcv_filtering`：比例与体素采样，显式区分真实点和质心策略。
- `pcv_output`：平面/边缘 Mask PNG、JSON 和 binary little-endian PLY 输出契约。
- `pcv_registration`：XML 手眼标定读取、刚体矩阵校验、位姿插值和线扫点云到 `robot_base` 的转换。
- `pcv_interface`：临时扫描信息契约解析、平面/ROI 提取以及临时工件四件输出的事务提交。

现有 processor 源文件仍是应用边界，算法会逐步抽取。新的可复用代码放入 `src/`，
项目头文件放入 `include/pcv/`，不得直接放入应用目录充当共享实现。

v0.3 迁移后，`modules/10_pointcloudread`、`modules/30_pointcloudstitch`、
`modules/40_pointcloudregistration`、`modules/50_coordinateconversion`、
`modules/70_roi_template` 和 `modules/80_planeoutput` 已承载规范源码与独立
`pcv_m<编号>_<名称>` target；原 `src/*` target 保留为兼容聚合入口，待调用方完全切换后清理。

## 依赖规则

- 共享算法代码不得依赖 Qt Widgets。
- 应用可以依赖共享模块；共享模块不得依赖应用。
- 运行时生成文件不得写入源码树。
- 测试只能从根目录 `tests/` 注册。

当前 `pointcloudview` 直接链接 `pcv_interface`，并通过它复用 `pcv_registration`；
`pointcloudstitch` 也已改为链接 `pcv_registration`，不再直接编译应用目录的
`handeye_transform.cpp`。共享模块不依赖任一应用。`pcv_output` 和 `pcv_interface` 额外使用
`Qt::Gui` 的 `QImage` 和矩阵类型。
当前 Debug CTest 目标为
`ply_reader_tests`, `cloud_cache_tests`, `downsample_tests`,
`statistical_filter_tests`, `plane_output_tests`, `temp_workpiece_interface_tests`,
`pointcloudprocessor_obstacle_tests`（当前内容为边缘 Mask 回归）和
链接 `pcv_registration` 的 `handeye_transform_tests`。
