# 30_pointcloudstitch

## 当前状态

流程模块已实现，应用级界面适配仍保留。规范代码位于 `modules/30_pointcloudstitch`，target 为 `pcv_m30_pointcloudstitch`。

## 职责

编排 PLY 读取、坐标转换、相邻帧配准、seam fusion、正式输出和失败回滚。配准算法由 `pcv_m40_pointcloudregistration` 提供。

## 接口与测试

统一接口为 `pcv::interface::stitchRawLineProfiles`。主要测试为 `stitching_interface_tests`。应用只负责 UI、输入组织、进度和结果展示。

最后核对日期：2026-08-31。
