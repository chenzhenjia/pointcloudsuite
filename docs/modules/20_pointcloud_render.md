# 20_pointcloud_render：画布与渲染

版本 v0.5，核对日期 2026-10-15。target pcv_m20_pointcloud_render，依赖 pcv_core 与 Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets，不依赖 apps。[架构](../architecture/software_architecture_v0.5.md) · [日志](../changelog/v0.5.md)。

## 当前职责与接口

PointCloudCanvas 类在全局 namespace，头路径 pcv/render/pointcloud_canvas.h；实现 modules/20_pointcloud_render/src/pointcloud_canvas.cpp。头路径不等于 pcv::render namespace。画布负责 OpenGL 上下文、VBO/FBO、点绘制、拾取、轮廓/坐标系及渲染 DTO，不承担业务几何或任务编排。

仅 GUI 线程修改 QWidget/OpenGL；点、轮廓、坐标系和 overlay 必须有限且索引合法。应用负责原始/显示点映射、异步会话及 DTO 转换，后台 worker 不访问画布。

## 灰度与选面

PointColorOverlay 接收 60 的严格 source raster 映射，渲染层校验后上传；只给有效平面点灰度，其他点回退原颜色。点云/平面/source provenance 变化清覆盖，同一平面边缘更新或 WObj1 修改保留覆盖，切换颜色模式保留缓存。无网格/UV 或彩色 PLY 输出。

所选平面单击复用拾取与高亮，算法归 60、确认状态归应用。拾取到首个完整高亮的 GUI 耗时需独立记录，不能引用算法耗时替代。

## 兼容与验证

旧 pcv_m20_pointcloudrender/旧头仅转发。pointcloud_canvas_contract_tests 覆盖非法值、索引及 DTO；真实 Windows/OpenGL、大点云、缩放、视觉方向和窗口关闭另验。历史显示细节见[归档](../archive/history_pre_v0.5.md)，本轮仅静态核对。
