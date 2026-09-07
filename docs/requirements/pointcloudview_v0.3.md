# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
最后核对日期：2026-09-07
状态：核心模块稳定（模块重命名与 30/40 职责重划完成），相机扩展实验中，发布验收待完成

## 1. 目标和事实来源

v0.3 是 PointCloudSuite 当前唯一统筹基线。项目按十个固定编号模块治理；源码、CMake 和测试优先于历史文档。

当前未实现模块为 `90_interferenceplane` 和 `100_qualityreport`。本版本不为这两个模块建立功能 target，也不报告虚假成功。

## 2. 模块基线

| 模块 | 职责 | 规范 target | 状态 |
|---|---|---|---|
| `10_pointcloud_read` | 点云输入（PLY 读写、缓存、校验）；相机帧采集与转换（原 05 已并入） | `pcv_m10_pointcloud_read` | 已实现；相机为可选实验能力 |
| `20_pointcloud_render` | Qt/OpenGL 画布、VBO、点选 | `pcv_m20_pointcloud_render` | 已实现，桌面验收待完成 |
| `30_pointcloud_stitch` | 多帧拼接唯一编排：读取、坐标转换/匹配调用、seam fusion、正式输出与回滚 | `pcv_m30_pointcloud_stitch` | 已实现 |
| `40_pointcloud_matching` | 平面/ROI 点匹配、覆盖校验、`robot_base` 完整回传；保留 ICP/结构校验底层能力 | `pcv_m40_pointcloud_matching` | 已实现 |
| `50_coordinate_conversion` | 手眼标定、位姿插值、坐标转换（唯一数学实现） | `pcv_m50_coordinate_conversion` | 已实现 |
| `60_plane_fitting` | 三点/n 点平面算法和诊断 | `pcv_m60_plane_fitting` | 已实现 |
| `70_roi_template` | ROI、模板、临时工件接口（ROI 匹配改调 40 统一接口） | `pcv_m70_roi_template` | 已实现，部分 UI 编排保留 |
| `80_plane_output` | PNG、PLY、JSON 成套输出 | `pcv_m80_plane_output` | 已实现 |
| 相机能力（原 `05_camerainput`） | 海康相机取流（实验扩展），已并入 `10_pointcloud_read` | 同 `pcv_m10_pointcloud_read` | 实验中，不纳入 v0.3 发布基线 |
| `90_interferenceplane` | 干涉平面检查 | 无 | 未实现 |
| `100_qualityreport` | 质量报告 | 无 | 未实现 |

迁移期兼容 target：旧模块名 `pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，以及 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`（仅转发）。兼容层在所有调用方迁移并完成独立审计前保留。

## 3. 数据流程

```text
10 输入/相机采集
    ↓
20 渲染与交互
    ↓
60 平面拟合
    ↓
70 ROI 生成
    ↓
40 ROI 点匹配并完整回传 robot_base
    ↓
30 多帧拼接、seam fusion、正式输出
```

单帧处理：输入校验 → `10` 读取/缓存 → `20` 渲染和点选 → `60` 平面拟合 → `70` ROI/模板 → `40` ROI 匹配（`robot_base` 完整回传，fail-closed）→ `80` 成套输出。

多帧处理：`30` 是唯一拼接编排：组织输入并读取 PLY，调用 `50` 转换与 `40` 多帧匹配，执行 seam fusion，成功后提交正式 PLY。取消或失败时不得发布部分结果或复用旧正式输出。

## 4. 稳定契约

- 长度单位为 mm，角度单位为 deg。
- 位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 平面输出为 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- PNG、PLY、JSON 先写入 staging 目录，全部成功后成套提交；任一失败必须回滚。
- 共享库不弹窗、不访问 QWidget；应用负责提示和线程生命周期。
- `PointCloudCanvas` 实现位于 `modules/20_pointcloud_render/src/pointcloud_canvas.cpp`。
- 诊断工具通过 `pcv::interface::stitchRawLineProfiles` 使用统一拼接接口。

