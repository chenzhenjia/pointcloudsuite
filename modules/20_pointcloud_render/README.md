# 20_pointcloud_render

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前状态

迁移完成，真实桌面交互验收待完成。规范代码位于 `modules/20_pointcloud_render`，target 为 `pcv_m20_pointcloud_render`。旧 target `pcv_m20_pointcloudrender` 保留为迁移期兼容入口（仅转发）。

## 职责

点云渲染与交互，包含 `PointColorOverlay`、等长 per-point 灰度覆盖和 GUI 线程上传。严格亮度采样属于 60；WObj1 变化不清除严格栅格纹理，点云或平面索引变化才使覆盖失效。`PointCloudCanvas` 负责 Qt/OpenGL context、shader、VBO、VAO、picking FBO、绘制、点选、矩形选区和视角交互。实现位于 `src/pointcloud_canvas.cpp`，头文件仅保留接口声明和字段。

## 依赖与测试

可依赖 Qt Widgets/OpenGL 和 `pcv_core`，不得依赖 `apps/` 或应用 processor。主要测试为 `pointcloud_canvas_contract_tests`。真实 Windows 桌面点选、矩形选区、旋转、平移、缩放和关闭验收需单独记录。

最后核对日期：2026-10-09（文档静态核对）（v0.4 UI 工作区与渲染边界复核）。
