# step_template_builder_v1

独立 Windows Qt Widgets STEP 数模模板制作程序。主界面是 `step_template_builder_window_v1.ui`，可以在 Qt Creator/Designer 编辑；后台会话位于 `step_template_builder_session_v1.*`。

本应用仅提供“工件信息 → 导入数模 → 创建模板草案”。点云视觉和矩形修剪参考 `scan_geometry_demo_v1`，但矩形筛选在完整点集上沿视线执行。程序不包含扫描采集、拼接、探针、机器人控制、模板管理中心或实测匹配页。

构建开关：`PCV_BUILD_STEP_TEMPLATE_BUILDER_V1` 和 `PCV_BUILD_STEP_IMPORT` 均默认 ON。STEP 开启时需要 OCCT 7.9.3。详细范围、静态验证和用户验收见 [开发说明](../../docs/development/step_template_builder_v1.md)。

本轮已完成 Debug/Release 编译；`轴承.step` 因既有闭合单实体契约被拒绝，原生 OpenGL/DPI 视觉验收仍待执行。
