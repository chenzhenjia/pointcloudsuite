# 60_geometry_workpiece：几何与工件流程

## 已有采样结果生成模板 v1（2026-11-06）

新增 `generateStepTemplatePackageFromSampledV1()`，接收不可变 `CadCloudResult`、工件/干涉平面角色和完整点删除下标，修剪后重新生成逐面范围、区域网格和边界，再走既有CAD模板事务；不重新采样，原始完整采样计数和来源指纹保留。

2026-11-05 清理：移除无引用旧plane_fitting/planefitting及70转发target，统一链接`pcv_m60_geometry_workpiece`；70公共兼容头保留。拟合、掩码、高侧及模板算法不变。[说明](../development/application_target_cleanup_v1.md)。

2026-11-05 应用契约复用：扫描几何GUI将最终保留点交给既有`buildPlaneMaskV1`，再光栅化成0/128/255契约PNG，并用既有WObj矩阵/轴/位姿作一致性校验；60公共头与算法不改。base X平行法向的既有WObj退化仍拒绝。见[契约适配说明](../development/scan_geometry_contract_adapter_v1.md)。

版本 v0.5，核对日期 2026-10-15。target pcv_m60_geometry_workpiece，依赖 core/infrastructure、10/50、Qt Core/Gui、Threads；不依赖 apps/QWidget。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [日志](../changelog/v0.5.md)。

## 扫描几何 WObj1 API（2026-10-29）

新增公共头 `pcv/roi/automatic_workpiece_frame_v1.h`，提供 `buildWorkpieceCoordinateSystemFromRobotAxes(const QVector3D &origin, const QVector3D &planeNormal)`。实现只负责有限值、法向归一化、robot_base X/Y 投影、右手系和 `[X,Y,Z,A,B,C]` 姿态校验；参考面完整点集的包围盒中心、投影和确认生命周期由 `scan_geometry_demo_v1` 负责。显示抽样点不进入该计算，模块 60 不依赖应用或 QWidget。

扫描几何应用将 WObj1、参考帧、来源 revision、修剪索引和人工干涉索引写入 `selected_plane_geometry_v1.json`。该文件属于应用派生输出，不改变模块 60 既有模板、ROI、纹理或扫描输出格式，也不代表机器人可执行性、碰撞安全或控制器下发。

## 实际 API

| header | current_namespace_and_entry |
|---|---|
| pcv/planefitting/plane_fitting.h | pcv::planefitting::fit |
| pcv/planefitting/single_click_plane_selection_v1.h | pcv::planefitting::preparePlaneSelectionIndex/selectPlaneAtPoint |
| pcv/planefitting/reference_plane_match_v2.h | pcv::planefitting::matchReferencePlaneV2；固定参考面辅助候选，必须人工审核 |
| pcv/planefitting/high_side_plane_candidates_v1.h | pcv::planefitting::findHighSidePlaneCandidatesV1；按已选平面高侧提出未标注平面提醒 |
| pcv/workpiece/temp_workpiece_geometry.h | pcv::roi |
| pcv/workpiece/roi_matching.h | pcv::matching::matchRoiToRobotBase |
| pcv/workpiece/temp_workpiece_interface.h | pcv::interface::prepareTempWorkpiece/finalizeTempWorkpiece |
| pcv/scan/scan_planner.h | pcv::scan::planScanTrajectory 及 JSON 序列化 |
| pcv/texture/texture_mapping.h | pcv::texture::mapPlaneBrightness |
| pcv/workpiece/workpiece_template_v1.h | pcv::workpiece::saveWorkpieceTemplate/loadWorkpieceTemplate |

头路径workpiece不表示pcv::workpiece namespace已落地。60/70/roi/matching/interface公共兼容头保留，无引用转发target已清理，不新增第二套几何。

## 平面与单击选面 v2

三点 fit 拒绝重复/越界/非有限/共线，保留 RANSAC/PCA、RMS、阈值、planarity、连通域与取消诊断。等价提速保留 300 轮、固定种子和模型采样上限，parallelEnabled=false/maxParallelTasks=4，StageTimings；PCA/RMS 保持串行浮点累加顺序。

单击 API 保留 v1 头名，行为已为 v2。不可变索引绑定同一份 const 点缓冲，替换/编辑后重建；surfaceToleranceMm 默认 0.4，maximumGapMm 默认 3（0～50），parallelEnabled=false/maxParallelTasks=4。多尺度 RANSAC/PCA 初始模型，全帧按垂直距离重分类；估计两个方向采样间距，正常连接后以真实边界点距离跨小缺失带，稳健精化最多六轮，集合稳定停止。

