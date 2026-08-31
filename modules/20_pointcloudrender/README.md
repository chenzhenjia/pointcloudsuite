# 20_pointcloudrender

## 当前状态

迁移完成，真实桌面交互验收待完成。规范代码位于 `modules/20_pointcloudrender`，target 为 `pcv_m20_pointcloudrender`。

## 职责

`PointCloudCanvas` 负责 Qt/OpenGL context、shader、VBO、VAO、picking FBO、绘制、点选、矩形选区和视角交互。实现位于 `src/pointcloud_canvas.cpp`，头文件仅保留接口声明和字段。

## 依赖与测试

可依赖 Qt Widgets/OpenGL 和 `pcv_core`，不得依赖 `apps/` 或应用 processor。主要测试为 `pointcloud_canvas_contract_tests`。真实 Windows 桌面点选、矩形选区、旋转、平移、缩放和关闭验收需单独记录。

最后核对日期：2026-08-31。
