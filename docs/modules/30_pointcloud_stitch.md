# 30_pointcloud_stitch：多帧拼接（唯一编排模块）

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 当前算法版本（2026-10-09 回滚）

按用户要求恢复 `6329426`（2026-09-18 09:27:46 +08:00）的拼接算法。模块 CMake、`multiframe_registration.h/.cpp`、`stitching_interface.h/.cpp`、多帧与接口测试源码直接由 Git 恢复；seam 实现在该提交后未变化。移除后续新增的 heightmap 源/头与 `pose_overlap_fine_tests` 注册。GUI 恢复两种模式，默认“配准与融合”，回归工具移除后续 heightmap/fine/interface-pipeline 参数。

保留共享模块 50 当前手眼数学与应用调用统一入口，并保留已有输出覆盖确认；未回滚其他模块或项目提交历史。以下 2.5D/快速精配准记录为历史实现，不代表当前能力。本轮实际验证与风险在文末追加。

### 2026-10-08（位姿粗配准＋重叠区快速精配准，已回滚）

- 主模块 30，复用 50 坐标转换及 10 `ArtifactTransaction`；40 不新增实现。`IcpOptions.profile` 默认为 `RegistrationProfile::Legacy`，GUI 默认新 `PoseOverlapFine`，旧完整配准和仅位姿转换入口保留。
- 新路径只在原 robot_base 相邻帧样本的重叠区域执行两层 point-to-plane ICP：体素 `0.5/0.2 mm`、距离 `3.0/0.6 mm`、迭代上限 `20/15`；连续三次增量 `<=0.005 mm/0.0005°` 提前结束。法向邻域至少 `1.5 mm`，避免细层体素小于输入间距时无法估计法向。特征值阈值仍为 `1e-3`，只解可观测方向；低对应比例/退化仅警告。
- 包围盒候选窗增加首层匹配半径容差，允许水平面的零厚度 Z 范围；有效求解仍需至少 100 个真实近邻对应。每对 `10 mm/1°`，越界、无重叠或无解弃用修正，当前帧恢复原机器人位姿并重置链锚点。成功矩阵按 `C[i]=C[i-1]*P[i]` 顺序累计，样本不累计变换，整帧只变换一次；累计不设上限但记录矩阵/漂移。
- 跳过平面身份、平面预对齐、粗/宽搜索、恢复、2.5D、旧验收和 ICP 诊断帧；新模式不执行 seam fusion。`accepted` 不冒充旧验收通过；新字段 `fineApplied/poseFallback/chainAnchorReset/fineReason/pairCorrection/cumulativeCorrection` 区分计算与质量。
- `artifacts.writePreview` 兼容默认 `true`，新 GUI profile 设 `false`。正式输出仍为 `stitched_robot_base.ply`、`.pcvmap`、`stitching_manifest.json`，全量有效点、点序和来源映射保留。报告 `processing_mode=pose_overlap_fine`、`quality_policy=warn_only`、`strict_validation_status=not_run`、`structural_metrics_status=not_computed`；逐对保存矩阵、层数、迭代/对应/残差、退化及回退，汇总完成/回退数量。
- 并行 worker 沿用 const 视图和私有结果、顺序提交。文件/标定/位姿/非有限/映射、内存、取消和事务失败仍为硬失败；GUI 写入已有结果前统一确认覆盖，不修改窗口生命周期。
- 回放工具增加 `--pose-overlap-fine` 和 `--interface-pipeline`，走模块 30 的事务输出；同配置对照可用后者或结合 `--transform-only`，新接口回放不自动覆盖已有输出。
- 本轮验证结果见文末追加的最终验收记录。真实 `D:\stitch_test` 只有原 PLY/XML，位姿 TXT 和历史回放报告缺失，不能据此声称 `1/5_40/6_50` 效果或速度已验收。双平面、局部误配与累计漂移仍可能出现，正式输出成功不代表严格几何验证。

