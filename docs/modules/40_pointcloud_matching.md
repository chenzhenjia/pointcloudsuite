# 40_pointcloud_matching：平面/ROI 点匹配（robot_base 完整回传）

状态：已实现。本模块为平面/ROI 点匹配与 `robot_base` 完整回传模块。新正式接口：`pcv::matching::{RoiMatchInput, RoiMatchResult, matchRoiToRobotBase(...)}`，位于 `include/pcv/matching/roi_matching.h`：校验 ROI 索引、平面来源、点数量与有限性；经 50 坐标转换生成 `robot_base` 点并保留原始点索引；fail-closed（任一 ROI 点无法合法回传则不发布不完整结果）；返回完整性统计、丢失点数、覆盖率、取消状态与失败原因。

规范目录：`modules/40_pointcloud_matching/{include,src}`；模块 target：`pcv_m40_pointcloud_matching`；旧 target `pcv_m40_pointcloudregistration` 与 `pcv_registration` 保留为迁移期兼容入口（仅转发）。`registerRobotBaseFrames()`/ICP/结构校验等底层能力保留（兼容接口，内部逐步转向 matching API）；`multiframe_registration` 现位于本模块 `include/pcv/registration/multiframe_registration.h`。

本模块不再负责 seam fusion 编排与最终拼接输出：seam fusion 实现与公共类型位于 30 模块 `include/pcv/stitching/`，本模块 `include/pcv/registration/seam_fusion*.h` 仅为转发头。

接口/测试：`pcv::matching`（`roi_matching.h`）、`multiframe_registration`、`seam_fusion`（转发头）；`roi_matching_tests`、`multiframe_registration_tests`、`seam_fusion_tests`。

最后核对日期：2026-09-17。历史正文中“点云配准/seam fusion 编排”职责及旧名称（`40_pointcloudregistration`、`pcv_m40_pointcloudregistration`、`modules/40_pointcloudregistration`）为当时名称与当时职责，仅用于追溯；当前 seam fusion 实现与编排归属 30 模块，本模块保留 ICP/结构校验等底层兼容能力。

### 2026-09-17（结构点验收改为命中口径；粗对齐 trimmed 目标；ICP 内并行与提速）

- 需求登记：主模块 `40_pointcloud_matching`，配套透传位于 `30_pointcloud_stitch`。本轮 9 项已确认决策：新口径为默认、不设可见性硬门槛、证据门槛为裁剪区结构点的 10%、命中率 <0.25 只警告、允许工作区外回放、改动范围限 30/40 + 测试 + 文档、加速漂移 ≤0.01 mm/0.005 度且判定必须一致、速度不设硬指标、多平面对齐延后。
- 证据（由现网 `stitching_report.json` 反算）：失败数据集的结构点 15 mm 命中率只有 0.182 / 0.229 / 0.376 / 0.487，而旧验收把 3 mm 覆盖率除以**全部**裁剪区结构点、门槛 0.65，任何刚体变换都无法满足；同期命中点内的 3 mm 覆盖率为 0.633 / 0.639 / 0.519 / 0.716。
- `IcpOptions` / `IcpDiagnostics` 新字段全部追加在结构体尾部，旧字段与旧公式一律保留可复算：
  1. **验收口径**：`structuralCoverageOfMatched=true`（默认）时覆盖率分母改为命中点；证据门槛 = `max(minimumStructuralPoints, ceil(minimumStructuralMatchedRatio × 裁剪区结构点数))`；`visibilityWarningRatio`（默认 0.25）只置位警告、不参与判定；失败原因码区分为 `structural_points_insufficient` 与 `structural_coverage_insufficient`（`failureCode` 机器可读，`reason` 保留中文）；`structuralCoverageOfMatched=false` 逐位复现旧行为。
  2. **粗对齐目标**：`tangentCoarseTrimmedScore=true`（默认）把切向搜索目标换成“最小 K 个最近邻距离均值”（`tangentCoarseTrimmedRatio=0.25`），接受条件为相对改善 `tangentCoarseMinimumScoreGain=0.01`；旧 `translatedCoverage` 目标保留可切换，`tangentCoverageBefore/After` 继续按 5 mm 双向覆盖率上报，便于与历史报告对照。
  3. **鲁棒对应**：`normalCompatibilityDegrees`（默认 **0 = 关闭**）法向一致性门控；`correspondenceTrimFraction`（默认 0）与 `autoTrimCorrespondences`（默认 false）提供 trim 加权；`adaptiveCorrespondenceRadius` 在预对齐 p90 > 2 mm 时把首层对应半径放宽到 4 mm。
  4. **重叠框路由**：`structuralFrameRouteDegrees`（默认 **90 = 关闭**）——主平面倾角仍为浅、但其上的主结构面偏离平面法向超过阈值时，重叠框改用该结构面的局部 `u/v/n` 坐标。
  5. **提速**：ICP 内对应搜索按块并行（`correspondenceParallelTasks`，0=自动，受 `parallelEnabled` 约束），worker 只读 const 视图、只返回私有块、按块序串行合并，与串行结果逐位一致；步长试探改为“变换即计数”，不再每次试探拷贝整云；relaxed retry 增加证据预判并记录 `relaxedRetrySkippedReason`。