详细字段和错误码以模块公共头、模块文档和测试源码为准。

## 5. 开发和验收

每次需求登记主模块编号，修改公共头、CMake 或跨模块接口时列出影响模块和回归测试。模块文档追加日期、需求、文件、行为、验证和风险。

验证顺序：模块测试 → 直接依赖构建 → 应用构建 → Debug/Release 全量 CTest → GUI 或真实夹具验收 → 文档和 Git 检查。

当前自动化测试用于验证共享算法和接口。Windows 桌面点选、矩形选区、旋转/平移/缩放、关闭生命周期以及真实生产拼接夹具仍需单独验收。

## 6. 兼容层清理条件

只有在所有公共 include 和生产调用方切换到规范模块路径、诊断工具仅使用模块 API、应用不再包含重复算法、Debug/Release 构建和全量 CTest 通过，并完成 GUI/夹具验收后，才能单独删除兼容 target 和 forwarding header。

## 7. 发布状态

当前状态为“迁移完成，发布验收待完成”。未完成真实 GUI 验收和生产夹具逐字段比对前，不标记为 Release Candidate。

## 8. 变更记录

### 2026-09-07（PointCloudStitch 配准与拼接耗时优化）

- 登记主模块 `30_pointcloud_stitch`、`40_pointcloud_matching`，应用配套为 `apps/pointcloudstitch`。本次仅优化多帧配准/拼接路径及其诊断输出，不改变手眼坐标转换、`robot_base` 输出坐标系、点源映射和 seam fusion 参数合同。
- `40_pointcloud_matching` 增加配准阶段耗时统计：`IcpDiagnostics` 记录预处理、粗搜索、三层 ICP、结构校验、严格尝试和最终尝试耗时及各层迭代次数；`MultiFrameRegistrationResult` 聚合预处理、粗搜索、各 ICP 层、结构校验、retry 和注册总耗时。严格尝试与 relaxed retry 共用 `PairPreprocessCache`（crop、平面、距离诊断、分层采样基点）；step factor 试算复用 scratch buffer，变换与重叠计数合并并支持达到阈值提前退出；每层增加无改善提前收敛，最终安全/结构验收保持不变。
- 采用平衡模式默认值：`tangentCoarseAlignmentEnabled=false`、`wideStructureDiagnosticEnabled=false`；新增 `tangentCoarseAlignmentAuto`，仅在机器人初值未满足现有覆盖/重叠条件时自动触发切向粗对齐。relaxed retry 仅针对 `safety_boundary`、`prealignment_boundary`、`structural_validation` 触发，取消、无重叠和输入错误不重试；诊断模式可显式重新开启完整粗搜索。
- `30_pointcloud_stitch` 的 seam fusion 改为按 `cloudId` 一次分桶，每条 seam 只处理相邻两帧点；融合带匹配标记使用局部数组，keep/核心点判定保持单次全量扫描。原有 `halfWidth`、`mutualDistance`、`decisionCellSize`、真实投影重叠、未匹配点保留、fail-closed、取消语义和输出点序保持不变。
- `pointcloudstitch` 增加“诊断输出模式”：标准模式不写逐帧来源及 pre/post ICP 诊断 PLY，诊断模式才写出；来源点按 cloud bucket 单次遍历生成，避免按帧反复扫描合并点云。`stitching_report.json` 新增 `timings` 汇总以及逐帧/逐层计时字段。
- 验证：`multiframe_registration_tests`、`seam_fusion_tests` 连续 8 次通过；真实 `robot_base` 数据（3 帧、约 90 万至 246 万点、`stride=8`）平衡模式与诊断模式修正矩阵逐位一致，wide 粗搜索由诊断模式约 414 ms/对降至平衡模式约 8 ms/对；阶段聚合耗时、逐对层级迭代和报告字段已核对。`stitching_interface_tests` 完成编译链接，当前沙箱因 `QTemporaryDir` 目录创建限制无法运行；GUI 仅完成 `uic+cl` 语法编译，未完成人工冒烟。
- 风险/待验收：用户环境需复验完整 Debug/Release CTest、GUI 标准/诊断模式切换、真实多帧 seam 输出点序/计数等价性，以及粗搜索和 retry 在生产夹具上的触发比例；历史 `seam_fusion_tests` 间歇性访问违规风险仍需持续关注。