状态：已实现。本模块为多帧拼接的唯一编排模块：读取（10）、坐标转换调用（50）、多帧 ICP/结构校验、seam fusion、拼接诊断和 manifest 均在此收敛；最终文件统一通过 10 的 `pcv::io::ArtifactTransaction` 提交。规范实现位于 `modules/30_pointcloud_stitch`，模块 target 为 `pcv_m30_pointcloud_stitch`；旧 target `pcv_m30_pointcloudstitch` 保留为迁移期兼容入口（仅转发）。

seam fusion 实现与公共类型位于本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；模块 40 的 `include/pcv/registration/seam_fusion*.h` 仅为转发头。

接口：分组式 `pcv::interface::StitchingRequest`（`input`、`registration`、`seam`、`artifacts`）、`StitchingResult`、`stitch()` 和 `applyTrajectorySeamFusion`。旧 `StitchingOptions` / `stitchRawLineProfiles` 为弃用兼容入口。测试：`stitching_interface_tests`、`multiframe_registration_tests`、`seam_fusion_tests` 和并行等价测试。应用只负责输入组织、进度、窗口状态和结果展示。

最后核对日期：2026-10-09（文档静态核对）。本文件历史正文中出现的旧名称与旧职责表述为当时状态，仅用于追溯。2026-09-17 之后的验证结论必须单独记录；本轮文档同步未运行 CTest。

## 当前实现与兼容待办（2026-10-09 静态核对）

`StitchingRequest/stitch()` 已公开，但内部仍组装 `StitchingOptions` 并调用旧实现；反向适配尚未实施。`registrationdialog` 和 `registration_diagnostic` 仍用旧 Options/入口，工具 CMake 仍链接旧 10 与 40 聚合 target。算法头位于 `pcv/stitching/`，实际 namespace 主要是 `pointcloud::`；目标 `pcv::stitching` 门面不是全部已落地。

现有 30 入口会编排 10 的事务，不能描述为全部只返回 manifest。2026-10-09 算法已恢复到 `6329426`，API/GUI 使用原完整配准流程；快速模式字段与输出策略已回滚。

`pose_overlap_fine_tests` 注册已随算法回滚移除；历史重复回归结论不代表本次验证。收敛顺序见 [开发方案](../development/module_convergence_v0.4.md)，具体测试以本轮追加记录为准。

## 历史变更与验证记录

### 2026-09-18（2.5D 高程图配准受控迭代）

