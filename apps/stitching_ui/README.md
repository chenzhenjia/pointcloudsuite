# stitching_ui：共享拼接窗口

v0.5。target pcv_stitching_ui为pointcloudview使用的应用层静态库，提供StitchingWindow和ui/stitchingwindow.ui，仅在pointcloudview开启时构建。独立拼接应用已删除；真实选面任务库另由两个GUI共用。链接Qt Core/Gui/Widgets/Concurrent、core/infrastructure、10/30/50；领域模块不得反向依赖此库。

共享输入/排序去重/整行位姿移动、普通/高级参数、日志/进度/取消/覆盖确认；只调用pcv::interface::StitchingRequest/stitch。seam距离/决策块与并行设置透传，输出不覆盖输入。

StitchingPublication包含全量点云、formalPly/previewPly/mappingFile/reportJson/outputDirectory，仅成功事务后发出stitchingSucceeded。View子窗关闭隐藏，主窗关闭取消/join。失败取消不替换旧会话，成功按应用revision门禁发布。

主线维护窗口、线程和依赖；UI feature/外部项目接入遵循[协同规范](../../docs/architecture/module_ui_integration_v0.5.md)。[架构](../../docs/architecture/software_architecture_v0.5.md) · [测试与历史](../../docs/changelog/v0.5.md)。本轮未做GUI验收。

## 2026-10-26 共享所选平面任务 v2

新增静态库 `pcv_selected_plane_tasks_v2`，头/实现为 `selected_plane_tasks_v2.h/.cpp`，namespace `pcv::ui`。提供 loadSelectedPlaneFrameV2、prepareDisplayCloudV2、prepareSelectedPlaneIndexV2、fitFullPlaneV2、loadedCloudDigestV2，复用 10/50/60，承载既有 SelectedPlaneFrame DTO 与 30 结果类型。只依赖 Qt Core/Gui/Concurrent 和领域模块，不拥有窗口或调度。

旧 SelectedPlaneDialog 通过兼容类型别名与转发使用这些任务，原调度保持；新 `ScanGeometrySessionV2` 使用独立两帧加载/串行匹配队列。展示抽样上限可由调用方指定，默认300000；旧对话框沿用默认，新 GUI 每帧显式请求最多1000000点。正式索引/快照保留完整点与来源。取消 token 必须非空；加载/索引需传入共享 ProcessingBudget。转换后 SHA256 摘要只用于会话重载一致性，不改变文件 schema。原 `pcv_stitching_ui` target 公开转接该任务库，保留旧调用方的 include/link；30 不反向依赖应用。当前新 GUI 接入、测试源码及用户验证分工见 [v2 开发说明](../../docs/development/scan_geometry_real_selected_plane_v2.md)。
v2 摘要补充：包含来源路径、声明点数、Start/End/手眼矩阵、signedTravel和完整点/来源/ratio序列，以便跨轮重载不误用来源元数据已变的候选；仍仅为当前加载一致性检查，不改变既有输出契约。

2026-11-05：SelectedPlaneTaskV2补充分段计时与高度范围；prepareHeightRangeV1在新GUI后台使用，旧调用方不新增完整扫描。共享读取/抽样默认上限不变。公共DTO改变须全量重编。详见 [加载计时v1](../../docs/development/scan_geometry_load_performance_v1.md)。

2026-11-05 性能v2：snapshotFromSingleClickV2将既有SingleClickResult转换为完整快照/面片，供人工单击与自动匹配共用，后者不再二次拟合。快照三向量reserve，snapshotMs只统计复制，performance单独记录模型后处理/中心/面片。prepareSelectedPlaneIndexV2尾参数normalBatchSize默认4096，新GUI传65536，旧调用方不变。未运行构建或测试。[说明](../../docs/development/scan_geometry_load_performance_v2.md)。

2026-11-05 性能v3：prepareSelectedPlaneIndexV2追加cacheNormalNeighbors=false尾参数，旧签名调用保持；新GUI传true。SelectedPlaneTaskV2增加sourceVerified/verifiedMs回传来源校验状态，snapshot performance透传连通细项和PlaneMaskResultV1.performance。新GUI将完整摘要移到后续索引任务，旧对话框读取流程不变。公共头重编，未运行测试。[v3说明](../../docs/development/scan_geometry_load_performance_v3.md)。

2026-11-05：SelectedPlaneTaskV2::Kind尾追加BuildResultRegions，旧kind数值保持；区域DTO和任务编排在新GUI私有层，不使共享任务依赖apps。详见[说明](../../docs/development/scan_geometry_result_regions_v1.md)。