### 2026-09-07（ROI 匹配接口收敛）

- `modules/70_roi_template/src/temp_workpiece_interface.cpp` 不再直接按索引复制 ROI 点，改为调用 `pcv::matching::matchRoiToRobotBase`；准备阶段已在 `robot_base` 的临时工件路径使用 identity transform 复用统一 fail-closed 校验，输出点数取匹配结果的 `matchedPointCount`。
- `pcv_m40_pointcloud_matching` 的 `RoiMatchResult` 继续提供完整性统计、覆盖率、缺失/拒绝计数、取消状态和错误原因；`roi_matching_tests` 纳入 CTest，覆盖有效输入及失败关闭路径。
- 本次仅同步源码事实和文档，未在当前会话重新执行构建或 CTest；GUI、真实相机和生产夹具验收仍未完成。

### 2026-09-06（模块重命名、相机并入 10 与 30/40 职责重划）

- 模块重命名：`05_camerainput` 并入 `10_pointcloud_read`；`10_pointcloudread`、`20_pointcloudrender`、`30_pointcloudstitch`、`40_pointcloudregistration`、`50_coordinateconversion`、`60_planefitting`、`80_planeoutput` 分别改为 `10_pointcloud_read`、`20_pointcloud_render`、`30_pointcloud_stitch`、`40_pointcloud_matching`、`50_coordinate_conversion`、`60_plane_fitting`、`80_plane_output`；旧 target 保留为迁移期兼容入口（仅转发）。
- 相机输入并入 10：相机公共头、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 `Mv3dLp` 实现位于 `modules/10_pointcloud_read`；`PCV_BUILD_EXPERIMENTAL_CAMERA` 仍为可选实验能力，无 SDK 时可构建纯函数转换与 Mock 路径。
- 30/40 职责重划：`30_pointcloud_stitch` 收敛为唯一拼接编排模块（读取、坐标转换/多帧匹配调用、seam fusion、诊断、正式输出与失败回滚）；seam fusion 公共类型与实现迁入其 stitching 目录；`40_pointcloud_matching` 改为平面/ROI 点匹配与 `robot_base` 完整回传模块，新增 `pcv::matching::{RoiMatchInput,RoiMatchResult,matchRoiToRobotBase}`（fail-closed、覆盖率与取消诊断），保留 `registerRobotBaseFrames()` 等兼容包装；`70_roi_template` 的 ROI 匹配改调 40 统一接口；50 仍是坐标转换唯一数学实现。
- 文档同步：docs 索引、架构概览、代码构成、模块文档与本文件同步新目录/target 名、相机并入说明、30/40 新职责、数据流与兼容层说明；历史研究文档仅修正仍被当作当前规范的引用并加“历史名称”说明。
- 验证：Windows Debug 三种配置（相机 OFF、相机 ON 无 SDK、相机 ON 真实 SDK）配置、构建与全量 CTest 通过；Release（相机 OFF）构建与 CTest 待执行。GUI 人工验收、真实生产夹具与旧 target 外部使用情况仍待独立验收。

### 2026-09-04（拼接报告元数据真实化与方向自检）

