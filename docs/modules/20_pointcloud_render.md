# 20_pointcloud_render：画布与渲染

2026-11-05 清理：移除无引用`pcv_m20_pointcloudrender`，统一链接`pcv_m20_pointcloud_render`；公共头、渲染及拾取不变，保留两个GUI的调用。[说明](../development/application_target_cleanup_v1.md)。

2026-10-30 扫描几何深度预览：`ScanGeometryCanvasV2` 复用 `DepthImageCanvasV1` 显示原始深度帧；SDK转换后切回既有点云画布。模块20仅接收中立 `CameraFrame` 预览数据，不调用SDK、不持有物化生命周期。PLY显示及拾取保持原行为。[开发记录](../development/scan_geometry_demo_v1.md#2026-10-30-sdk深度缓存与按需物化)。

版本 v0.5，核对日期 2026-10-15。target pcv_m20_pointcloud_render，依赖 pcv_core 与 Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets，不依赖 apps。[架构](../architecture/software_architecture_v0.5.md) · [日志](../changelog/v0.5.md)。

## 当前职责与接口

PointCloudCanvas 类在全局 namespace，头路径 pcv/render/pointcloud_canvas.h；实现 modules/20_pointcloud_render/src/pointcloud_canvas.cpp。头路径不等于 pcv::render namespace。画布负责 OpenGL 上下文、VBO/FBO、点绘制、拾取、轮廓/坐标系及渲染 DTO，不承担业务几何或任务编排。

仅 GUI 线程修改 QWidget/OpenGL；点、轮廓、坐标系和 overlay 必须有限且索引合法。应用提供原始/显示点映射，画布负责索引转换；异步会话及 DTO 转换归应用，后台 worker 不访问画布。

## 灰度与选面

PointColorOverlay 接收 60 的严格 source raster 映射，渲染层校验后上传；只给有效平面点灰度，其他点回退原颜色。点云/平面/source provenance 变化清覆盖，同一平面边缘更新或 WObj1 修改保留覆盖，切换颜色模式保留缓存。无网格/UV 或彩色 PLY 输出。

所选平面单击复用拾取与高亮，算法归 60、确认状态归应用。拾取到首个完整高亮的 GUI 耗时需独立记录，不能引用算法耗时替代。

所选平面对话框加载当前帧时，应用均匀抽取最多 300,000 点，通过 `setDisplayCloud(points, displayToFullIndices, fullPointCount)` 发布。画布校验长度、越界、重复和非有限数据，拒绝非法快照且保留旧显示；拾取返回完整索引，平面/边缘/选中点叠加映射到可见点，不补画未显示点。`setCloud()`/`setCloudStreaming()` 清除映射并恢复原索引语义；拼接结果恢复 `setCloud(result.points)` 完整显示。统计为“当前帧展示 / 完整点数”；画布使用 `int` 拾取索引，加载阶段拒绝超出该范围的完整帧。`hasRenderedCurrentCloud()` 在 frameSwapped 时判断当前点云版本是否已绘制，防止将旧帧交换记为新帧首显。当前映射、失败保留及全量恢复回归放在 `selected_plane_dialog_tests_v1`；真实 OpenGL/首帧时序由用户验收。

## 兼容与验证

## 默认预览 v1（2026-10-23）

默认点大小为 2，使用蓝-青-绿-黄-红的高度 `RAINBOW` 伪彩并按当前 Z 范围自动映射。`PointCloudCanvas` 通过透明子画布绘制无数值色条、左下 XYZ 轴和右上方向控件；默认隐藏地板网格、包围盒和统计文字。纹理灰度仍由应用切换到 color mode 3，不改变默认高度伪彩。

旧公共头继续转发，无引用旧target已删除。pointcloud_canvas_contract_tests覆盖非法值、索引及DTO；真实Windows/OpenGL、大点云、缩放、视觉方向和窗口关闭另验。历史显示细节见[归档](../archive/history_pre_v0.5.md)，本轮仅静态核对。

六分屏整合（2026-10-16）：六个独立画布复用既有完整下标映射，仅批量入口设置更小最小尺寸；放大切换同一布局保留视角。正式结果仍setCloud完整显示。平面叠加更新推进渲染revision，使frameSwapped计时等待真正的候选绘制；重复设置相同选面模式不再反复抢焦点。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)，真实OpenGL未验收。


2026-10-16 整合v2：沿用setDisplayCloud映射和绘制revision，真实鼠标/滚轮在应用激活格；默认格不抢索引。结果仍setCloud完整显示，主窗口模板读取完整显示。画布公共API本轮未另改，六实例真实OpenGL待用户验收。见[当前整合](../development/batch_plane_selection_template_integration_v2.md)。

2026-10-16 运行验证：新增selected_plane_dialog_tests_v1 --native-gl独立原生模式，在Windows desktop OpenGL 3.3下实际显示六画布，验证GPU拾取完整索引、完整候选绘制、放大恢复与完整拼接结果绘制；Debug/Release均通过，RTX4060/OpenGL3.3日志已记录。本模块生产代码/API本轮未修改，应用图标QRC由应用及相应主窗口测试target的AUTORCC补齐。常规offscreen回归仍保留；真实大点云、手工视角操作和性能不能由小夹具替代，见[修复说明](../development/build_runtime_repair_v1.md)。

