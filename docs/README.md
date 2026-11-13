# PointCloudSuite 文档

当前源码包含三个 GUI、六个领域模块与 RAW 转换工具。当前文档使用稳定路径；协议版本保留在正文与文件内容。历史测试结果不能替代本轮验证。

- [需求与能力](requirements/requirements.md)
- [整体架构](architecture/architecture.md)、[数据流](architecture/data-flow.md)、[模块与应用协同](architecture/module-ui-integration.md)
- [数据契约](contracts/data-contracts.md)
- [Windows 构建](development/build-windows.md)、[测试](development/testing.md)、[开发计划](development/development-plan.md)、[迁移方案](development/migration-plan.md)
- 应用开发：[工作台](applications/pointcloudview.md)、[扫描几何](applications/scan-geometry.md)、[STEP 模板](applications/step-template-builder.md)、[RAW 转换](applications/raw-converter.md)
- 接口说明：[scan_geometry会话、数据与输出](development/scan-geometry-interfaces.md)
- 使用指南：[工作台](user-guide/pointcloudview.md)、[扫描几何](user-guide/scan-geometry.md)、[STEP 模板](user-guide/step-template-builder.md)、[输入输出](user-guide/input-output.md)
- 模块：[10 IO](modules/pointcloud-io.md)、[20 渲染](modules/pointcloud-render.md)、[30 拼接](modules/pointcloud-stitch.md)、[40 匹配](modules/pointcloud-matching.md)、[50 坐标](modules/coordinate-conversion.md)、[60 几何](modules/geometry-workpiece.md)
- 背景研究：[相机 SDK](research/camera-sdk.md)、[RZ90](research/rz90-stitching.md)
- 历史：[变更记录](archive/changelog.md)、[历史需求](archive/historical-requirements.md)、[历史开发](archive/historical-development.md)、[迁移表](archive/document-migration.md)