- 需求登记：主模块 `30_pointcloud_stitch`，配套公共匹配类型仍由 40 的规划占位承载；未修改 `modules/50_coordinate_conversion`、手眼矩阵、机器人 `[X,Y,Z,A,B,C]` 语义、原始 PLY 或 `workpiece_list.json`。
- 新增 `pcv::stitching::HeightMap` / `HeightMapRegistrationResult`（`heightmap_registration.h/.cpp`）：按机器人初值重叠框与候选框交集构建 2.5D 高程图，保存有效掩膜、网格计数、源点索引、原始/去趋势高程、局部范围和网格原点；分辨率由中位间距估计并限制在 `0.5~2.0 mm`，异常返回 `heightmap_grid_resolution_invalid` 或 `heightmap_insufficient_data`。
- 搜索为无外部依赖的受限 coarse-to-fine：第一阶段仅 `Δx/Δy`，第二阶段仅小范围 `Δz/yaw`，严格预算 `10 mm/1°`；使用有效重叠鲁棒高程残差、有效掩膜和候选唯一性/实际重叠检查，保留轨迹诊断和取消语义。不引入 pose graph、硬件时间戳或 `Δt` 估计。
- `IcpOptions.heightMapRegistrationEnabled` 默认 `false`，新增网格、有效比例/点数、改善率、预算、受限自由度等字段均追加在结构体末尾；`IcpDiagnostics` 追加 2.5D 尝试、评分、网格、受限 ICP、回退、来源和拒绝字段。开关关闭时沿用旧 ICP。
- 开关开启时每个相邻 pair 同时保留 legacy baseline：2.5D 通过全部新验收且受限 ICP 三层完成时来源为 `heightmap_restricted_icp`；2.5D 失败且 legacy 通过时为 `legacy_icp`；两者失败为 `none`。最终有效可见比例 `<0.25` 优先硬拒绝，报告 `visibility_hard_rejected=true`，不得以命中点内覆盖率放行。
- 应用层 `stitchingwindow.cpp` / `regressionrunner.cpp` 仅透传和序列化上述字段，并显示“2.5D 配准通过 / 失败回退旧 ICP / 低可见性拒绝 / 候选歧义 / 受限 ICP 退化”等状态；未改变窗口生命周期、异步调度或取消对象管理。
- 验证：Debug 定向构建 `cmake --build C:\pcv-build\heightmap-iteration-debug-v1 --config Debug --target multiframe_registration_tests stitching_interface_tests pointcloudstitch` 成功；定向测试 2/2 通过；Debug 全量 CTest 15/15 通过。Release 定向构建 `C:\pcv-build\heightmap-iteration-release-v1` 的三个目标成功，定向测试 2/2 通过。
- 真实回放（源码树外 `D:\pcv-replay\heightmap-v1`，输入为原始 PLY 同盘硬链接，未覆盖原始数据）：`1/test_1` 报告 `heightmap_registration_report_v2.json` 成功，网格 `1.91765 mm`、有效比例 source/target=`0.5623/0.7987`、有效点=`6709/9530`、评分改善 `10.42%`，但受限 ICP 标记退化（3 层完成），故安全回退 `legacy_icp`，生成正式 `regression_registered_robot_base.ply`；`5_40/test` 网格 `1.93265 mm`、有效比例=`0.6691/0.8358`、评分改善 `5.47%`，legacy baseline 虽报告可用，但最终比例 `0.2246` 触发 `visibility_hard_rejected`，来源 `none`，未生成正式合并 PLY。
- Release 部署未做独立便携验收：构建环境未找到 Visual Studio redist 子目录；GUI 人工冒烟、PageHeap、真实生产夹具和现场同步仍未验证。

### 2026-09-17（配准新选项透传与失败信息细化）

- 需求登记：主模块 `30_pointcloud_stitch`（配套 40 模块的验收口径与鲁棒性改动）。
- `pcv::interface::StitchingOptions` 增加透传字段：`structuralCoverageOfMatched`、`minimumStructuralMatchedRatio`、`visibilityWarningRatio`、`tangentCoarseTrimmedScore`、`correspondenceParallelTasks`；`stitchRawLineProfiles` 在组装 `IcpOptions` 时逐项赋值，默认值与 `IcpOptions` 保持一致，不改动既有调用方的行为。
- 失败信息：帧配准未通过时，`message` 现在同时给出机器可读 `failureCode`、原因文本、双向命中率与命中点内双向 3 mm 覆盖率，用于区分“数据不可比（视场重叠很少）”与“算法没对好”。
- 接缝融合本轮无功能改动：现网报告 `seam_fusion_ms=0`（未启用），按方案先测量再决定是否另立子项；`decisionCellSize` 维持“仅校验、保留位”。
- 验证：Debug 构建与全量 CTest 通过（`stitching_interface_tests`、`seam_fusion_tests` 未回归）。
- 风险：新选项默认值即使与 40 模块一致，仍需在真实数据回放中确认 `1/2` 组判定与修正不漂移。

### 2026-09-15（seam fusion 并行路径 Qt 隐式共享容器并发 detach 修复）