- 关键取舍：`autoTrimCorrespondences` 默认关闭。实测中按残差顺序裁剪会优先保留退化的支撑面点、丢弃有信息量的结构点，把已由粗对齐恢复的位姿重新拉走（合成夹具实测 4.00 mm → 8.94 mm），因此 trim 只作为显式开关保留。
- 验证：Debug 构建成功；`ctest -C Debug` **14/14 通过**；`multiframe_registration_tests` 新增 4 组用例（视角相关丢点下的命中口径与证据门槛 + legacy 对照、4 mm 切向偏移下 trimmed 与 legacy 目标的判定差异、浅主平面 + 40 度结构面的重叠框路由、150×150 稠密夹具上 ICP 内并行的逐位等价）。
- **真实数据回放（Release 工具 `registration_diagnostic`，输入为 `D:\stitch_test` 原 PLY 的硬链接 + 由报告位姿反解生成的 `pose_info.txt`）**：
  - `1/test_1`：仍接受，命中率 0.9845、命中点内 3 mm 0.8693；修正 (0.7015, 1.6979, -0.6516)/0.2784 度，与记录基线 (0.7012, 1.7057, -0.6515) 相差 0.008 mm、0.0009 度，落在 0.01 mm/0.005 度预算内。**在法向门控与结构路由同时打开时同一对修正漂移到约 1.8 mm**，因此两者改为默认关闭。
  - `5_40/test`：由拒绝转为**接受**，命中率 0.2246（`visibility=warn`）、命中点内 3 mm 0.6566、修正 (-3.7708, -2.7613, 1.6065)/0.1154 度；严格档直接通过，不再触发 relaxed retry。
  - `6_50/test`：仍拒绝，命中率 0.3703、命中点内 3 mm 0.4989（trimmed 粗对齐已把目标从 3.1292 改善到 1.7485，但精度门槛仍未达到）。按方案停止判据，不放宽门槛，改以可见性/精度数值与工艺层建议收尾。
  - 计时（Release，同一工具）：`1` registration 2.4-2.7 s、`5_40` 1.7-2.5 s；基线报告 `5_40` 为 47.7 s，但其中包含 relaxed retry（24.3 s）且为当时的构建配置，**不能全部归因于本轮提速**，同配置 A/B 仍未执行。

### 2026-09-15（并行 overlap 预计算改为只读访问）

- `registerRobotBaseFrames` 的并行 overlap 预计算不再通过非 `const` 的 `clouds[index]` 访问共享的 `QVector<ConvertedCloud>`；改为 `const QVector<ConvertedCloud> *const cloudView` 只读视图，worker 内统一使用 `cloudView->at(index).sample`。捕获列表由 `[&, index]` 收敛为 `[cloudView, index]`。
- worker 仍只返回私有 `OverlapDiagnostic`，在显式 join 屏障后由 `preparedOverlaps[index] = futures[index].result()` 串行提交，结果与扫描顺序无关。
- 每对 ICP 的修正矩阵累计（`result.registrationCorrections[index]`）与 `clouds[index].full` 变换继续严格按扫描顺序串行执行，本轮**未**并行化该提交路径。
- `clouds` 是函数内局部容器（refcount 恒为 1），因此该处属并发 `detach` 的潜在隐患；修复目标是消除该类风险，不改变任何公共契约：未修改 `IcpOptions`、`IcpDiagnostics`、`SeamFusionDiagnostic` 或 JSON schema，共享 `CancellationToken` 生命周期设计保持不变。
- 测试：`multiframe_registration_tests` 新增 4 帧串行/并行（`maxParallelTasks=1/2/4`）等价性用例，逐字段比较 `overlapDiagnostics`（含 cloudId/面积/覆盖率/IoU/warning）、点数与点序、`cloudIds/sourceIndices/scanRatios`、修正矩阵全部 16 个元素及 ICP 诊断数量，并保留输入额外共享副本；新增 30° 倾斜对在串行与并行下“成功或结构化拒绝且不取消”的回归。

