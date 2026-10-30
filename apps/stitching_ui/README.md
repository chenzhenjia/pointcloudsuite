# stitching_ui：共享拼接窗口

v0.5，2026-10-15。target pcv_stitching_ui为应用层静态库，View/Stitch共用StitchingWindow和ui/stitchingwindow.ui。链接Qt Core/Gui/Widgets/Concurrent、core/infrastructure、10/30/50；领域模块不得反向依赖此库。

共享输入/排序去重/整行位姿移动、普通/高级参数、日志/进度/取消/覆盖确认；只调用pcv::interface::StitchingRequest/stitch。seam距离/决策块与并行设置透传，输出不覆盖输入。

StitchingPublication包含全量点云、formalPly/previewPly/mappingFile/reportJson/outputDirectory，仅成功事务后发出stitchingSucceeded。View子窗关闭隐藏，主窗关闭取消/join；独立Stitch退出取消/join。失败取消不替换旧会话，成功按应用revision门禁发布。

主线维护窗口、线程和依赖；UI feature/外部项目接入遵循[协同规范](../../docs/architecture/module_ui_integration_v0.5.md)。[架构](../../docs/architecture/software_architecture_v0.5.md) · [测试与历史](../../docs/changelog/v0.5.md)。本轮未做GUI验收。

## 2026-10-26 共享所选平面任务 v2

新增静态库 `pcv_selected_plane_tasks_v2`，头/实现为 `selected_plane_tasks_v2.h/.cpp`，namespace `pcv::ui`。提供 loadSelectedPlaneFrameV2、prepareDisplayCloudV2、prepareSelectedPlaneIndexV2、fitFullPlaneV2、loadedCloudDigestV2，复用 10/50/60，承载既有 SelectedPlaneFrame DTO 与 30 结果类型。只依赖 Qt Core/Gui/Concurrent 和领域模块，不拥有窗口或调度。

旧 SelectedPlaneDialog 通过兼容类型别名与转发使用这些任务，原调度保持；新 `ScanGeometrySessionV2` 使用独立两帧加载/串行匹配队列。展示抽样上限可由调用方指定，默认300000；旧对话框沿用默认，新 GUI 每帧显式请求最多1000000点。正式索引/快照保留完整点与来源。取消 token 必须非空；加载/索引需传入共享 ProcessingBudget。转换后 SHA256 摘要只用于会话重载一致性，不改变文件 schema。原 `pcv_stitching_ui` target 公开转接该任务库，保留旧调用方的 include/link；30 不反向依赖应用。当前新 GUI 接入、测试源码及用户验证分工见 [v2 开发说明](../../docs/development/scan_geometry_real_selected_plane_v2.md)。
v2 摘要补充：包含来源路径、声明点数、Start/End/手眼矩阵、signedTravel和完整点/来源/ratio序列，以便跨轮重载不误用来源元数据已变的候选；仍仅为当前加载一致性检查，不改变既有输出契约。