- 现象：PointCloudStitch 间歇性 `0xc0000005` 读访问违规，读取地址为 `0xFFFFFFFFFFFFFFFF`，表面故障点在 `QVector<SeamFusionDiagnostic>` 扩容。按“上游堆损坏”处理，未修补崩溃行。Windows 事件日志中 `pointcloudstitch.exe`（2026-09-03/07/08/15）与 `seam_fusion_tests.exe`（2026-09-03 多次）存在同类 `0xc0000005` 历史记录。
- 根因：`applyTrajectorySeamFusionKernel` 以 `merge.points = *points` 形式与调用方共享容器（GUI 路径 `apps/pointcloudstitch/seamfusion.cpp` 正是此入口），随后并行 worker 通过**非 const** 的 `merge->points[...]`、`merge->cloudIds[...]`、`buckets[i]`、`centers[i]`、`directions[i]` 读取。Qt 的非 `const` `operator[]` 会 `detach()`；多个 worker 同时对同一个 refcount > 1 的 `QArrayDataPointer` detach 时，会并发改写同一个指针成员并对旧缓冲重复 deref，造成堆破坏，随后在任意分配点（这里是 diagnostics 扩容）表现为 `0xc0000005`。
- 动态复现：在未修复源码上运行新增回归用例（`seam_fusion_tests`，每次进程独立），连续 100 次中第 34 次以 `-1073741819`（`0xC0000005`）退出；其余 99 次通过。修复后同一用例 100 次全部通过。
- 修复：新增 `SeamWorkerView`（只持有 const 指针），在启动任何任务前一次性建立 `inputPoints/inputCloudIds/inputSourceIndices/inputScanRatios/inputFrameMetadata/inputCorrections` 与 `buckets/centers/directions/seams/usable/options` 的稳定只读视图；`buildSeamBand`/`buildCoreChunk` 的捕获由宽泛的 `[&]` 改为按值捕获该视图，worker 内只使用 `.at()`/`->at()` 等 const API。seam 与 core 两波并行都保留显式 join 屏障，`seams/left/right/usable/handled/diagnostics` 等共享结果全部在串行汇合阶段按确定顺序提交，`merge->*` 只在所有 future 结束后一次性改写。`result.diagnostics.reserve(...)` 仅作为确定性与防御性措施，不作为根因修复。
- 行为不变：输入长度校验、`cloudId` 范围校验、取消粒度与取消语义、失败回滚、输出点序与来源映射、串并行结果一致性、`SeamFusionDiagnostic` 字段与 JSON schema 均未改变；未引入悬空引用。
- 测试：`seam_fusion_tests` 新增 9 帧重叠链夹具（每帧 40 × 200 点，合并 72000 点，跨 2 个 core chunk、8 条 seam），并刻意保留额外共享副本使 Qt refcount > 1。覆盖 `parallelEnabled=false` 与 `maxParallelTasks=1/2/4` 的点数/点序/来源映射/全部诊断字段一致性、重复调用确定性、共享输入缓冲未被改写、并行途中取消、无重叠结构化拒绝、空融合带、以及 30° 倾斜链的“成功或结构化失败”。

### 2026-09-07（第二轮性能优化 P1：seam 每 cloud 索引与接口选项）

- seam fusion：新增 SeamFusionResult::seamIndexBuildMs（按 seam 右侧融合带网格构建累计）。按 cloud 一次性建立 mutualDistance 空间索引并跨 seam 复用的变体已实现并以真实数据（438 万点合并、3 帧）评测：线性链中每 cloud 只服务一条 seam，整 cloud 索引构建 358 ms 明显高于按带网格（约 20 ms 量级），属净回归，已回退并保留按带网格实现（带内候选语义与点序不变）；若未来出现同一 cloud 参与多条 seam 的 X/Y 拓扑，每 cloud 索引才值得重估。
- 接口选项：pcv::interface::StitchingOptions::collectDiagnosticFrames（默认 true 兼容）透传至 40 模块 IcpOptions，标准输出调用方可关闭诊断帧收集。
- 验证（手工 cl 构建）：seam_fusion_tests 连续重复通过（含中途取消输入不被修改用例）；stitching_interface_tests 编译链接通过（沙箱 QTemporaryDir 限制仍阻断运行）；GUI 报告新增 timings 键（target_level_cache_ms/cache_build_ms/correspondence_ms/boundary_check_ms/diagnostic_frame_ms/seam_index_build_ms/formal_output_write_ms）与 diagnostic_frames_collected，uic+cl 语法编译通过。
- 风险：每 cloud 索引在带占比小且多 seam 场景可能高于原按带网格成本，待生产多帧数据复核；输出文件格式写入仍以模块 10 writer 为唯一权威，formal 输出与 pcvmap/preview 的完全单遍融合需流式 writer API（超出本轮模块范围）。
### 2026-09-07（seam fusion 分桶优化与接口计时）