### 2026-09-15（陡峭主平面配准）

- `IcpOptions` 增加 `maximumPlaneTiltDegrees=75`、确定性 RANSAC 假设数、最小平面内点比例、平面身份法向/偏移公差与陡峭面最小可观测自由度。浅倾角继续使用既有 Z 高度带路径；高度带无效或不代表主体时，`pcaPlane()` 改用 1 mm 采样的确定性 `ransac_pca` 精化。
- 陡峭面以目标平面局部 `u/v/n` 坐标建立重叠框，重叠点计数、ICP 输入、步进保留与结构验收共用该口径；候选变换后从完整 source/target 重建局部重叠。非平面结构和局部 `u/v` 边界点加入 Point-to-Point 辅助残差，主平面仍保留 Point-to-Plane；浅倾角仍使用原有 robot XYZ 路径。
- 倾角超过 75 deg 返回 `plane_tilt_exceeds_limit`；陡峭面在 ICP 前及结构验收时均要求两侧不少于 100 个非平面结构点、`observableDof >= 3` 和双向 3 mm 覆盖率不低于既有阈值，否则返回 `steep_plane_feature_insufficient`，不发布正式结果。
- `IcpDiagnostics` 与 `stitching_report.json` 补充拟合方法、法向/倾角/RMS/内点、局部重叠、特征点与陡峭面拒绝原因。坐标链仍只报警，不自动翻转或补偿。
- 验证：Debug 构建 `multiframe_registration_tests`、`stitching_interface_tests`、`seam_fusion_tests`、`pointcloudstitch` 成功；新增 60/75 deg 特征面通过、76 deg 限制拒绝、60 deg 纯平面 fail-closed 的自动回归。真实陡峭夹具和 `D:\stitch_test\6_50` 尚待重放。

### 2026-09-08（低重叠 retry 与粗对齐边界修复）

- 修复放宽 retry 未恢复缓存拟合平面的问题：retry 不再因 `sourcePlane/targetPlane` 为默认无效值而将结构验收误报为 `0/0`。
- 切向粗对齐和大范围结构候选应用后重新计算实际三维重叠框及两侧点数；候选超出修正预算、破坏重叠或产生空重叠时回退到候选前状态。
- 切向粗对齐候选按 `maximumCorrectionTranslation`/`maximumCorrectionAngleDegrees` 逐级缩放，优先保持现有安全边界，再进入 ICP。
- 验证：Debug 目标构建成功，`multiframe_registration_tests` 通过；真实 `D:\stitch_test\6_50` GUI/现场重放待复验。

### 2026-09-07（第二轮性能优化 P0：缓存扩展与 ICP 内循环）

需求：PointCloudStitch 第二轮性能优化方案 P0/P1（P2 并行化按计时证据门控，本轮未实施）。

- P0 预处理缓存：PairPreprocessCache 增加按层 target 工件缓存（LevelTargetEntry：reference 含法向 + SpatialIndex，键为层入口修正矩阵；矩阵一致 ⇒ overlap box 与结构决策一致，strict/retry 共享 downsample/estimateNormals/buildIndex，命中时源侧 moving 仍按尝试重导出）；原初 try/retry 共享的 crop/平面/初始距离统计保留。
- P0 ICP 内循环：estimateIncrement 接受可复用 correspondence 与 unique 引用 scratch（QVector<CorrespondencePair>/std::unordered_set，按轮次容量保留，消除每轮分配）；reference 恒为 actualOverlap 裁剪后体素化的点集，候选循环移除冗余 target insideBounds；27 邻域改为全局静态偏移表 neighborOffsets27；trial 边界检查改用仅变换 XYZ 的 countInsideBoundsAfterTransformXyz（法向对边界计数无贡献）。
- 计时/诊断：PairStageTimes/IcpDiagnostics/MultiFrameRegistrationResult 增加 targetLevelCacheMs/correspondenceMs/boundaryCheckMs（attempt 级 + 聚合）与 targetLevelCacheHit；IcpOptions::collectDiagnosticFrames（默认 true 兼容；GUI 标准模式 false）按需生成 pre/post 诊断帧并累计 diagnosticFrameMs。
- 验证（手工 cl 构建，/MD，O2 基准）：multiframe_registration_tests、seam_fusion_tests 连续通过；真实 robot_base 3 帧数据（90 万至 246 万点）balanced 与 diagnostic 修正矩阵与上轮完全一致（逐位），新增子计时字段按预期输出；cache 命中路径与重算路径由 seam/registration 单测及真实数据矩阵一致性覆盖。
- 风险/未验证：cmake 全量 Debug/Release CTest 与 GUI 实机待用户环境复验；target 缓存仅放宽重试命中（正常一次成功尝试无二次使用）；结构验收区 2mm 样本跨尝试缓存未实施（依赖最终修正的源侧无法安全共享），仍为后续候选。
### 2026-09-07（配准与拼接耗时优化，阶段 1/2/3）

