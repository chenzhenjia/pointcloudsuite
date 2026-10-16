# 20_pointcloud_render：画布与渲染

版本 v0.5，核对日期 2026-10-15。target pcv_m20_pointcloud_render，依赖 pcv_core 与 Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets，不依赖 apps。[架构](../architecture/software_architecture_v0.5.md) · [日志](../changelog/v0.5.md)。

## 当前职责与接口

PointCloudCanvas 类在全局 namespace，头路径 pcv/render/pointcloud_canvas.h；实现 modules/20_pointcloud_render/src/pointcloud_canvas.cpp。头路径不等于 pcv::render namespace。画布负责 OpenGL 上下文、VBO/FBO、点绘制、拾取、轮廓/坐标系及渲染 DTO，不承担业务几何或任务编排。

仅 GUI 线程修改 QWidget/OpenGL；点、轮廓、坐标系和 overlay 必须有限且索引合法。应用提供原始/显示点映射，画布负责索引转换；异步会话及 DTO 转换归应用，后台 worker 不访问画布。

## 灰度与选面

PointColorOverlay 接收 60 的严格 source raster 映射，渲染层校验后上传；只给有效平面点灰度，其他点回退原颜色。点云/平面/source provenance 变化清覆盖，同一平面边缘更新或 WObj1 修改保留覆盖，切换颜色模式保留缓存。无网格/UV 或彩色 PLY 输出。

所选平面单击复用拾取与高亮，算法归 60、确认状态归应用。拾取到首个完整高亮的 GUI 耗时需独立记录，不能引用算法耗时替代。

所选平面对话框加载当前帧时，应用均匀抽取最多 300,000 点，通过 `setDisplayCloud(points, displayToFullIndices, fullPointCount)` 发布。画布校验长度、越界、重复和非有限数据，拒绝非法快照且保留旧显示；拾取返回完整索引，平面/边缘/选中点叠加映射到可见点，不补画未显示点。`setCloud()`/`setCloudStreaming()` 清除映射并恢复原索引语义；拼接结果恢复 `setCloud(result.points)` 完整显示。统计为“当前帧展示 / 完整点数”；画布使用 `int` 拾取索引，加载阶段拒绝超出该范围的完整帧。`hasRenderedCurrentCloud()` 在 frameSwapped 时判断当前点云版本是否已绘制，防止将旧帧交换记为新帧首显。当前映射、失败保留及全量恢复回归放在 `selected_plane_dialog_tests_v1`；真实 OpenGL/首帧时序由用户验收。

## 兼容与验证

旧 pcv_m20_pointcloudrender/旧头仅转发。pointcloud_canvas_contract_tests 覆盖非法值、索引及 DTO；真实 Windows/OpenGL、大点云、缩放、视觉方向和窗口关闭另验。历史显示细节见[归档](../archive/history_pre_v0.5.md)，本轮仅静态核对。

六分屏整合（2026-10-16）：六个独立画布复用既有完整下标映射，仅批量入口设置更小最小尺寸；放大切换同一布局保留视角。正式结果仍setCloud完整显示。平面叠加更新推进渲染revision，使frameSwapped计时等待真正的候选绘制；重复设置相同选面模式不再反复抢焦点。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)，真实OpenGL未验收。


2026-10-16 整合v2：沿用setDisplayCloud映射和绘制revision，真实鼠标/滚轮在应用激活格；默认格不抢索引。结果仍setCloud完整显示，主窗口模板读取完整显示。画布公共API本轮未另改，六实例真实OpenGL待用户验收。见[当前整合](../development/batch_plane_selection_template_integration_v2.md)。

2026-10-16 运行验证：新增selected_plane_dialog_tests_v1 --native-gl独立原生模式，在Windows desktop OpenGL 3.3下实际显示六画布，验证GPU拾取完整索引、完整候选绘制、放大恢复与完整拼接结果绘制；Debug/Release均通过，RTX4060/OpenGL3.3日志已记录。本模块生产代码/API本轮未修改，应用图标QRC由应用及相应主窗口测试target的AUTORCC补齐。常规offscreen回归仍保留；真实大点云、手工视角操作和性能不能由小夹具替代，见[修复说明](../development/build_runtime_repair_v1.md)。