- 阶段 4 seam fusion：applyTrajectorySeamFusionImpl 改为按 cloudId 一次分桶（buckets），每条 seam 只遍历相邻两个 cloud（投影范围、融合带均从桶内计算），keep/核心点单次全量扫描内联判定，每条 seam 的 matched 标记改为左/右融合带局部数组（不再为每条 seam 分配全量点数 QVector<bool>）；保持 halfWidth/mutualDistance/decisionCellSize、真实投影重叠判定、未匹配点保留与 fail-closed、取消粒度与输出点序语义不变。
- 阶段 1 计时：SeamFusionResult 增加 seamFusionMs；pcv::interface::StitchingResult 增加 readMs/transformMs/registrationMs/seamFusionMs/outputMs/totalMs，stitchRawLineProfiles 累计并在 diagnostics 附加 timings_ms 汇总行（含模块 40 各阶段聚合）。
- 验证（手工 cl 构建）：seam_fusion_tests 连续 8 次通过（含中途取消输入不被修改用例）；stitching_interface_tests 编译链接通过，沙箱内 QTemporaryDir 限制导致运行不可用（环境限制，非代码回归）；GUI pointcloudprocessor.cpp/seamfusion.cpp/stitchingwindow.cpp uic+cl 语法编译通过。
- 风险：输出点序/计数与旧实现的等价性通过 seam 单测核对有限；用户环境全量 CTest 待复验。
### 2026-09-06（模块重命名与 30/40 职责重划）

- 目录重命名：`modules/30_pointcloud_stitch`，target 改为 `pcv_m30_pointcloud_stitch`；旧 `pcv_m30_pointcloudstitch` 保留为兼容 INTERFACE，转发到新 target。
- 本模块成为多帧拼接的唯一编排模块：读取、坐标转换调用、多帧匹配调用、seam fusion、拼接诊断、正式输出与失败回滚均在此收敛。
- seam fusion 实现与公共类型迁入本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；模块 40 的 `include/pcv/registration/seam_fusion*.h` 降为转发头。
- `40_pointcloudregistration` 更名为 `40_pointcloud_matching`，改为平面/ROI 点匹配与 `robot_base` 完整回传，不再负责 seam fusion 编排与最终拼接输出。

### 2026-09-04（元数据补全）

- `stitchRawLineProfiles` 路径为 `RobotBaseFrame` 填入 `flangeFromDepth = calibration.flangeFromDepth`，配合模块 40 使报告/诊断记录真实标定矩阵。
- 验证：未编译；待 Debug 构建与 `stitching_interface_tests`。

### 2026-08-31（M5.1 seam pipeline）

- M5.4：`registration_diagnostic` 已改用 `pcv::interface::stitchRawLineProfiles`，工具不再编译应用 processor；固定四帧输入、no-seam/seam 两套输出均由同一接口流水线执行。
- 验证：工具 Debug 构建通过；真实夹具运行和 GUI/接口逐字段比对待现场数据。

- 移除 `seamEnabled` 的“暂时禁用”入口占位判断，使 `stitchRawLineProfiles` 可以调用共享 seam fusion。
- 无真实投影重叠时允许 fail-closed 保留完整点云并返回诊断；真实重叠但无可用互相对应点时仍返回 `PCV_STITCH_001`，禁止生成正式输出。
- 验证：接口错误安全测试覆盖启用 seam 时的非法 PLY 输入和正式输出不被替换。
- 应用 seam 适配器已改用 `pcv_m40_pointcloudregistration` 的共享结果类型别名，保留旧包装入口但移除第二套数据模型。
- `stitchingwindow.ui` 已移动至 `apps/pointcloudstitch/ui/`，CMake AUTOUIC 继续生成 `ui_stitchingwindow.h`。
- 新增 `pcv::stitching::WorldCloudInput` 纯数据 DTO；应用 `pointcloud::WorldCloudInput` 改为兼容别名，建立拼接输入类型的模块所有权。

