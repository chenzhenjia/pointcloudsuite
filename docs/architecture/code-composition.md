# 代码构成

本文档描述当前 v0.3 代码边界。详细 API 和字段以公共头、CMake 和测试为准。

## 规范模块

| target | 主要职责 | 依赖边界 |
|---|---|---|
| `pcv_m10_pointcloud_read` | PLY 读写、缓存；相机帧数据类型、帧转换、Mock 源；可选 Mv3dLp 实现（05 已并入） | `pcv_core`、`pcv_infrastructure`、Qt Core |
| `pcv_m20_pointcloud_render` | OpenGL context、VBO/FBO、绘制和点选 | `pcv_core`、Qt GUI/Widgets/OpenGL |
| `pcv_m30_pointcloud_stitch` | 多帧输入组织与 PLY 读取、坐标转换/多帧匹配调用、seam fusion、拼接诊断、正式输出（唯一拼接编排） | 10、40、50、80 |
| `pcv_m40_pointcloud_matching` | ICP、结构校验等底层能力；平面/ROI 点匹配接口 `pcv::matching` 与 `robot_base` 完整回传 | `pcv_core`、`pcv_filtering`、50、Qt Core/Gui |
| `pcv_m50_coordinate_conversion` | XML 标定、位姿插值、坐标转换（唯一数学实现） | `pcv_core`、Qt Core/Gui |
| `pcv_m60_plane_fitting` | 三点/n 点拟合、RANSAC/PCA、连通域、几何诊断 | `pcv_core`、Qt Core/Gui |
| `pcv_m70_roi_template` | 临时扫描、ROI、模板和 Mask；ROI 匹配改调 40 统一接口 | 10、40、50、80 |
| `pcv_m80_plane_output` | PNG、PLY、JSON 成套输出和回滚 | `pcv_core`、`pcv_infrastructure`、10、Qt Core/Gui |

实验相机能力由 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制，位于 `pcv_m10_pointcloud_read`
（`pcv::camera` 头与实现）。无 SDK 时仍可构建纯函数转换与 Mock 源；真实 Mv3dLp 编译
仅在 SDK 存在时启用。相机取流不属于 v0.3 稳定发布基线。

历史目录名 `05_camerainput`、`10_pointcloudread`、`20_pointcloudrender`、
`30_pointcloudstitch`、`40_pointcloudregistration`、`50_coordinateconversion`、
`60_planefitting`、`80_planeoutput` 仅在兼容层与历史文档中出现。

## 应用和工具

- `pointcloudview`：界面、异步读取、画布生命周期、业务状态和 DTO 转换。
- `pointcloudstitch`：界面、输入组织、进度和结果展示。
- `intensity_viewer`：实验性相机亮度图预览与非覆盖 PNG 保存；仅在实验相机开关开启且至少构建一个桌面应用时生成。
- `registration_diagnostic`：仅链接模块 target，调用 `stitchRawLineProfiles`。
- `pointcloudcli`：独立 C++17 命令行入口，使用 Qt Core/Gui/Concurrent 及所需共享模块，提供 PLY Header/完整读取摘要与临时工件四件套流程；不链接 Widgets、OpenGL 或 GUI application target。
- `tests/`：注册模块单元测试、接口测试和必要的兼容入口测试。

## 兼容层

`pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、
`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、
`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，
以及 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output` 和旧 forwarding
header 在迁移期保留，只做 target/header 转发，不再增加新功能。删除前必须完成引用审计、
全量构建、CTest、GUI 和真实夹具验收，并单独提交。

最后核对日期：2026-09-13（CLI PLY 读取、运行数据目录与便携部署同步后）。