需求：PointCloudStitch 配准与拼接耗时优化方案（源码级分析基线：粗搜索/ICP 热循环/重复预处理/seam 全量扫描/GUI 输出为热点；本会话无 cmake/ninja 环境，采用手工 cl 链路完成模块级验证）。

- 阶段 1 计时基线：IcpDiagnostics 增加每尝试阶段耗时 preprocessMs/coarseSearchMs/icpLevel1-3Ms/structuralValidationMs/attemptTotalMs/strictAttemptTotalMs/levelIterations；MultiFrameRegistrationResult 增加聚合 readMs/transformMs/preprocessMs/coarseSearchMs/icpLevel1-3Ms/structuralValidationMs/retryMs/registrationTotalMs/registrationAttemptCount。registerPair() 用 RegionTimer 分区计时（预对齐前为 preprocess、粗搜索为 coarse、每 ICP 层独立、结构校验独立），严格档与放宽档均计入聚合。应用 wrapper（pointcloudprocessor.cpp）累计 read/transform 回填结果。
- 阶段 3 分级启用：IcpOptions 默认改为 tangentCoarseAlignmentEnabled=false（新增 tangentCoarseAlignmentAuto=true：平面预对齐后覆盖率/重叠条件未满足时自动触发）、wideStructureDiagnosticEnabled=false（诊断模式显式重开）。relaxed retry 改为按 blockingClass 触发（仅 safety_boundary/prealignment_boundary/structural_validation；no_overlap、取消与输入错误不重试；主平面角度拒绝等 prealignment 类失败现纳入重试范围）。
- 阶段 2 预处理缓存与 ICP 内循环：新增 PairPreprocessCache（bounds/crop/平面拟合/初始距离统计/每层源采样基点），严格档与放宽档共享，retry 只重算依赖修正矩阵的部分；collectInitialPairDiagnostics 拆分为 computePairInitialData+stampPairInitialData；ICP step factor trial 改为复用 scratch buffer 的 countInsideBoundsAfterTransform（变换与计数合并、达阈值提前退出）；每层增加亚微米级无改善终止（连续 8 次增量平移不超过 5um 且旋转不超过 0.0005deg 视为收敛），最终安全与结构验收仍把关质量。
- 验证（本会话手工 cl 构建，/MD，O2 仅用于基准）：multiframe_registration_tests、seam_fusion_tests 连续 8 次通过；真实 robot_base 数据（D:/workpiece/output/pointcloud/1..3_*_robot_base_retained.ply，90 万至 246 万点，stride 8 采样）注册基准：平衡模式与诊断模式结果完全一致（修正矩阵逐位相同），wide 粗搜索由诊断模式约 414ms/对降至平衡模式约 8ms/对（自动触发条件跳过）；阶段聚合耗时、逐对层级迭代与阶段 ms 字段均按预期填充。
- 风险/未验证：cmake/Ninja 全量 Debug/Release CTest 与 GUI 运行需在用户环境复验；stitching_interface_tests 在本沙箱受 QTemporaryDir 目录创建限制无法运行（编译链接通过）；真实夹具多帧/粗细搜索/retry 组合占比仍需用户生产数据验收；GUI 改动仅完成 uic+cl 语法编译，未做 UI 人工冒烟。
### 2026-09-06（模块重命名与职责重划为 ROI 点匹配）