### 2026-08-31（M5.1 输入 DTO 下沉）

- `WorldCloudInput` 已移入 `modules/30_pointcloudstitch/include/pcv/stitching/world_cloud_types.h`。
- 应用头仅保留 `pointcloud::WorldCloudInput` 兼容别名；`pointcloudstitch` Debug 构建和完整 CTest（14/14）通过。

接口/测试：`stitching_interface`、`applyTrajectorySeamFusion`；`stitching_interface_tests`、`seam_fusion_tests`。

### registration_diagnostic 通用输入

工具支持 `registration_diagnostic stitch`（省略子命令时保持兼容）以及 `inspect`。拼接可重复传入 `--input <file>`，或使用 `--input-dir <directory>` 扫描当前目录 `*.ply`；两者可混用并按规范化绝对路径去重。每个文件名需包含 `A<n>` 扫描编号，并通过 `--pose-info` 的 `A<n> Start ... ; END ...` 行匹配 `[X,Y,Z,A,B,C]` 位姿。`inspect` 支持 `--json`，报告原始 `format`、`requires_conversion` 和固定的 `canonical_format=binary_little_endian`；拼接入口仅在 canonical Binary 准备完成后继续。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m30_pointcloudstitch` 兼容入口；保留现有应用流程。
- M3/M4：`stitching_interface` 已迁移到模块源码，target 负责流程编排并依赖配准、坐标转换和读写模块。
- M5.1：应用 seam fusion 入口已增加 `ok/error/cancelled` 状态并在正式输出前 fail-closed；GUI 仍保留应用数据模型适配，待公共拼接类型完全收敛后删除适配层。

### 2026-10-09（PointCloudStitch 算法定向回滚到 6329426）

- 主模块 30；用户明确授权 Git 定向回滚。起始工作区干净，基线为 `6329426`（2026-09-18 09:27:46 +08:00），使用 `git restore --source=6329426 --worktree -- <paths>`，不重置项目历史。
- 模块 CMake、公共头、multiframe/stitching 实现、UI 模式文件及两份拼接测试恢复基线；seam 算法本就与基线一致。删除后续新增 heightmap 源/头，移除 pose_overlap_fine 专属测试注册与 heightmap/fine/interface-pipeline 回放参数。GUI 恢复完整配准/仅手眼转换两种模式。
- 必要差异：保留模块 50 当前统一手眼实现、regressionrunner 参数适配和 GUI 报告的统一坐标组合调用，保留已有输出覆盖确认。其他模块、其他测试注册及历史文档记录保留。同步应用/模块 README、v0.3/v0.4 契约、协同文档、开发方案与 target.md。
- 验证：`C:\pcv-build\stitch-rollback-6329426-debug-v1` 完整 Debug 构建成功；定向 stitching_interface/seam_fusion/multiframe 测试 `3/3` 通过，全量 CTest `19/19` 通过。核心模块文件、UI 与对应测试对 `6329426` 的 `git diff --exit-code` 为 `0`，`git diff --check` 通过。
- 启动冒烟：首次指定 offscreen 时 pointcloudstitch 停在 Runtime Library 弹窗，未算成功；结束本轮测试进程后，显式使用当前 Qt plugins/platforms 与 windows 平台重试，pointcloudstitch/pointcloudview 自关闭启动退出码均为 `0`。未影响已有用户运行的 pointcloudview。
- 未验证：Release 构建/部署、真实 PLY/XML/Start-End 回放与 GUI 全流程、现场质量/性能/机器人安全。模块 50 保持当前版本，因此不是整个项目逐字回退；恢复旧算法也恢复其既有质量/性能限制。
- 回滚点：本轮前完整版本仍为 `ca0adc7`。本轮作为独立本地 Git 提交保存，不进行远程上传或强制推送。