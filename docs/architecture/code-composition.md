# 代码构成

本文档描述当前 v0.3 代码边界。详细 API 和字段以公共头、CMake 和测试为准。

## 规范模块

| target | 主要职责 | 依赖边界 |
|---|---|---|
| `pcv_m10_pointcloudread` | PLY 读写、缓存、取消和进度 | `pcv_core`、`pcv_infrastructure`、Qt Core |
| `pcv_m20_pointcloudrender` | OpenGL context、VBO/FBO、绘制和点选 | `pcv_core`、Qt GUI/Widgets/OpenGL |
| `pcv_m30_pointcloudstitch` | 读取后流程编排和正式输出 | 10、40、50、80 |
| `pcv_m40_pointcloudregistration` | ICP、结构校验、seam fusion | `pcv_core`、`pcv_filtering`、Qt Core/Gui |
| `pcv_m50_coordinateconversion` | XML 标定、位姿插值、坐标转换 | `pcv_core`、Qt Core/Gui |
| `pcv_m60_planefitting` | 三点/n 点拟合、RANSAC/PCA、连通域 | `pcv_core`、Qt Core/Gui |
| `pcv_m70_roi_template` | 临时扫描、ROI、模板和 Mask | 10、50、80 |
| `pcv_m80_planeoutput` | PNG、PLY、JSON 成套输出和回滚 | `pcv_core`、`pcv_infrastructure`、Qt Core/Gui |

## 应用和工具

- `pointcloudview`：界面、异步读取、画布生命周期、业务状态和 DTO 转换。
- `pointcloudstitch`：界面、输入组织、进度和结果展示。
- `registration_diagnostic`：仅链接模块 target，调用 `stitchRawLineProfiles`。
- `tests/`：注册模块单元测试、接口测试和必要的兼容入口测试。

## 兼容层

`pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output` 和旧 forwarding header 在迁移期保留。删除前必须完成引用审计、全量构建、CTest、GUI 和真实夹具验收，并单独提交。

最后核对日期：2026-08-31。