## CAD 面画布 v1（2026-10-23）

新增 `pcv::render::CadMeshCanvasV1`：只接收core `CadMeshV1`（顶点/三角形/faceId），独立OpenGL面显示、深度拾取、旋转/平移/缩放及面高亮，不依赖60/OCCT。输入拒绝非法顶点/索引；选面状态在应用，正式曲面采样不取显示网格点作为最终点。见[STEP实施说明](../development/step_cad_template_v1.md)。


2026-10-23 深度画布v1：SDK无关pcv::render::DepthImageDisplayV1/DepthImageCanvasV1，C16直接伪彩、掩码覆盖、亮度切换、原像素拾取、滚轮缩放/右键平移；20不依赖10/60。[说明](../development/camera_depth_preview_cache_v1.md)。

## 独立扫描几何演示的实例外观（2026-10-26）

当前扫描几何三维调用方按工件拟合状态切换：无工件候选为高度色，有效工件候选为统一灰点，结果/修剪为统一灰点并显示蓝色模板/红色干涉区域；蓝/红仅显示拟合区域。本次仅应用层复用既有接口，不改20实现和旧调用方默认行为。完整高度范围缓存、动态图例及用户验收见[当前说明](../development/scan_geometry_real_selected_plane_v2.md#2026-11-04-按工件拟合状态切换配色)。

2026-11-05 新增 `setUniformPointColor(const QColor&)` / `clearUniformPointColor()`：仅 GUI 线程设置有效 RGB；默认未启用，启用后常规点绘制绕过高度色、法向照明、纹理、marker 颜色及 marker 点径，清除恢复原模式；不修改点/映射。选择高亮 pass 仍供旧调用方使用，新 GUI 不提交选择点。统一色启用时高度色条隐藏。

`setControlPointIndices(const QVector<int>&)` 按完整点索引映射展示点，忽略未显示/无效下标、去重；独立 QPainter 白缘深色十字随投影更新，逻辑像素由 Qt 按 DPI 绘制，不改变点色或拾取。此为屏幕提示而非深度几何，云替换/streaming 后清除。新 GUI 保留 25% 蓝/红 PlanarMaskOverlay 与闭合边界、深度测试及原偏移，停用 marker 和修剪点高亮。旧调用方默认外观/API 兼容；本轮未构建或原生验收，见[当前说明](../development/scan_geometry_real_selected_plane_v2.md#2026-11-05-灰色点云与独立拟合面显示)。

`PointCloudCanvas::setAppearance(const QColor &background, const QColor &selection)` 设置实例背景与选中点颜色，仅 GUI 线程接受有效颜色。默认仍为黑底、白色选中点；`paintGL` 和单点/矩形 GPU 拾取后的背景恢复使用实例配置。点数提示根据背景亮度选择深浅文字，黑底保留原文字颜色。

`bool setPointColorOverlay(const pcv::render::PointColorOverlay &overlay)` 复用既有逐点覆盖 DTO、验证和纹理颜色缓冲；调用方按完整点云下标提供颜色与 valid，显示子集内部经已有 display→full 映射提取。非法输入返回 false 且保留旧覆盖。与灰度覆盖共用同一缓冲，调用方通过既有 color mode 3 显示；没有新增配准、选面算法或序列化格式。

`setPointMarkerOverlay()` / `clearPointMarkerOverlay()` 是独立的标注显示通道，按完整点索引映射到当前显示子集，颜色不受 color mode 影响。仅有效标注点在常规点云通道上绘制更大圆点、深色外缘、浅色内缘和指定颜色核心；点径随设备像素比缩放。GPU 拾取仍使用原独立 pass。默认无 marker 的旧调用方渲染不变。

`apps/scan_geometry_demo_v1` 使用高度彩虹作底色，蓝色覆盖工件面，红色覆盖人工干涉面；应用独立维护稳定 ID、修剪和会话确认。高侧候选提醒保持在应用层，不改变模块 20 DTO 或 GPU 拾取。矩形框选直接复用深度 GPU 拾取，仅返回当前视角可见点，不新增穿透选择。模块 20 仍只依赖 core/Qt，不依赖演示应用或其他算法模块。

新增原生 view 回归覆盖默认黑底白选中、浅底拾取恢复、覆盖映射/非法保留、遮挡拾取和修剪显示。首轮 Debug/Release 全量 CTest 各 47/47；代码修正后 Release 末轮 47/47，Debug 末轮第 47 项按用户接手验证要求停止。最后测试环境追加顺序调整未重配置，最终 GUI/DPI/旧工作台人工验收归用户，不能由这些测试代替。详见[实施记录](../development/scan_geometry_demo_v1.md)。

## 2026-10-26 真实离线单页调用方 v2

`apps/scan_geometry_demo_v1/scan_geometry_canvas_v2.*` 复用现有实例外观、PointColorOverlay、setDisplayCloud 和 GPU 可见点拾取；本轮不改模块 20 渲染算法或旧默认外观。原始帧 display→full 映射保留完整下标，结果修剪映射至不可变原始结果下标，并由应用层检查结果 revision。蓝色为工件，橙色为待审核工件，黄色为人工干涉标注；干涉标注不混入正式结果。视角/缩放/尺寸变化取消待擦除框，确认擦除才改会话遮罩。原生 GL/DPI/遮挡与默认黑底回归由用户执行，见 [v2 说明](../development/scan_geometry_real_selected_plane_v2.md)。

## 2026-10-29 扫描几何画布配色

扫描几何页使用 `PointCloudCanvas::setAppearance` 设置实例级深色背景，不改变其他调用方默认外观。基础点色恢复模块 20 的高度彩虹模式；`PointColorOverlay.valid=0` 的点继续使用高度渐变，工件/干涉/待审核索引以稀疏覆盖显示。GPU 拾取仍使用独立颜色/深度 framebuffer，不依赖显示色。

右侧 HTML 风格五步条属于应用层导航，不进入模块 20。应用只开放“扫描与几何选择”，其余工件信息、位置姿态、探针和发布项禁用；选面/拼接/保存子阶段由会话状态门控。该 UI 调整不改变画布、着色、拾取或其他 pointcloudview 调用方。

同页使用实例级 `setEmptyMessage(QString)` 关闭模块通用 PLY 空态，改由应用显示扫描数据空态，避免两条提示重叠。空字符串只关闭该实例占位文本，默认消息对既有调用方不变。

## 扫描几何高度预览与标注（2026-10-30）

扫描几何调用方按模块 20 默认 `colorMode=0`、point size 2、overlay 1.0 显示转换后原始帧，并使用完整 robot_base Z 范围设色阶。高度彩虹保持原样，蓝色工件面和红色干涉面走独立 marker 通道，以放大点径及明暗双层边缘强调；拾取继续使用独立 pass。纹理覆盖契约及其他调用方默认外观不变。

## 拟合平面掩码叠加（2026-10-30）

`PlanarMaskOverlay` 接收世界坐标顶点、三角索引、闭合边界线环、颜色及透明度；`PointCloudCanvas::setPlanarMaskOverlays()` 在 GUI 线程验证后更新动态 VBO/EBO。三角面片按半透明填充绘制，边界按设备像素比设线宽，绘制不更改点颜色、纹理 overlay、拾取索引或正式点云。设置新点云/显示子集时清除旧掩码，调用方需在该次发布后提交与新来源匹配的 overlay。

扫描几何使用蓝色工件面、红色待确认/已确认干涉面，默认填充透明度 25%。掩码由模块 60 在完整拟合点索引上生成，闭合边界按 U/V 占用单元追踪外环和孔洞环；渲染使用 `GL_LINE_STRIP` 与 VAO 绑定的元素缓冲。掩码生成峰值工作区通过共享 `ProcessingBudget` 申请，失败只丢弃叠加并保留选面结果，应用通过交互提示 tooltip 报告失败。旧黄色 `Contour`/轮廓渲染和未调用新 API 的调用方默认行为不变。

## 新 GUI 加载耗时 v1（2026-11-05）

新增可选 `PointCloudCanvas::displayTiming(revision,QJsonObject)` 与 `displayCloudRevision()`；只统计CPU发布/缓冲准备/OpenGL调用，不做GPU同步。默认无回调，不改变旧外观或拾取。新GUI使用worker提供的完整高度范围，避免界面扫描；详见 [说明](../development/scan_geometry_load_performance_v1.md)。仅静态检查，用户构建验证。

## 2026-11-05 加载耗时优化 v2

setDisplayCloud对严格递增映射采用相邻校验和lower_bound反查，避免百万点反向QHash构建；乱序仍用QHash且拒绝重复/越界，替换完整/流式数据重置快路径。displayTiming记录mapping_strategy，配色/缓冲/深度拾取不变。补递增/乱序/缺失/重复/越界/修剪回归源码；本轮仅静态检查，原生拾取与实际性能待用户验证。[说明](../development/scan_geometry_load_performance_v2.md)。

2026-11-05 应用提前预览v3：新GUI按source verification状态提交/隐藏旧平面叠加，本次高度范围身份改为frameId+attempt，已校验同数据恢复区域不重传点。模块20本轮无代码修改，拾取/渲染契约不变。[用户回归](../development/scan_geometry_load_performance_v3.md)。

2026-11-05 最终结果区域：新GUI复用setPlanarMaskOverlays和统一色接口，结果及修剪起灰点、蓝红25%面片、无色条；网格由应用后台提供，20无新算法/默认行为变化。[说明](../development/scan_geometry_result_regions_v1.md)。


## STEP 模板与自动匹配 v1

2026-11-05 STEP模板：`CadMeshCanvasV1::setFaceColors/setAppearance/clearMesh` 提供实例级灰/蓝/红CAD显示，旧默认不变。匹配复用25%掩码与闭合边界，候选切换仅更新叠加。 [开发与验收](../development/step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
