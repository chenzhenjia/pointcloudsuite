# 30_pointcloud_stitch：多帧拼接（唯一编排模块）

状态：已实现。本模块为多帧拼接的唯一编排模块：读取（10）、坐标转换调用（50）、多帧 ICP/结构校验、seam fusion、拼接诊断和 manifest 均在此收敛；最终文件统一通过 10 的 `pcv::io::ArtifactTransaction` 提交。规范实现位于 `modules/30_pointcloud_stitch`，模块 target 为 `pcv_m30_pointcloud_stitch`；旧 target `pcv_m30_pointcloudstitch` 保留为迁移期兼容入口（仅转发）。

seam fusion 实现与公共类型位于本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；模块 40 的 `include/pcv/registration/seam_fusion*.h` 仅为转发头。

接口：分组式 `pcv::interface::StitchingRequest`（`input`、`registration`、`seam`、`artifacts`）、`StitchingResult`、`stitch()` 和 `applyTrajectorySeamFusion`。旧 `StitchingOptions` / `stitchRawLineProfiles` 为弃用兼容入口。测试：`stitching_interface_tests`、`multiframe_registration_tests`、`seam_fusion_tests` 和并行等价测试。应用只负责输入组织、进度、窗口状态和结果展示。

最后核对日期：2026-09-17。本文件历史正文中出现的旧名称与旧职责表述为当时状态，仅用于追溯。2026-09-17 之后的验证结论必须单独记录；本轮文档同步未运行 CTest。

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