- 配准/拼接报告元数据真实化：`RobotBaseFrame` 携带 `flangeFromDepth`，GUI 路径补传 `signedTravel`/`dominantTravelAxis`，修复 `stitching_report.json` 中 flange=identity、signed_travel=0、dominant_travel_axis=-1 的失真；`stitching_report.json` 新增 `orientation_check`（净旋转 Z 系数 + 台面上下采样分布），辅助真机核对输出上下方向。
- 已知问题（本次未改拼接算法）：实测本机拼接输出净旋转 Z 系数为 -1（台面≈820.75 时工件主体 z≈716~819 位于台面下方，相对 +Z 向上约定整体上下颠倒），根源指向扫描位姿 A/B/C 与手眼标定 z 方向约定的组合，修正需真机判定（候选见 30/40 模块变更记录）。
- 验证：本会话沙箱禁止外部程序，未编译；待 Debug 构建、全量 CTest 与真实数据验收。

### 2026-09-04（渲染包围盒轴线和整数刻度）

- 深色 `PointCloudCanvas` 将 X/Y/Z 彩色轴固定到包围盒最小角点的三条正向边，三轴以该角点共点；交点不标识坐标数字，包围盒、网格和刻度只属于当前会话的显示层；
- 网格按包围盒归一化比例生成，每个坐标轴固定 10 个区间；轴标签使用加粗大字号，映射到实际 mm 坐标后显示为整数；点云数据、拾取索引、平面流程、PLY、JSON、PNG 和四件套输出契约不变。

### 2026-09-04（全量 Markdown 中文同步）

- 按当前源码、CMake 和测试同步全部项目 Markdown：补充 `pointcloudcli`、实验相机和 `registration_diagnostic` 的功能边界，统一模块 target、构建生成器和验收口径；
- 历史 v0.1/v0.2、交接、契约和研究文档保留原始版本语义，仅修正当前状态提示、失效路径和中文说明；
- 本次仅完成文档检查，未运行 Debug/Release 构建或 CTest，因此不新增本次构建或测试通过结论。

### 2026-09-03（CLI 构建路径骨架）

- 新增 `PCV_BUILD_CLI`（默认 `OFF`）和独立 `pointcloudcli` target；开启后可在关闭 `pointcloudview`、`pointcloudstitch` 的配置中单独构建 CLI，CLI-only 路径不配置 Qt 或共享模块，既有 GUI target、UI 和默认构建行为不变；
- CLI 首期仅提供 `--help`、`--version`、空参数及未知参数的稳定退出码契约，不接入点云读取、配准、拼接、临时工件、平面、文件输出或 Qt 事件循环；
- `pointcloudcli` 不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target；`PCV_BUILD_CLI=ON` 与 `PCV_BUILD_TESTS=ON` 时注册四项 CLI CTest；
- 验收范围：独立 CLI target 构建与参数测试，及默认 GUI 配置回归。GUI 人工操作和真实生产夹具不属于本项自动化验收。

### 2026-09（相机取流扩展，风险收敛）