## 高侧平面提醒 v1（2026-10-30）

`findHighSidePlaneCandidatesV1` 接收完整 robot_base 点云、同缓冲不可变索引、已选平面及其中心、应排除的完整点下标和门限。它从索引局部模型确定性抽取最多 16 个种子，复用 `selectPlaneAtPoint()` 获取完整候选；以候选点集包围盒中心 Z 减工件面中心 Z 筛选，至少默认高 2 mm、100 点。搜索不计算或比较法向量，因此水平或倾斜工件均按 robot_base 世界 Z 解释；倾斜面高度仅是中心高度近似。重合候选按点索引交集/较小候选达到 80% 合并；与已选/标注点集重合达到 80% 的候选排除。

API 仅建议人工检查，不确认为干涉面、不改变拼接 DTO。搜索支持取消，区分 Success/InvalidInput/Cancelled/Error；有限种子可能漏检，候选提醒不是碰撞或完整性判定。`scan_geometry_demo_v1` 只保存候选 seed/高度/点数元数据，用户选择标注后才用既有完整选面任务获取待确认干涉点集，避免长期保留多份大索引。

## 完整拟合区域掩码 v1（2026-10-30）

新增 `pcv/planefitting/plane_mask_v1.h` / `buildPlaneMaskV1()`。输入完整 `robot_base` 点集、拟合模型、完整平面点索引和 U/V 采样间距；单击选面同时传入选面器的 U 采样轴，掩码按同一平面基向量解释各向异性间距。三点选面无采样轴时使用确定性平面基；未提供正间距时按投影范围与完整点数估算。算法将点投影至模型平面，以占用单元生成贴合平面的三角面片，并沿单元边界追踪闭合环，内部空缺保留为孔洞，凹形边界不作凸包填充。输出顶点不回写原始点云，也不进入选面快照或拼接/磁盘产物。

支持取消、完整索引校验和共享 `ProcessingBudget` 峰值工作区限制；取消、无效输入、超预算或内存分配失败时不返回半成品网格。输出掩码是显示几何，允许应用缓存；调用方应随输入/frame revision 丢弃旧网格。有限采样间距会将落入同一单元的邻近点合并成占用区域，因此区域边缘是采样网格近似，不等同于原始点的精确 alpha shape。

混合/共线局部不能整格丢弃目标点；最终点始终满足发布模型容差，不补点/加厚/压平、不因时间阈值截断。0 只关闭额外跨带；近邻共面独立物体可能合并，需预览确认。参数变化使候选失效但保留已确认快照。应用负责三点备用、确认/自动下一帧、revision 与关闭 join。

worker 只读 const Qt 容器，私有结果全部 join 后按输入顺序合并；连续数组投影网格和相同模型/候选的连通复用不改变结果。

## 临时工件、扫描与纹理

PLY 内存描述符与扫描 JSON 共用 50 转换和几何尾段；两组三点与同面校验后生成 WObj1、边缘、ROI/Mask，由 ArtifactTransaction 成套提交四件套。baseline 不是 40 移动判定。

扫描 Global 使用拟合平面，LocalRoi 使用现有 ROI 不重拟合，UI 默认 LocalRoi。WObj1 XY 包围盒定范围，默认高度/宽度/重叠/两端延长 50/100/50/50 mm；线序 -X→+X、行程 +Y→-Y，单线宽度容差 0.1 mm。项目/机器人 JSON、P[10] 编号、WObj 局部坐标和 9990 点限制见数据契约；不做 IK/碰撞/刀路算法。

纹理严格 source_raster_index、原始声明点数等于像素数、左上 row-major，仅平面点灰度；拒绝非法/变更 provenance。无裁剪/缩放/翻转/配准；WObj1 不参与映射。原始坐标实验经两组三点同面校验后复用映射，不手眼/建 WObj1/出四件套。

## 三维工件模板 v1

独立保存当前整幅实测点云，robot_base/mm，保留 XYZ、已有法向及点序；允许缺失表面，不裁剪/补面/估计法向，不要求 WObj1，不含纹理、逐点映射、匹配或移动判定。公共请求/结果为 SaveWorkpieceTemplateRequest、LoadWorkpieceTemplateRequest、WorkpieceTemplateResult，元数据为 WorkpieceTemplateInfo；source 只作文件级追溯，不依赖原始文件存在。

