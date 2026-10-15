# 60_geometry_workpiece：几何与工件流程

版本 v0.5，核对日期 2026-10-15。target pcv_m60_geometry_workpiece，依赖 core/infrastructure、10/50、Qt Core/Gui、Threads；不依赖 apps/QWidget。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [日志](../changelog/v0.5.md)。

## 实际 API

| header | current_namespace_and_entry |
|---|---|
| pcv/planefitting/plane_fitting.h | pcv::planefitting::fit |
| pcv/planefitting/single_click_plane_selection_v1.h | pcv::planefitting::preparePlaneSelectionIndex/selectPlaneAtPoint |
| pcv/workpiece/temp_workpiece_geometry.h | pcv::roi |
| pcv/workpiece/roi_matching.h | pcv::matching::matchRoiToRobotBase |
| pcv/workpiece/temp_workpiece_interface.h | pcv::interface::prepareTempWorkpiece/finalizeTempWorkpiece |
| pcv/scan/scan_planner.h | pcv::scan::planScanTrajectory 及 JSON 序列化 |
| pcv/texture/texture_mapping.h | pcv::texture::mapPlaneBrightness |
| pcv/workpiece/workpiece_template_v1.h | pcv::workpiece::saveWorkpieceTemplate/loadWorkpieceTemplate |

头路径 workpiece 不表示 pcv::workpiece namespace 已落地。原 60/70/roi/matching/interface 兼容 target/header 保留，不新增第二套几何。

## 平面与单击选面 v2

三点 fit 拒绝重复/越界/非有限/共线，保留 RANSAC/PCA、RMS、阈值、planarity、连通域与取消诊断。等价提速保留 300 轮、固定种子和模型采样上限，parallelEnabled=false/maxParallelTasks=4，StageTimings；PCA/RMS 保持串行浮点累加顺序。

单击 API 保留 v1 头名，行为已为 v2。不可变索引绑定同一份 const 点缓冲，替换/编辑后重建；surfaceToleranceMm 默认 0.4，maximumGapMm 默认 3（0～50），parallelEnabled=false/maxParallelTasks=4。多尺度 RANSAC/PCA 初始模型，全帧按垂直距离重分类；估计两个方向采样间距，正常连接后以真实边界点距离跨小缺失带，稳健精化最多六轮，集合稳定停止。

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