- 登记实验模块 `05_camerainput`（`pcv_m05_camerainput`，历史名称：2026-09-06 起并入 `10_pointcloud_read`，见本模块变更记录）：`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，默认构建包含相机扩展，仍可显式关闭；SDK 存在时链接真实 `Mv3dLp`，否则保留 Mock 演示路径；
- `20_pointcloudrender` 新增 `PointCloudCanvas::setCloudStreaming`（实时流式刷新，保留覆盖层）；
- `apps/pointcloudview` 新增 `CameraDialog`（相机实时取流页面）与 `live_pipeline`（JSON pipeline 执行器，camera.nextFrame/filter.voxel/canvas.publish）；
- 实时路径仍为实验链路；`live_pipeline` 尚未接入生产相机链；
- 验证：实验开关 OFF 的 Debug CTest `13/13`，开关 ON 且 SDK 存在的 Debug CTest `17/17`；完整 SHA-256 缓存回归通过；
- 未验证项：真实相机 GUI、`SDKCoordinateType` 坐标映射、实机软触发/重连、关闭时序和 130 万点性能，均为发布阻断项。

### 2026-09（采集工作台 Designer UI）

- `apps/pointcloudview` 使用 Qt Designer 增加顶部工具栏、中央画布工作区及采集、清理、平面、输出四步导航；既有算法和输出契约不变；
- 实验相机对话框显示当前 `DeviceInfo` 字段和 Mock 源，未实现的相机参数以禁用控件显式提示，不纳入 v0.3 稳定能力；
- 图标资源仅服务 UI，不引入共享模块依赖。相机 GUI 使用 SDK 回调取流，默认请求 `Range_Image=7`；实时帧仅预览，冻结有效帧后进入既有点云处理和输出流程。

### 2026-09（全界面浅色主题补强）

- 主窗口与相机对话框补充嵌套面板、菜单、弹出列表、滚动条、SpinBox 和复选框的浅色样式，避免系统调色板导致深色残留；
- OpenGL 画布继续使用浅灰白清屏色和深色叠加文字，不改变渲染公共接口。

### 2026-09（浅色 UI + 深色 OpenGL 画布）

- 主窗口、工具栏、面板和对话框保持浅色视觉基线；中央 OpenGL 画布采用深色背景，以提高彩色点云、边缘和坐标轴的识别度；
- 画布叠加文字同步使用浅色高对比度，不改变渲染公共接口或数据契约。

### 2026-09-04（独立渲染设置窗口）

- `apps/pointcloudview` 新增 Qt Designer `RenderDialog`，通过工具栏打开“渲染 / 内容”双页设置窗口；原有点尺寸、颜色模式、透明度和映射范围与窗口双向同步；
- 支持坐标轴、XY 地板网格、坐标标签、法向量、包围盒和统计信息显示开关；设置仅作用于当前会话的 `PointCloudCanvas`，不影响 PLY、JSON、PNG 或四件套输出。

### 2026-09-01（当前差异风险收敛）

- P0：帧槽通知改为加锁 setter/clear；FrameSource 默认拒绝不支持能力；转换单飞、仅保留最新待处理帧，按 generation 丢弃旧结果；关闭先清通知并等待转换和 SDK callback；
- P0：真实异常回调映射 `FrameKind::Exception`、异常类型和描述；callback 注册返回码检查；SDK lease 保证一次 Initialize 对应一次 Finalize；
- P0：PLY 缓存恢复完整 SHA-256，缓存版本升至 3；同尺寸同时间戳的未采样区域修改必须 cache miss；
- P0：移除 `CMAKE_DEPENDS_USE_COMPILER OFF`，统一 `VSLANG=1033` 并对 MSVC ABI/64 位探测失败硬失败；
- P1：生产链统一 `PipelineRunner`、canonical warning 透传、PLY 完整回读、换帧清除点索引覆盖层；P2：DP2240 实测坐标/软触发/重连/性能后再定帧率目标。

### 2026-09-01（P1 生产链与契约补强）

- `CameraDialog` 的实时帧统一经 `PipelineRunner` 执行，内置链为 `camera.nextFrame → filter.voxel（可选） → canvas.publish`；生产链携带 `frameNumber`、时间戳和 stream generation，旧 generation 不得发布。
- 外部 pipeline 使用 `pointcloudview-live-pipeline-v1`，支持命令行/`PCV_LIVE_PIPELINE_FILE`/应用目录配置文件；解析、顺序、重复 ID、节点类型和参数任一失败时回退内置链并记录 warning。
- `CachedCloudResult`/`LoadResult` 增加 warnings；canonical PLY 写入失败保持主读取成功，但 warning 透传到日志和状态栏。
- binary PLY writer 增加 `Point3D` 标准布局、可平凡复制、字段偏移和大小约束；非 little-endian 使用逐字段编码。
- 新增 pipeline 严格校验、预转换帧透传、warning 及 binary writer 回读回归；实验相机 Debug CTest `17/17` 通过。

## 9. 风险与验收门槛

### 2026-09-02（registration_diagnostic 通用 CLI）

- `registration_diagnostic` 保留 `pointcloudview` GUI，并支持通用 `--input`/`--input-dir` 多帧输入、`--pose-info` 位姿匹配及 `inspect` PLY header `format` 检查；底层仍统一调用 `pcv::interface::stitchRawLineProfiles`。
- 验证：Windows Debug 工具构建通过；全量 CTest `13/13` 通过。真实 PLY/XML 拼接夹具和 GUI 点击验收未执行。

### 2026-09-02（Unicode 路径与 Binary PLY 门禁）

- 移除构建脚本和 CMake 的 ASCII-only 路径拒绝；保留 MSVC `VSLANG=1033` 与 header dependency 检查。
- 外部输入兼容 ASCII、binary little-endian 和 binary big-endian；所有成功输入必须生成并校验 binary little-endian canonical PLY，失败时主流程停止。正式、预览、诊断、平面和临时工件 PLY 均通过共享 Binary writer 输出。

- 核心门槛：实验开关 OFF/ON 均可配置，Debug/Release 全量 CTest 通过；Windows 使用 `NMake Makefiles` 并验证公共头依赖，JOM/Ninja 不作为当前支持生成器；
- 相机门槛：Mock 连接/首帧/停止/断开/重开、`POINTCLOUDVIEW_SELFTEST_CLOSE=1` smoke 及 DP2240 实机 GUI/坐标/重连证据；当前未完成；
- 不将 `live_pipeline` 单测或 DLL 部署视为生产链路验收。

### 2026-09-01（扫描 JSON 主线路崩溃修复：构建依赖跟踪）

- 现象：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`，主线路无法跑通；
- 根因：Qt Creator 的 `NMake Makefiles JOM` 构建树未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节），`modules/10_pointcloudread`（历史目录名，2026-09-06 起为 `modules/10_pointcloud_read`）的 `ply_reader.h` 扩展了 `PlyReadOptions`/`PlyReadResult` 后，`cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 未重新编译，与新编译的 `ply_reader.cpp.obj` 按不同结构体布局链接，`readPly()` 返回值写穿了 `readPlyCached` 的栈槽；
- 影响面：扫描 JSON 线路（`prepareTempWorkpiece` → `readPlyCached`）、普通"打开点云"与 `pointcloudstitch` 的读取路径；单元测试因可执行文件在头文件变更之后重建而全部通过，无法暴露该问题；
- 历史处置已被 2026-09-01 的 `VSLANG=1033` 策略替代；不再使用 `CMAKE_DEPENDS_USE_COMPILER OFF`，以恢复 ABI 探测并保持 `/showIncludes` 依赖解析；
- 验证：新建 `NMake Makefiles` 构建目录 Debug 构建通过，`.obj.d` 记录 `ply_reader.h`，全量 CTest 13/13 通过；相机开关 ON 的独立构建和 CTest 17/17 通过。`NMake Makefiles JOM` 实测生成空 `.obj.d`，脚本现已拒绝该生成器；Ninja 在中文 MSVC 下依赖同样失效。
- 未验证项：GUI 人工复测（打开扫描 JSON 与打开点云）需在重建后的可执行文件上执行；用户现有的 `build/Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 目录需重新配置并重新构建后才恢复正常。