每次生成名称规范化后的独立 `_v1_时间戳` 目录，冲突追加序号；JSON 保留原始名称，schema 为 pcv-workpiece-template-v1。两件产物为 workpiece_template_robot_base.ply（六个 float32 字段、Binary LE）和 workpiece_template_info.json；版本时间 UTC。默认根为运行数据目录/workpiece_templates，源码树内输出及指向源码树的目录链接被拒绝。10 ArtifactTransaction 默认拒绝覆盖；普通失败/commit 前取消撤销本轮输出，commit 后取消不改变已保存状态。

读取检查 schema/kind、robot_base/mm、合法名称/版本/时间、点数、包内相对路径（含链接目标）、六字段固定 PLY 布局与载荷长度、有限 XYZ/法向和包围盒一致性。全零法向合法且不代表有效法向；未知格式或坏数据直接失败，不静默过滤或修复。progress 回调在 worker/reader worker 执行，调用方必须线程安全；保存结果不携带点缓冲，读取成功才返回 points。

应用独立维护原始/当前点云来源与 revision，只有已确认的手眼转换、整帧拼接/仅转换、模板读取可建模板；去噪继承并追加步骤，恢复原始缓冲恢复原始来源，不从文件名或 WObj1 推断坐标。GUI 创建前勾选基准确认，读写后台互斥、可取消、关闭 join；成功读取才清理旧派生结果并发布。真实工件质量与 GUI 样例验收仍独立进行。

## 测试与风险

plane_fitting_tests、plane_fitting_equivalence_tests_v2、single_click_plane_selection_tests_v1/v2、roi_matching_tests、temp_workpiece_interface_tests、scan_planner_tests、texture_mapping_tests；应用 selected_plane_dialog/texture_experiment 另测。串行与 1/2/4 worker 必须保持额外 Qt 共享副本回归，覆盖取消/过期/关闭 join 和 manifest 参数回读。

本轮仅静态审计。历史 630 万点模块中位数不代表真实 GUI 两秒；真实选面完整性、复杂缺失带、row-major、WObj1 朝向、Start/End 和机器人安全待验收。

后续工件模板增量另有 workpiece_template_tests_v1 / workpiece_template_view_tests_v1，覆盖文件往返、搬迁、非法数据、事务/取消、来源继承、过期结果和关闭生命周期；本次实际构建、CTest 和规模记录统一见版本日志的工件模板 v1 条目，不沿用上段文档整理轮的静态结论。

批量选面优化（2026-10-16）：IndexPreparationOptions提供可选point→bucket缓存与独立网格法向并行；单击复用分类/投影/距离/连通队列缓冲，内存由可选ProcessingBudget租约共享，预算不足保持原判断并退回串行/阶段释放。保留网格编号/点序、邻域累计及PCA/RMS顺序、v2容差/跨带/六轮规则。索引与单击结果追加outOfMemory诊断。应用将完整拼接结果直接交现有saveWorkpieceTemplate，主窗口仍loadWorkpieceTemplate，不新增模板schema/匹配功能。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)；本轮回归源码未执行。


2026-10-16 整合v2：完整候选投影使用65536点连续窗口私有输出，独立预算预留、全join后原序归并；低于100000点/预算不足串行，线程创建失败join后串行剩余区间。网格法向同样先join再提交。SingleClickResult尾部追加projectionMs/normalConnectivityMs诊断，不改v2数学/拓扑/六轮/正式字段；102400点串行/1/2/4/零预算对照源码已补未运行。见[完整处理v3](../development/full_resolution_processing_acceleration_v3.md)。

2026-10-16 构建修复：preparePlaneSelectionIndex内样本计数和网格计数分别使用validSampleCount/bucketCount，修复同作用域重复声明C2373及连带C2737，不改数学/顺序/接口。此次实际全新Debug/Release全量构建和各43项CTest通过，包含完整性v2及并行/预算对照；原生六画布流程另通过。真实工件精度和性能仍独立验收，见[修复说明](../development/build_runtime_repair_v1.md)。

2026-10-22 连续采集纹理点云 v1：新增 `pcv::texture::mapMultiFrameBrightness`，按拼接结果的 `cloudIds/sourceIndices` 从逐帧 Mono8 row-major 栅格产生等长 `grayscale/valid` 数组和映射统计。输入尺寸、载荷、来源索引或取消不满足时返回失败；当前位姿直拼不去重点，重复目标由调用方按采集顺序处理，正式点云几何和 PLY schema 不变。实现只依赖 10 的采集回读数据，不依赖 apps。[实现说明](../development/camera_capture_texture_v1.md)

## STEP/CAD 模板 v1（2026-10-23）