- 目录重命名：`modules/40_pointcloud_matching`，target 改为 `pcv_m40_pointcloud_matching`；旧 `pcv_m40_pointcloudregistration` 保留为兼容 INTERFACE，转发到新 target。
- 职责重划：模块改为平面/ROI 点匹配与 `robot_base` 完整回传。新增正式接口 `pcv::matching::{RoiMatchInput, RoiMatchResult, matchRoiToRobotBase(...)}`（`include/pcv/matching/roi_matching.h`）：校验 ROI 索引、平面来源、点数量与有限性；经 50 坐标转换生成 `robot_base` 点并保留原始点索引；fail-closed（任一 ROI 点无法合法回传则不发布不完整结果）；返回完整性统计、丢失点数、覆盖率、取消状态与失败原因。
- seam fusion 实现与公共类型迁至 30 模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；本模块 `include/pcv/registration/seam_fusion*.h` 降为转发头。本模块不再负责 seam fusion 编排与最终拼接输出。
- `registerRobotBaseFrames()`/ICP/结构校验等底层能力保留（兼容接口，内部逐步转向 matching API）；`multiframe_registration` 位于本模块 `include/pcv/registration/multiframe_registration.h`。

### 2026-09-04（报告元数据真实化）

- `RobotBaseFrame` 增加 `flangeFromDepth`；`registerRobotBaseFrames()` 生成的 `FrameTransformMetadata` 现复制真实的 `flangeFromDepth`/（既有）`signedTravel`/`dominantTravelAxis`，修复 stitching_report 中 `flange_from_depth=identity`、`signed_travel=0`、`dominant_travel_axis=-1` 的失真（此前 GUI 路径在 `mergePlyCloudsInWorld` 组装 moduleFrames 时未携带这些值）。
- 说明：`stitching_report.json` 新增 `orientation_check`（净旋转 Z 系数、台面高度、台面上下采样分布、`suspected_upside_down`），用于真机核对输出是否上下颠倒。
- 验证：本会话沙箱禁止外部程序，未编译；待 Debug 构建 + `multiframe_registration_tests`/`seam_fusion_tests`/全量 CTest 与真实数据验收。

### 2026-09-03

- 崩溃加固：`IcpOptions` 增加共享 `cancellationToken`，ICP 优先读取稳定的 `std::shared_ptr<std::atomic_bool>`；GUI 任务将同一 token 贯穿配准任务，保留 `std::function<bool()>` 兼容入口。
- retry 内存收敛：`registerPair()` 的 source 保持只读，严格档和放宽档不再提交失败中的临时 sample；覆盖率改善使用已有诊断结果，避免 retry 额外复制整帧点云。
- 验证：Debug 目标构建成功，`multiframe_registration_tests` 连续运行 3 次通过，全量 Debug CTest `17/17` 通过。尚未使用真实夹具回归，崩溃 UAF 仍需 GUI 重放或 ASan/PageHeap 进一步确认。

- 取消令牌生命周期契约：`cancellationToken` 别名移至 `seam_fusion_types.h`，`SeamFusionOptions` 增加同款 token 并统一走 `cancellationRequested()`；GUI `m_cancelRequested` 改为窗口生命周期单令牌（构造函数创建、开始任务仅 `store(false)` 不再替换对象），开始门卫改为 `m_watcher` 非空即拒绝重入，`stitchingFinished` 先 disconnect 再清理，消除“上一任务 finished 未处理时重入/双 finished/令牌 use-after-free”；模块 30 接口路径在入口自建内部 token，并由注册进度回调同步置位，使所有热循环只读稳定原子、不再依赖调用方 std::function 生命周期。取消语义不变：阶段边界生效。
- 验证：Debug 目标构建 + `multiframe_registration_tests`/`seam_fusion_tests`（含 token 取消用例）通过；GUI 启动冒烟通过；真实数据连续/取消/快速重开场景仍待用户数据验收。

- 配准 pair 改为原子提交：严格档和放宽档均从同一原始 sample 副本运行，只有最终 accepted 才提交 sample、full 和全局修正。
- 增加一次性放宽重试：仅针对安全边界、主平面平移或结构验收失败触发；放宽上限按稳健初始误差计算并限制在 `50 mm / 3 deg`，仍要求三层收敛、重叠点数、可观测自由度、修正预算和覆盖率护栏全部通过。
- 结构验收增加低点数分级：`100~499` 点使用双向 `3 mm >= 0.45` 且 `5 mm >= 0.80`，结构点不足时允许 `+-2 mm` 平面带二次验收；严格档默认值保持不变。
- `stitching_report.json` 和 regression report 增加 `attempt_count`、`primary_attempt_reason`、`relaxed_retry_*`、`accepted_by_retry`、稳健初始误差及双向 `5 mm` 覆盖率字段。
- 验证：Debug 全量构建，`multiframe_registration_tests` 通过；全量 CTest `17/17` 通过。尚未接入真实夹具回归，位姿图优化暂未实现。