### 2026-09（DP2240 实机 scan-height 无效帧修复）

- 现象：Range_Image 深度图回调（完整 3000 行）被 ScanFrameAssembler 以 Height=1060 误判丢弃，UI 显示帧：无效；
- 根因：Height 参数（1060）是传输窗口高度，非完整扫描行数（3000 来自 SDK 内部 max_scan_rows）；深度图模式回调本就是整幅扫描；
- 修复：装配器无活动分片时整幅 chunk 直接发布并自适应行数（动态提升 expectedRows）；活动分片期间的高度异常仍拒绝；Height 参数改为仅诊断，装配器以 expectedRows=0 首帧自适应；
- 验证：frame_source_tests 更新整幅放行/活动分片拒绝/自适应用例，单测 17/17 通过；实机确认 startup.log 出现 complete scan rows=3000/3000 fragments=1 discarded=0 size=2100x3000，无 valid=false 空帧；
- 已知限制：实时预览 pipeline 默认 maxPoints=300000，完整 630 万点被截断到 30 万点显示，画布完整点数渲染待评估。

### 2026-09（对齐 3DMVS 取流方式）

- 依据 3DMVS V3.1.4 实测日志与用户手册：真实相机 DP2240 取流 ImageMode=7（深度图）+ SDK 深度图转点云；虚拟相机走同一流程；缓存节点/重发包为 SDK 内部默认；
- 实现：默认 ImageMode=7；事件驱动取帧（onFramePublished）；转换移出 GUI 线程（QtConcurrent）；异常事件（FrameKind::Exception）+ 自动重连；软触发（SoftTrigger）；虚拟相机识别；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 通过；应用构建通过；
- 未验证项：真实相机软触发/重连/深度图实机复测待执行。