新增 `pcv/workpiece/cad_template_v1.h`（namespace `pcv::workpiece`）：STEP读取、不可变模型、曲面采样及独立CAD模板读写。默认OFF的OCCT7.9.3仅私有解析/几何实现；关闭STEP时CAD包仍可读写。cad_model/mm不冒充robot_base，不增加匹配功能。接口、算法、容差和验证见[实施说明](../development/step_cad_template_v1.md)。


2026-10-23 相机深度悬停v1：应用临时展开一帧后复用既有完整索引/单击选面，默认0.4mm/3mm、正式实测点不变；不新增深度选面算法、不接入六分屏拼接。[说明](../development/camera_depth_preview_cache_v1.md)。

## 2026-10-26 固定参考面辅助匹配 v2

新增 `pcv/planefitting/reference_plane_match_v2.h` / `pcv::planefitting::matchReferencePlaneV2`，由新单页离线 GUI 调用，不属于模块 40 位置确认。目标完整点云及不可变索引必须匹配同一缓冲，参考平面已在 robot_base/mm。默认搜索垂距 10 mm、法向角 5°（绝对点积消除法向正负差）、最多 16 个确定性分散种子；参数可调，正式选面垂距仍使用原容差。

实现复用索引局部模型及 `selectPlaneAtPoint`，不增加第二套 RANSAC/PCA，不翻转/补点/压平。交集按较小候选点数计算，≥80% 合并重复候选；两个不同候选立即返回 Ambiguous。返回 Unique/Ambiguous/NotFound/Cancelled/InvalidInput/Error、完整候选模型/索引、种子/实际尝试序列、距离角度及诊断。有限种子下的唯一候选仍须人工审核，不等于全局唯一或严格配准质量通过。接口、测试源码与待用户验收项见 [v2 说明](../development/scan_geometry_real_selected_plane_v2.md)，本轮未构建/测试。

2026-11-05 加载计时：PlaneSelectionIndexResult新增budgetAcquireMs，统计既有点桶与法向缓冲预算申请，包含于总准备/grid/normals内。不改索引或选面算法。公共DTO需全量重编，[说明](../development/scan_geometry_load_performance_v1.md)，本轮仅静态检查。

## 2026-11-05 真实日志耗时优化 v2

IndexPreparationOptions追加normalBatchSize（默认4096，新GUI显式65536）；仅改变法向调度块大小，邻居累计/PCA/确定性提交顺序不变。私有并行模型申请ProcessingBudget，退回4096再串行，启动失败join后处理剩余。结果补网格/批次/实际最大并行块/回退/计算/等待/提交耗时；compute sum、join与normals相互包含，不可累加。ReferencePlaneMatchResultV2追加seedSelectionMs供应用直接复用原SingleClickResult后处理，不新增拟合或改变候选规则。公共DTO须完整重编。补4096/65536、1/2/4worker、预算/取消等价测试源码，本轮未运行。[说明](../development/scan_geometry_load_performance_v2.md)。

## 2026-11-05 性能优化 v3

IndexPreparationOptions追加cacheNormalNeighbors默认false，新GUI显式开启：临时27邻居int表，13方向查找双填，按旧slot序累计，预算/分配失败直查，法向结束释放。SingleClickOptions.reuseConnectivityWorkspace默认true，任务私有数组/容量复用（新增留存按projected×256预算，失败原路径），拟合/连通/跨带/六轮数学不变。PlaneMaskOptionsV1.preferDenseGrid默认true，紧凑且预算允许用连续占用/顶点表，稀疏或失败回原路径；完整边界/孔洞/区域不变，内部编号可变。三个结果DTO追加诊断计时，不进正式schema；公共头需完整重编。补等价/预算/取消源码未运行。[v3开发说明](../development/scan_geometry_load_performance_v3.md)。

## 2026-11-05 结果区域固定网格元数据

PlaneMaskResultV1返回有效double间距及投影原点；PlaneMaskOptionsV1间距double、追加默认关闭fixedGridOrigin/gridOriginU/V，仅应用结果修剪启用固定网格。占用/三角面/边界算法不变，旧调用方自动原点保持。完整点生成，公共DTO全量重编；[说明](../development/scan_geometry_result_regions_v1.md)。


## STEP 模板与自动匹配 v1

2026-11-05：CAD面信息增加解析平面原点/法向，采样结果追加完整逐面范围；新增 `template_features_v1` 和 `template_package_v1`，复用完整选面/区域、裁剪CAD网格与孔洞边界，事务保存多角色伴随文件。旧模板schema保持。 [开发与验收](../development/step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