## 变更记录

### 2026-08-31

- M5.4 收尾：诊断工具改为直接消费本模块经 `pcv_m30_pointcloudstitch` 暴露的统一接口，ICP/seam diagnostics 字段保持原输出语义。
- 验证：Debug/Release 全量 CTest 均为 14/14；工具 Debug 构建通过。
- 风险：旧兼容 target `pcv_registration` 仍保留，待弃用周期和生产引用审计完成后单独删除。

- M5.1：`pointcloudstitch` 的生产路径在手眼转换后统一调用 `registerRobotBaseFrames()`，ICP、相邻帧诊断和结果模型由本模块提供；应用层旧实现仅作为待清理兼容代码保留。
- 验证：`pointcloudview`、`pointcloudstitch` Debug 构建成功，完整 Debug CTest `14/14` 通过。
- M5.4：已删除应用层 `registerPair()`、旧 ICP 循环及其诊断辅助函数；`pointcloudstitch` 保留读取/手眼转换后直接转交 `RobotBaseFrame`。模块实现成为唯一生产配准算法来源。
- `registration_diagnostic` 已改为编译 `pointcloudstitch` 兼容处理器并链接 `pcv_m10_pointcloudread`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`；不再直接编译 `pointcloudview` processor。

### 2026-08-31

- 建立模块边界和兼容入口；配准实现暂留 `src/registration`。
- M3：多帧 ICP 与接缝融合源码已迁移到 `modules/40_pointcloudregistration`，旧 `pcv_registration` 保留兼容聚合。
- M5.1：共享 `seam_fusion` 已从固定禁用占位改为真实投影重叠检查、融合带裁剪和候选最近邻插值；应用层入口保持兼容包装。当前仍需补充多帧、无重叠、取消和 bounded-memory 回归。
- M5.1 接口收敛：`pcv_m30_pointcloudstitch` 已允许启用共享 seam fusion；无真实投影重叠按 fail-closed 保留点云，真实重叠但无互相对应点仍失败并阻止正式输出。

### 2026-08-31（M5.4 兼容层审计）

- 生产引用仍包括 `apps/pointcloudstitch/pointcloudprocessor.cpp` 的独立 `registerPair()`/`mergePlyCloudsInWorld()`，以及 `tools/registration_diagnostic` 对该处理器的直接编译。
- `pcv_registration`、`pcv_io`、`pcv_output` 仍被兼容 CMake 聚合 target、旧公共头和测试引用，当前不满足删除条件。
- 下一步必须先统一应用处理器与 `pcv_m40_pointcloudregistration` 的结果类型，再迁移诊断工具和测试，之后才能单独提交兼容层删除。
- seam fusion 增加处理中途取消检查；取消发生在输出提交前，原始点云映射保持不变，不发布部分融合结果。
- 融合带中的未匹配点现在显式保留，并累计到 `SeamFusionDiagnostic::unmatchedPreserved`；只有成功匹配的点生成插值结果。
- 应用 `seamfusion.h/.cpp` 改用 `pcv::SeamFusionOptions/Result/Diagnostic` 类型别名，包装层不再复制结果模型。
- 应用 `pointcloudprocessor.h` 已改为直接复用 `multiframe_registration.h` 的 `IcpOptions`、`IcpDiagnostics`、`FrameTransformMetadata` 和 `MultiFrameRegistrationResult`；仅保留兼容函数入口和 `WorldCloudInput` 别名。
# 40_pointcloud_matching：工件位置确认（规划占位）

正式声明：对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

当前状态：**规划目标，未实现**。本模块当前不实现基准模板持久化、当前扫描与基准扫描比较、移动阈值和 6DoF 偏差判定、机器人执行或坐标修正下发，也不产生新的正式输出闭环。`pcv_m40_pointcloud_matching` 仅聚合 30/60 的兼容能力，旧 registration/matching header 仅转发。正式设计和 UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准。