### 2026-09（DP2240 实机取流修复）

- 现象：DP2240（MV-DP2240-01P）接入取流返回"无效帧"；
- 根因：SDK 日志确认设备为 HFR 线激光轮廓传感器，ImageMode=4（点云）下实际回传深度图帧（ImageType_Depth C16）；原转换器只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 分派；FrameSource::convertFrame 真机 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应（不强制）；对话框取流模式选项 + 帧类型诊断日志；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 通过；应用构建通过；实机 DP2240 复测待执行。

### 2026-09-02（DP2240 无效帧：ABC32f 默认链路）

- 诊断：`startup.log` 的 Range C16 回调为有效帧（`ImageType_Depth=0x011000b8`、`2100x3000`、`12600000` 字节），但深拷贝后调用 `MV3D_LP_MapDepthToPointCloud` 返回 `0x8006000c (MV3D_LP_E_ALGORITHM)`；SDK 日志 `TO_PROFILE_S32 config[0] [0,0,0,0,0,0]` 后内部错误，不能以手写 scale/offset 推导替代；
- 实现：`Mv3dLpCamera` 和 `CameraDialog` 默认请求 `ImageMode_PointCloud_Image=4`；SetParam、GetParam 和回读模式必须全部成功，否则按回调关闭、等待、StopMeasure、CloseDevice、Finalize 路径清理。模式 4 仅接收 `ImageType_PointCloud=0x026000c0` 的 ABC32f；C16/Profile 和非法 payload/bValid=false 直接拒绝；
- 保留：`Range_Image=7` 是显式诊断选项，仍仅可调用 SDK 转换且失败 fail-closed；实时预览、latest-frame-wins、后台转换、冻结后处理和正式输出契约不变；
- 验证边界：自动化覆盖模式/类型策略与 ABC32f 长度/尺寸门禁。`DB1723975 / MV-DP2240-01P` 仍须现场确认模式 `4`、类型 `0x026000c0`、`width * height * 12`、持续预览、冻结、软触发、断线重连、关闭时序和目标帧率；若模式 4 仍输出 C16，保持失败并单独评估原始 SDK 回调内转换方案。

### 2026-09-02（DP2240 完整扫描回调链）

- 根因：实机 `ImageMode=4` 的 `Profile -> PointCloud` 转换成功，但每个回调仅为 `3200x24~30` 行，且 `nFrameNum` 与分片高度连续组成一次约 3000 行扫描；最新帧槽逐批覆盖造成画布只显示线轮廓；
- 实现：默认恢复 3DMVS `Range_Image=7`，严格 Set/Get 回读后读取设备 `Height`。SDK 原始回调内完成 C16/ABC16 到 ABC32f 的权威转换；内部组帧器以首批 `nFrameNum` 作为运行序号锚点，仅在后续编号连续、宽度一致且累计高度等于 `Height` 时发布完整扫描；不假定首批编号为 0；
- 边界：组帧失败、丢包、停止、断线与重连均清空未完成扫描。`LatestFrameSlot`、实时 canvas、冻结及既有输出只接收完整 ABC32f；冻结按钮拒绝残缺扫描；
- 验证：自动化覆盖完整帧、连续/末批、跳行、重复、宽度变化、重置和内存上限。DP2240 仍须现场确认 `ImageMode=7` C16 回调、`MV3D_LP_MapDepthToPointCloud=MV3D_LP_OK`、约 `2100x3000` 完整扫描、冻结、停止、重连及坐标方向。

### 2026-09-03（DP2240 TriggerMode 兼容）

- 现象：连续取流启动在 `MV3D_LP_GetParam("TriggerMode")` 返回 `0x80060004 (MV3D_LP_E_PARAMETER)`，回调尚未注册，界面无帧信息。
- 根因：当前实现把可选触发节点当作所有设备的启动前置条件；3DMVS `SimpleView_Callback` 官方示例打开设备后直接注册回调并 `StartMeasure`，未要求读取 `TriggerMode`。
- 修复：连续模式对 `MV3D_LP_E_PARAMETER`/`MV3D_LP_E_SUPPORT` 按 SDK 默认自由采集兼容并继续启动；节点存在时尽力设置并回读 `TriggerMode=0`，失败仅告警。软触发仍须完整探测 `TriggerMode=1`、设备支持的 `TriggerSource` 并 Set/Get 回读，否则 fail-closed；不猜测 `TriggerSelector` 枚举值。
- GUI：软触发按钮仅在本次设备探测成功后启用；连续模式超过 5 秒未收到完整扫描时提示可能仍处于硬件触发配置。
- 验证边界：自动化覆盖触发策略分类；真实 DP2240 仍须确认连续回调、软触发能力和设备端触发配置。

### 2026-09-03（DP2240 完整扫描高度判断）

- 修正 `Height` 目标选择：优先使用 SDK `nMax`（完整扫描容量），仅在不可用时回退 `nCurValue`，不硬编码扫描高度。
- 组帧错误记录实际 chunk/累计行数与目标行数；GUI 区分无回调与已有回调但尚未完成完整扫描。
- 单测覆盖 `nCurValue=24,nMax=3000` 选择、3000 行完整帧直发及高度不匹配诊断。
- 实机仍需确认 DP2240 的 `Height` 参数语义、完整扫描输出、坐标方向、软触发和断线重连。

### 2026-09-02（DP2240 Profile 原始回调转换）

- 新证据：`DB1723975 / MV-DP2240-01P` 已确认请求/回读 `ImageMode=4`，但稳定回传有效 `ImageType_Profile=0x023000b9`，例如 `3200x26x6=499200` 字节；SDK 定义明确支持 `ImageType_Profile -> ImageType_PointCloud`，且帧 scale/offset 为零，禁止手写坐标换算；
- 实现：在 `Mv3dLpCamera` SDK 回调内对原始 Profile 调用 `MV3D_LP_ImageConvert`，对 Range 深度调用 `MV3D_LP_MapDepthToPointCloud`，成功后立即复制 ABC32f 到 `LatestFrameSlot`。后台、实时预览、冻结和正式输出只处理拥有式 ABC32f；
- 门禁与风险：状态必须为 `MV3D_LP_OK`，输出类型 `0x026000c0` 且长度 `width * height * 12`；失败 fail-closed。实机仍需确认回调内 SDK 转换无重入/关闭死锁、坐标方向和性能；若直接 Profile 转换失败，后续仅评估 SDK 两步 `Profile -> Profile_ABC32 -> PointCloud`，不自动启用近似算法。

### 2026-08-31

- 完成 v0.3 模块目录、规范 target、UI 独立文件和模块文档治理。
- 完成共享 seam fusion、平面拟合和渲染组件实现迁移。
- 完成 `registration_diagnostic` 到模块接口的迁移。
- Debug/Release 构建和全量 CTest 已执行；GUI 人工验收、生产夹具验收和兼容层删除仍待完成。
