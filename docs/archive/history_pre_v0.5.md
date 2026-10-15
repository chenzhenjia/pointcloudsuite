# v0.5 之前的决策与历史证据

归档日期：2026-10-15。当前规则见[需求](../requirements/pointcloudview_v0.5.md)、[架构](../architecture/software_architecture_v0.5.md)与[契约](../contracts/data_contracts_v0.5.md)。本文件不是当前能力声明；详细来源去向见[迁移表](document_migration_v0.5.md)。

## 决策时间线

| period | decisions_and_supersession |
|---|---|
| 2026-08～09 相机与交接 | DP2240模式/SDK转换/扫描组装多轮试验，真机SDK失败禁止近似坐标，05最终并入10；保留试验日期，不能合并成一种同时生效的配置 |
| 2026-09 模块整理 | 40旧配准归30，ROI/临时工件归60，70→60、80/filtering/05→10；旧target/header保留 |
| 2026-09-18 2.5D试验 | 高程图与结构可见性等受控迭代曾有真实回放；后续已回滚，只是历史证据 |
| 2026-09-20～21 临时工件 | PLY与JSON统一尾段，随后修会话状态机；早期静态与后续15/15属于不同验证 |
| 2026-09-22～30 扫描 | 中心/范围从人工参数变为WObj投影，最终保留Global/LocalRoi；“删除全局”不是现行规则 |
| 2026-09-24 手眼 | 统一T_base_flange*T_flange_depth和LineProfileXz语义；真实样本4389670点逐点误差0只对应当次来源 |
| 2026-10-06～08 纹理/性能 | 映射由几何投影/裁剪改为严格原始sourceIndex；原始坐标实验独立；平面提速与0.1mm单线容差有各自验证 |
| 2026-10-08～09 快速模式与回滚 | PoseOverlapFine曾实现并测试，75d3e3b定向恢复6329426整帧算法；保留50当前数学及覆盖确认 |
| 2026-10-09以后 | 独立所选平面、共享UI、内存、单击v1/v2详见版本日志，不以它们恢复旧整帧模式 |

## 去重和追溯规则

以下证据段保留原始段落/表格/命令，不把历史“当前”“本轮”“通过”解释为2026-10-15当前软件。相同内容忽略空白/排版差异只存一次；其他来源给重复段落编号。不同措辞或不同配置保留，避免删除独有参数、日志、风险与回滚依据。历史文档里的版本、旧路径和过时声明是原时点事实，现行文档另行定义。

<a id="source-01"></a>

## AGENTS.md

原始来源：`AGENTS.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0088"></a>

**10. 变更记录（追加，不修改以上规则）**

<a id="evidence-0089"></a>

**2026-09-20（PLY 线路：打开 PLY → robot_base → 四件套）**

<a id="evidence-0090"></a>

- 主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`、`10_pointcloud_io`；UI 编排 `apps/pointcloudview`。
- 现有“打开 PLY”入口接入临时工件流程：手眼 XML + 手工 Start/End 位姿 → 转换到 `robot_base` → 第一组三点自动平面拟合 → 第二组三点同面校验 → 自动 WObj1、边缘分割、ROI、Mask → `ArtifactTransaction` 一次性提交四件套。两组三点是唯一人工步骤。
- 模块 60 追加 `TempWorkpieceOptions::useInMemoryScan` / `scanInfo` 与 `TempWorkpiecePreparation` 诊断字段（`inputYMinimum`、`inputYMaximum`、`signedTravel`、`dominantTravelAxis`、`cancelled`），JSON 入口行为与公共 JSON schema、四件套文件名/格式、错误码均未变化；兼容 target 与 forwarding header 未改动。
- 本轮未执行 Git 操作、未执行 Debug/Release 构建、未执行 CTest 和 GUI 验收；结论仅覆盖代码级与静态检查。

<a id="evidence-0091"></a>

**2026-09-21（PLY/JSON 临时工件状态机排错修复）**

<a id="evidence-0092"></a>

- `apps/pointcloudview` 新增 `None/Json/Ply` 单一临时工件来源状态、统一会话失效、旧会话替换确认和自动链失败出口；参数对话框取消或 JSON 解析失败不再遗留 `WaitingForParameters` 锁死状态。
- 主动放弃取点保留转换点云和会话，可重新取点；WObj1、边缘、ROI/Mask、四件套前置校验等同步失败统一进入 `Failed`，保留结果并允许重试；打开新源前确认是否放弃旧会话。
- JSON 位姿摘要统一显示 `[X,Y,Z,Rx,Ry,Rz]`，与 `[X,Y,Z,A,B,C]`、`A=Rx/B=Ry/C=Rz` 契约一致；公共 schema、四件套格式和错误码未改动。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke、真实 PLY/XML 和生产夹具未验证。

<a id="evidence-0093"></a>

**2026-09-21（代码验收与发布边界）**

<a id="evidence-0094"></a>

- 代码验收通过：源码树外 Debug/Release 构建成功，双配置全量 CTest 均为 `15/15`，`git diff --check` 通过。
- 本次发布只包含 PLY/JSON 临时工件线路修复、模块 60 接口/实现、对应回归测试和文档；已有 `30_pointcloud_stitch`、PointCloudStitch、临时脚本等工作区修改保留，不混入提交。
- GUI smoke、真实 PLY/XML 和生产夹具现场验收仍未完成，不能以自动化测试替代。

<a id="evidence-0095"></a>

**2026-09-22（扫描路径规划修订与 UI 接入）**

<a id="evidence-0096"></a>

- 主模块 `60_geometry_workpiece`；UI 编排 `apps/pointcloudview`；JSON 正式写入复用 `10_pointcloud_io::ArtifactTransaction`。
- 扫描规划固定 WObj1 X 横向/Y 行程：全局中心由用户输入 `XYZABC`，局部中心由 ROI 三维质心投影到 WObj1 XY 后沿 `+Z` 偏移并继承 ABC；目标宽度、行程长度、相机宽度和重叠距离均为 mm。
- 扫描线按 `-X → +X` 编号，所有线使用用户选择的同一 Y 方向，输出连续 `P0→P1`、`P2→P3`。`scan_trajectory_v1` 记录最终中心、请求/实际重叠、间距、方向、P 点与段。
- `pointcloudview` 增加第五步“扫描规划”、后台取消与 revision 门禁、P 点显示、覆盖确认和事务 JSON 导出；画布、WObj1、ROI 或参数变化后旧结果立即失效。
- 自动化与风险结论以本轮最终验证为准；不把软件轨迹计算等同于真实机器人可达性、相机有效宽度、安全距离、碰撞或干涉验收。
- 本轮验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟通过，`git diff --check` 通过；Release `windeployqt` 缺少本机 Visual Studio release redistributable，未做便携部署。未执行真实点云/ROI 的全流程 GUI 手工验收。

<a id="evidence-0097"></a>

**2026-09-22（全局与局部扫描路径规划统一）**

<a id="evidence-0098"></a>

- 恢复 `pcv::scan::ScanPlanningMode::{Global,LocalRoi}`；全局使用拟合平面索引，局部使用 ROI 索引，均从 WObj1 XY 投影包围盒计算中心、范围，统一沿 `+Z` 偏移 `scanHeightMm`。
- 扫描带固定 50% 重叠，UI 删除人工宽度/行程/重叠/ABC 输入，增加模式切换、范围显示、项目 JSON 与 InoRobot `P.json` 两个事务导出；InoRobot 使用 `sr2026-p-json-mvp-2`、`wobj_num=1`、P[10] 起始和 `[X,Y,Z,RZ,RY,RX]`。
- 本轮扫描单元测试通过，pointcloudview Debug 目标编译通过；Release、全量 CTest 和真实点云 GUI 全流程尚未在本轮重复验证。真实机器人可达性、相机有效宽度、安全距离、碰撞与干涉仍是现场风险。

<a id="evidence-0099"></a>

**2026-09-24（模块 50 手眼标定统一与点云倒转修复）**

<a id="evidence-0100"></a>

- 主模块 `50_coordinate_conversion`；调用方清理 `apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`；复用入口核对 `apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`、`apps/pointcloudstitch` UI 与回归路径。
- 黄金基准与统一契约：扫描 JSON 路径是手眼标定黄金基准；`RTmatDepth2robot` = `T_flange_depth`（flange_from_depth）；`p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`，即 `T_base_depth = T_base_flange * T_flange_depth`。禁止默认求逆，禁止固定 Z 翻转、镜像或 180° 补丁。`LineProfileXz` 的相机点为 `[X,0,Z]`，输入 Y 只表示扫描进度（`ratio = input.y / signedTravel`，负行程是合法反向扫描，方向由 Start/End 决定）；`FullXyz` 保留完整 XYZ；点与法向共用同一旋转且法向不平移。
- 根因与修复：反转风险来自重复实现与 LineProfileXz 语义不一致。模块 50 新增 `composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus`/`robotCloudStatusCode()`/`robotCloudStatusText()`，`transformPointToRobotBase()` 追加可选 `QString *error` 尾参；删除回归工具的重复 XML 解析与 `poseMatrix()`、view 应用的重复刚体校验/SLERP/进度启发式/内联矩阵乘法，改调模块 50。既有矩阵方向、位姿约定、单位、PLY/PNG/JSON 契约与错误码不变；view 应用合并缓存版本提升为 9。
- 测试：`handeye_transform_tests` 覆盖乘法方向、JSON 黄金链路、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、正负行程与反向扫描点序、FullXyz、法向、位姿往返、非有限/非正交/非单位/行列式非 +1 的拒绝、无行程/空点云/全部无效/超范围/取消与状态码；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口的 robot_base 逐点一致性。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，4 项手眼/临时工件/拼接/多帧定向测试 `4/4`，`pointcloudview` 双配置启动冒烟 `ExitCode=0`。真实样本转换有效点 `4,389,670`，JSON 输出与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 提示缺少本机 Visual Studio release redistributable，便携部署未验收。
- 未验证：真实 XML 物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一（不在本轮范围）以及真实机器人安全/可达性/碰撞/干涉；结论只来自本轮实际执行的构建、回归和启动冒烟。

<a id="evidence-0101"></a>

**2026-09-30（扫描路径与 InoRobot P.json 规则修订）**

<a id="evidence-0102"></a>

- 主模块 `60_geometry_workpiece`；UI `apps/pointcloudview`；正式 JSON 仍复用 `10_pointcloud_io::ArtifactTransaction`。
- `Global`/`LocalRoi` 双模式保留，UI 默认 LocalRoi，局部模式直接使用 `localRoiIndices` 且不重新拟合。扫描高度、相机宽度、重叠默认分别为 50/100/50 mm；横向按 `cameraWidthMm-overlapMm` 均匀分布，首尾覆盖贴合 ROI X 边界，固定 `-X→+X` 排序和 `+Y→-Y` 行程，每条线两端各延长 50 mm。
- `ScanPlanningOptions` 增加 `overlapMm`/`travelExtensionMm`；结果明确报告原始/最终行程、扩展值、请求/实际重叠、实际间距和线数。InoRobot 限制最多 9990 点，`source_workpiece_info=temp_workpiece_info.json`；P.json 使用 `P[10]...`，姿态 `[X,Y,Z,RZ,RY,RX]`，不写控制器。
- UI 删除方向选择、增加重叠参数和固定延长提示，显示规划摘要及内部 `P0/P1` 与导出 `P[10]/P[11]` 的编号映射；保留后台取消、窗口关闭等待、revision 门禁、内部 JSON 导出和覆盖确认。
- 验证：本轮定向 `scan_planner_tests` 通过；Debug/Release 构建、全量 CTest、启动冒烟与真实点云 GUI 操作结果记录在本轮最终验收条目。机器人可达性、碰撞/干涉与真实控制器导入不属于软件验证。

<a id="evidence-0103"></a>

**2026-09-30（扫描规划最终验证）**

<a id="evidence-0104"></a>

- 追加测试覆盖：实际重叠不低于请求值、两端固定延长、倾斜 WObj1 的 P.json 局部 XYZ、无效坐标轴、退化 ROI 投影和 9990 点边界；UI 的 overlap 最大值随 cameraWidth 联动。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，双配置 `scan_planner_tests` 通过，`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟双配置退出码均为 `0`，`git diff --check` 通过。
- 未验证：GUI 人工 LocalRoi 单线/多线、参数修改、P.json 导出和旧结果失效；computer-use connector 返回 `unsupported Codex auth method: apikey`。真实相机、机器人可达性、碰撞/干涉和控制器导入仍需现场验收。

<a id="evidence-0105"></a>

**2026-10-06（离线 PLY + 亮度图纹理点云）**

<a id="evidence-0106"></a>

- 主模块 `60_geometry_workpiece` 新增 `pcv::texture::mapPlaneBrightness()`；当前契约输入现有点云、`planeIndices`、原始 PLY `sourceIndices`/点数和 `QImage`，输出等长灰度/有效标记、栅格尺寸、映射统计、取消和诊断。
- 映射固定为 `Format_Grayscale8`、忽略 alpha、不增强对比度、最近邻、等比中心裁剪；图像左→右对应局部 `+X`，顶部→局部 `Y` 最小值；不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- `20_pointcloud_render` 增加 `PointColorOverlay` 校验、纹理灰度颜色模式和 GUI 线程 OpenGL 上传；非平面点/无效点回退原颜色模式，点云/平面/坐标系变化清理旧覆盖，切换颜色模式保留缓存。
- `apps/pointcloudview` 增加亮度图选择、后台映射、取消/revision 门禁、替换保护和状态栏统计；失败或取消不发布半成品。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `17/17`；纹理映射和渲染契约定向测试通过。
- 未验证：真实 PLY/亮度图 GUI 手工流程、视觉方向/中心裁剪效果、无 WObj1 现场方向和大点云性能；实时取流不在本轮范围。Release `windeployqt` 仍缺本机 Visual Studio release redistributable。

<a id="evidence-0107"></a>

**2026-10-06（严格 PLY 栅格索引亮度纹理修正）**

<a id="evidence-0108"></a>

- `pcv::texture::mapPlaneBrightness()` 改为严格原始 PLY `sourceIndex` 采样：原始点数必须等于亮度图像素数，点序固定为左上角起始的 row-major；仅 `planeIndices` 对应点接收灰度。
- `pointcloudview` 保存单个 PLY 的 `declaredPointCount`、`sourceIndices` 和转换过滤统计；过滤、降采样、拼接、文件夹 PLY 或无 provenance 点云禁用纹理入口并显示诊断。
- 纹理不再依赖 WObj1，WObj1 修改不清除纹理；点云/平面索引/source provenance 变化清除纹理；同一平面索引的边缘结果更新保留纹理。
- 不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON；未实现实时取流纹理接入。
- 最终验证：`C:\pcv-build\texture-raster-debug` 与 `C:\pcv-build\texture-raster-release` 构建成功，双配置 CTest 均 `17/17`；纹理定向测试、`pointcloudview` 自关闭启动冒烟和 `git diff --check` 均通过。真实 PLY/亮度图视觉验收、现场 row-major 一致性和实时取流仍未验证；Release `windeployqt` 缺少本机 Visual Studio release redistributable。

<a id="evidence-0109"></a>

**2026-10-08（PLY 原始坐标纹理实验入口）**

<a id="evidence-0110"></a>

- `apps/pointcloudview` 增加“PLY 纹理实验（原始坐标）”独立来源状态：单个 PLY 直接保留 XYZ、原始点序、`declaredPointCount` 和 identity `sourceIndex`，不经过手眼转换，不建立 WObj1，不生成临时工件四件套。
- 实验流程复用两组三点和同面校验；第一组三点使用任意平面方向的垂直距离拟合，第二组三点通过后才启用亮度图选择，复用严格 `source_raster_index` 映射。
- 新增 `tests/unit/pointcloudview/texture_experiment_tests_v1.cpp` headless 契约测试，并在 `tests/CMakeLists.txt` 注册；覆盖声明点数、source index、垂直平面、同面校验、严格 row-major 映射、取消和尺寸不匹配拒绝。
- 本轮 Debug 构建和全量 CTest `18/18` 已通过；Release 构建/CTest、真实 PLY/亮度图 GUI 视觉验收、现场 row-major 一致性和实时取流仍需验证。

<a id="evidence-0111"></a>

**2026-10-08（扫描规划单线容差修复）**

<a id="evidence-0112"></a>

- 主模块 `60_geometry_workpiece`；扫描线判定增加模块内固定 `0.1 mm` 容差，投影宽度 `<= cameraWidthMm + 0.1 mm` 时只生成 `P0/P1`，超过阈值才按实际覆盖要求生成多条线。
- 结果 DTO 增加 `singleLineWidthToleranceMm`，内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm`；保留原始宽度和单线重叠字段兼容值，不新增 UI 参数，不改变 InoRobot P.json 契约。
- 回归覆盖 Global/LocalRoi 单线/超容差多线边界、独立段数、P.json 点数和编号；真实相机有效视野、ROI 边界及现场机器人行为仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，全量 CTest 双配置均 `18/18`；扫描定向 Debug 测试通过，双配置全量测试均包含扫描回归；双配置 `pointcloudview` 自关闭启动冒烟退出码 `0`，`git diff --check` 通过。未使用真实 ROI 做 GUI 复验；Release `windeployqt` 仍缺本机 Visual Studio release redistributable。

<a id="evidence-0113"></a>

**2026-10-08（三点平面拟合等价提速 v2）**

<a id="evidence-0114"></a>

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。

<a id="evidence-0115"></a>

**2026-10-08（PointCloudStitch 位姿粗配准＋重叠区快速精配准）**

<a id="evidence-0116"></a>

- 主模块 `30_pointcloud_stitch`；保留 `Legacy` 完整配准和仅位姿直拼入口，新增 `RegistrationProfile::PoseOverlapFine`。新模式以模块 50 的 `robot_base` 位姿为初值，仅在相邻帧机器人初值重叠区执行两层受限 point-to-plane ICP（体素 `0.5/0.2 mm`、对应距离 `3.0/0.6 mm`、迭代 `20/15`），每对修正预算 `10 mm / 1°`。
- 新模式跳过旧平面跟踪、切向/宽范围搜索、恢复重试、2.5D 和 legacy 双跑；仅更新可观测方向，退化、低覆盖、结构不足和无有效解按警告处理。无重叠、超预算或取消时回退机器人位姿或终止任务，按扫描顺序累计 pair 矩阵；不混用已修正目标，不重复累计。
- 新模式默认关闭 seam fusion 与 preview，保留全量有效点、`cloudIds`、`sourceIndices`、`scanRatios` 和事务化正式输出。manifest/诊断记录 `fine_applied`、`pose_fallback`、层数、对应数量、残差、退化状态、pair/cumulative matrix 及累计漂移；`formal_output` 只表示文件事务提交成功，质量策略为 `warn_only`。
- GUI 默认选择 `PoseOverlapFine`，公共 API 默认仍为 `Legacy`；`regressionrunner` 增加 `--pose-overlap-fine` 和 `--interface-pipeline`。未修改手眼矩阵、坐标约定、窗口生命周期、原始输入或提交/推送行为。
- 本轮源码树外构建目录：`C:\pcv-build\pose-overlap-fine-debug-v1`、`C:\pcv-build\pose-overlap-fine-release-v1`，生成器 `NMake Makefiles`，MSVC `19.51.36252.0`，Qt `C:\Qt\6.8.3\msvc2022_64`。Debug/Release 定向构建已成功；Debug 定向 `stitching_interface_tests`、`seam_fusion_tests`、`multiframe_registration_tests`、`pose_overlap_fine_tests` 已通过，100 次重复回归正在/已执行，以最终命令结果为准。
- 结构平面路径补充修复 `structuralOverlapPlane()` 返回结构平面时初始化 `axisU/axisV`，避免局部重叠范围退化为 `[0,0]`。测试覆盖水平、`30°/60°/75°`、纯平面可观测自由度、链累计、回退、取消、预算、低覆盖警告及串并行等价。
- `D:\stitch_test` 当前可见 `hand_eye_convert.xml` 和 PLY，但缺少可安全重放所需的 Start/End 位姿 JSON/TXT 或历史报告；因此 `1/test_1`、`5_40/test`、`6_50/test` 及真实倾斜夹具本轮未作真实回放，不以合成测试替代现场结论。GUI 人工拼接、真实输出质量、PageHeap 和 Release 便携部署仍需补验；Release `windeployqt` 可能受本机 Visual Studio release redistributable 缺失影响。

<a id="evidence-0117"></a>

**2026-10-09（PointCloudStitch 算法定向回滚到 6329426）**

<a id="evidence-0118"></a>

- 主模块 30；用户明确授权 Git 定向回滚。起始工作区干净，基线为 `6329426`（2026-09-18 09:27:46 +08:00），使用 `git restore --source=6329426 --worktree -- <paths>`，不重置项目历史。
- 模块 CMake、公共头、multiframe/stitching 实现、UI 模式文件及两份拼接测试恢复基线；seam 算法本就与基线一致。删除后续新增 heightmap 源/头，移除 pose_overlap_fine 专属测试注册与 heightmap/fine/interface-pipeline 回放参数。GUI 恢复完整配准/仅手眼转换两种模式。
- 必要差异：保留模块 50 当前统一手眼实现、regressionrunner 参数适配和 GUI 报告的统一坐标组合调用，保留已有输出覆盖确认。其他模块、其他测试注册及历史文档记录保留。同步应用/模块 README、v0.3/v0.4 契约、协同文档、开发方案与 target.md。
- 验证：`C:\pcv-build\stitch-rollback-6329426-debug-v1` 完整 Debug 构建成功；定向 stitching_interface/seam_fusion/multiframe 测试 `3/3` 通过，全量 CTest `19/19` 通过。核心模块文件、UI 与对应测试对 `6329426` 的 `git diff --exit-code` 为 `0`，`git diff --check` 通过。
- 启动冒烟：首次指定 offscreen 时 pointcloudstitch 停在 Runtime Library 弹窗，未算成功；结束本轮测试进程后，显式使用当前 Qt plugins/platforms 与 windows 平台重试，pointcloudstitch/pointcloudview 自关闭启动退出码均为 `0`。未影响已有用户运行的 pointcloudview。
- 未验证：Release 构建/部署、真实 PLY/XML/Start-End 回放与 GUI 全流程、现场质量/性能/机器人安全。模块 50 保持当前版本，因此不是整个项目逐字回退；恢复旧算法也恢复其既有质量/性能限制。
- 回滚点：本轮前完整版本仍为 `ca0adc7`。本轮作为独立本地 Git 提交保存，不进行远程上传或强制推送。

<a id="evidence-0119"></a>

**本轮最终验证**

<a id="evidence-0120"></a>

- 源码外 `selected-plane-debug-v1` / `selected-plane-release-v1`完整构建成功，双配置CTest21/21；Debug拼接相关5项各100次（500次）全部通过；pointcloudview双配置自关闭启动ExitCode=0。完整命令/日志在开发记录。
- 新入口MainWindow采用轻量launcher，避开旧processor和模块30的同名ICP DTO头文件冲突，不重构旧接口。offscreen测试显式当前Qt插件搜索路径，首次初始化超时不当作已证实堆损坏。
- 用户确认现场选面另验；真实质量/性能、GUI点选、PageHeap、生产夹具及Release便携部署未验证。旧目录未删除，代码未提交/推送/上传；最后检查执行git diff --check和git status --short。

<a id="source-02"></a>

## target.md

原始来源：`target.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0134"></a>

**2026-10-08：PointCloudStitch 位姿＋重叠精配准迭代**

<a id="evidence-0135"></a>

- 用户确认优先观察全量拼接结果与提速：位姿粗配准、重叠区两层小幅 6DoF 精配准、质量仅警告、失败回退位姿继续正式输出；每对 10 mm/1°，累计不限，seam 默认关闭，无预览。
- 实现位于模块 30，UI 默认新模式，公共 API 默认 Legacy；复用 50 和 10，不修改手眼或原始点云，不提交/推送。矩阵按原相邻样本求解、帧序累计、失败锚点重置；Qt worker const 访问和来源映射保持。
- 报告区分 `fine_applied/pose_fallback` 与严格质量状态，旧验收未执行不写成通过。正式输出仍用 stitched 文件名，GUI 覆盖前确认，事务成套提交。
- 测试登记：倾斜纯平面/小旋转、无重叠和超预算、链重置、非有限、取消、串并行1/2/4等价、无预览、事务覆盖保护。旧 coarse 夹具要求恢复4 mm却继承2 mm默认预算，本轮使其平移预算显式化，不修改算法质量阈值。
- 验证与风险以本轮最终记录为准；真实 PLY/XML 存在但 Start/End 位姿和历史报告缺失，不能宣称现场速度/双平面问题已验收。

<a id="evidence-0136"></a>

**一、北极星目标**

<a id="evidence-0137"></a>

将 `pointcloudview` 建设为点云处理、局部标注、刀路规划结果接入和用户展示的统一入口，形成从输入数据到可视化业务结果的完整软件闭环。

<a id="evidence-0138"></a>

本目标以 `pointcloudview` 主流程为中心，覆盖必要的点云读取、渲染、点选、几何处理、坐标转换、结果输出和外部项目接入能力。陈振佳与于海军分别负责后端和前端/外部项目，并共同完成跨边界联调和端到端验收。

<a id="evidence-0139"></a>

**二、目标业务闭环**

<a id="evidence-0140"></a>

**1. 全剖流程**

<a id="evidence-0141"></a>

```text
.ply 文件
  → 点云处理程序
  → png 文件
  → 全剖刀路规划程序
  → 全剖路径 PNG
  → 贴合到纹理点云并由 pointcloudview 展示
```

<a id="evidence-0142"></a>

**2. 点剖流程**

<a id="evidence-0143"></a>

```text
.jpg 文件
  → 高点标注程序
  → high_point_overlay.png
  → 点剖刀路规划程序
  → 点剖路径 PNG
  → 贴合到纹理点云并由 pointcloudview 展示
```

<a id="evidence-0144"></a>

两条流程均应输出坐标 JSON，用于描述结果位置、坐标关系和后续接入所需的元数据。`high_point_overlay.png` 是点剖流程中的中间输入和可视化产物；路径 PNG 是最终贴合到对应纹理点云的业务结果。

<a id="evidence-0145"></a>

最终用户应能够在 `pointcloudview` 中查看点云、纹理、overlay、路径和相关坐标结果，并能够识别处理成功、取消、失败和输入不合规等状态。

<a id="evidence-0146"></a>

**三、共同交付边界**

<a id="evidence-0147"></a>

**陈振佳：后端负责人**

<a id="evidence-0148"></a>

- 负责点云处理算法、共享模块及其稳定公共接口。
- 负责版本化 DTO/API、PLY/PNG/JSON 数据契约和 `ArtifactTransaction` 事务输出。
- 负责错误码、诊断信息、取消语义、失败回滚和结果发布边界。
- 负责构建配置、自动化测试、后端回归和对前端接入所需的稳定能力支持。
- 不直接承担前端页面和用户交互实现。

<a id="evidence-0149"></a>

**于海军：前端与外部项目负责人**

<a id="evidence-0150"></a>

- 负责 `pointcloudview` 前端页面、交互流程和用户展示体验。
- 负责高点标注程序、全剖/点剖刀路规划项目，以及这些项目与 `pointcloudview` 的接入实现。
- 负责将后端输出转换为清晰可操作的界面状态、结果预览和用户提示。

<a id="evidence-0151"></a>

**双方共同负责**

<a id="evidence-0152"></a>

- 确认跨项目输入、输出和版本兼容关系。
- 完成接口联调、固定样例和契约内现场数据回归。
- 共同处理跨边界缺陷、变更影响和最终端到端演示验收。

<a id="evidence-0153"></a>

**四、接口与数据原则**

<a id="evidence-0154"></a>

- 继续沿用现有 PointCloudView 数据契约：binary little-endian PLY、Grayscale8 PNG、`snake_case` JSON、`robot_base` 坐标约定和 `ArtifactTransaction` 成套提交语义。
- 新增字段、产物或外部项目接口必须版本化，不破坏既有契约；具体 schema 以对应模块公共头、模块文档和数据契约为准。
- 后端通过稳定 DTO/API、结果状态、错误码、取消标记和诊断信息向前端及外部项目提供能力；前端不得直接依赖共享模块内部实现或 `modules/*/src`。
- 外部项目的最终接入形态暂不冻结。CLI、库、进程或其他方式作为后续共同决策项；在形态确定前，先保持版本化文件契约和 adapter 边界。
- 取消、失败或输入不合规时，不得发布部分结果、复用旧正式输出或伪造成功状态。

<a id="evidence-0155"></a>

**五、完成定义与验收门槛**

<a id="evidence-0156"></a>

本目标完成的最低条件是：

<a id="evidence-0157"></a>

- 满足已发布输入、标定、质量和规模约束的现场数据，能够完成全剖或点剖对应流程。
- 点云处理、高点标注、刀路规划、坐标 JSON、路径 PNG、纹理点云和统一展示之间的调用链能够端到端运行。
- 在相同输入、参数和契约条件下，结果可重复，关键产物和诊断可追溯。
- 输入校验、取消、处理失败、外部项目失败和事务回滚均有可见、可定位的状态反馈。
- 不满足契约的数据必须被明确拒绝并给出诊断，不以“成功但结果不可信”的方式继续发布。
- 双方完成接口联调、回归验证和一次可复现的端到端业务演示。

<a id="evidence-0158"></a>

真实设备、机器人、现场生产部署、长期稳定性和性能扩展属于后续验收门槛；本目标首先保证契约内现场数据的软件闭环。

<a id="evidence-0159"></a>

**六、范围边界**

<a id="evidence-0160"></a>

本目标默认不新增以下独立交付范围：

<a id="evidence-0161"></a>

- `pointcloudstitch`、`pointcloudcli` 和实验相机能力，除非后续确认它们是本闭环的必要依赖。
- `90_interferenceplane` 和 `100_qualityreport`，当前仍按未实现模块处理。
- 未经双方确认的刀路 JSON schema、外部项目部署方式或机器人执行/坐标修正下发能力。

<a id="evidence-0162"></a>

现有 `AGENTS.md`、`docs/architecture/module_ui_integration_v0.4.md`、`docs/requirements/pointcloudview_v0.3.md`、源码、CMake 和测试是实现约束与事实来源；本文件不替代这些文档，也不把流程图中的目标步骤表述为已经完成的功能。

<a id="evidence-0163"></a>

**七、轻量状态记录**

<a id="evidence-0164"></a>

| 日期 | 状态 | 说明 |
|---|---|---|
| 2026-09-18 | 目标基线已建立 | 已确认两条业务流程、双方职责、统一展示入口、契约内现场数据验收口径；外部项目接入形态待共同决策。 |
| 2026-09-20 | PLY 线路已实现（代码级） | 现有“打开 PLY”入口改造为手眼转换 + 临时工件线路，两组三点后自动生成四件套；未执行构建、CTest 与 GUI 验收，详见第九节。 |

<a id="evidence-0165"></a>

**2026-09-22（删除全局扫描，仅保留局部 ROI）**

<a id="evidence-0166"></a>

- 主模块 `60_geometry_workpiece`，UI 编排 `apps/pointcloudview`；删除 `ScanPlanningMode`、全局中心和人工 ABC 输入，扫描 API 始终使用当前 ROI。
- ROI 三维质心投影到 WObj1 XY 后沿 `+Z` 偏移生成中心 XYZ，P 点 ABC 直接取 WObj1 `poseA/poseB/poseC`；JSON 固定 `mode=local_roi`，不再提供全局分支或全局回退。
- 更新扫描单元测试、模块/架构/需求文档和 UI 参数转换；保留后台取消、revision 门禁、P 点显示和 `ArtifactTransaction` 导出。
- 本轮验证待执行；风险仍包括真实机器人可达性、相机有效宽度、安全距离、碰撞/干涉和真实 ROI GUI 流程。

<a id="evidence-0167"></a>

**八、2.5D 高程图配准迭代记录**

<a id="evidence-0168"></a>

**2026-09-22（60 扫描路径规划修订与 UI 接入）**

<a id="evidence-0169"></a>

- 主模块 `60_geometry_workpiece`，UI 编排 `apps/pointcloudview`，事务输出复用 `10_pointcloud_io`；修订 `pcv::scan::planScanTrajectory()`、`scanTrajectoryToJson()` 及尚未发布的 `scan_trajectory_v1`。
- WObj1 X/Y 固定为横向/行程轴；全局采用人工中心 `XYZABC`，局部采用 ROI 三维质心投影与 `+Z` 高度并继承 ABC；输入人工目标尺寸、相机宽度和毫米重叠距离。扫描线按 `-X → +X` 编号，全部使用选定的同一 Y 方向，输出 `P0→P1`、`P2→P3` 独立线段。
- `pointcloudview` 新增第五步“扫描规划”，后台执行、逐行显示 P 点、revision 变化自动失效，默认导出 `scan_trajectory.json`；覆盖前确认，写入由 `ArtifactTransaction` 原子提交。
- 文件：`modules/60_geometry_workpiece/include/pcv/scan/scan_planner.h`、`modules/60_geometry_workpiece/src/scan_planner.cpp`、`modules/60_geometry_workpiece/CMakeLists.txt`、`apps/pointcloudview/mainwindow.h/.cpp`、`tests/unit/scan/scan_planner_tests.cpp`、`tests/CMakeLists.txt`、模块/架构/需求文档、`AGENTS.md`。
- 验证：源码树外 `scan-ui-debug-20260922` / `scan-ui-release-20260922` 构建成功；Debug/Release 全量 CTest 均 `16/16`，扫描/平面/ROI/事务定向测试通过；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` Debug/Release 启动冒烟通过；`git diff --check` 通过。Release `windeployqt` 提示本机缺少 Visual Studio release redistributable，便携部署未验收。
- 风险：真实相机有效宽度、机器人可达性、现场安全距离、碰撞和干涉未验证；未用真实点云/ROI 完成人工 GUI 全流程（全局/局部/导出/旧结果失效）冒烟。

<a id="evidence-0170"></a>

**2026-09-30（扫描规划参数与 P.json 契约修订）**

<a id="evidence-0171"></a>

- 主模块：`60_geometry_workpiece`；UI：`apps/pointcloudview`；事务输出：`10_pointcloud_io::ArtifactTransaction`。
- 保留 `Global`/`LocalRoi`，UI 默认 LocalRoi；LocalRoi 直接使用当前 ROI 索引，不重新拟合。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、两端 `travelExtensionMm=50`，固定 `-X→+X` 线序和 `+Y→-Y` 行程，首尾覆盖贴合 ROI X 边界。
- 增加原始/最终 Y 行程、延长距离、请求/实际重叠、实际间距、线数结果字段；限制 P.json 最多 9990 个点，来源字段为 `temp_workpiece_info.json`，点号从 `P[10]` 连续递增，姿态为 `[X,Y,Z,RZ,RY,RX]`，不调用控制器 API。
- UI 删除方向下拉框，增加重叠输入和固定延长提示，显示内部 P0/P1 与 P.json P[10]/P[11] 区别；点云、WObj1、ROI 或参数 revision 变化立即取消并清空旧结果，导出继续使用 ArtifactTransaction 和覆盖确认。
- 文件：`modules/60_geometry_workpiece/include/pcv/scan/scan_planner.h`、`modules/60_geometry_workpiece/src/scan_planner.cpp`、`apps/pointcloudview/mainwindow.h`、`apps/pointcloudview/mainwindow.cpp`、`tests/unit/scan/scan_planner_tests.cpp`、相关模块/架构/需求文档与 `AGENTS.md`。
- 验证：本轮 `scan_planner_tests` 通过；完整构建、全量 CTest、启动冒烟和 GUI 手工验证以最终汇报的实际结果为准。机器人可达性、碰撞/干涉、控制器导入和真实工件 GUI 全流程仍需现场验证。

<a id="evidence-0172"></a>

- 补充 `scan_planner_tests`：固定 Y 延长和最终行程、实际重叠下限、倾斜 WObj1 P.json 局部坐标、无效 WObj1/退化投影、P[10] 到 P[9999] 边界及无 `wobj` 字段。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`；双配置定向扫描测试通过；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟双配置退出码均为 `0`；`git diff --check` 通过。
- GUI 人工验证未完成：computer-use connector 报 `unsupported Codex auth method: apikey`；真实 ROI、相机视野、机器人可达性、安全距离、碰撞/干涉和控制器导入仍需现场验证。

<a id="evidence-0173"></a>

- 主模块：`60_geometry_workpiece`；UI 编排：`apps/pointcloudview`；输出复用 `10_pointcloud_io::ArtifactTransaction`。
- 修改：恢复 `Global`/`LocalRoi` 双模式；两种模式均从平面或 ROI 有效点的 WObj1 XY 投影包围盒计算中心、X 宽度和 Y 行程，沿 WObj1 `+Z` 使用统一 `scanHeightMm`，固定 50% 带间重叠并输出同向 `P0→P1...`。
- 接口/文件：修订 `scan_planner.h/.cpp`、双 JSON 序列化（项目 `scan_trajectory_v1` 与 InoRobot `sr2026-p-json-mvp-2`）、`mainwindow.h/.cpp` 双模式 UI 与两个原子导出按钮；扫描测试改为覆盖全局/局部、倾斜 WObj1、方向、编号、错误和 JSON 契约。
- 验证：源码树外 Debug 扫描测试构建与 `scan_planner_tests` 通过；`pointcloudview` Debug 目标编译通过；`git diff --check`、Release 全量 CTest、GUI 全流程本轮尚未完成。
- 风险：未验证真实相机有效宽度、机器人可达性、控制器写入、现场安全距离、碰撞和干涉；现有无关 `30_pointcloud_stitch` 修改保留。

<a id="evidence-0174"></a>

补充验证：完成源码树外 Debug/Release `pointcloudview` 目标构建、双配置全量 CTest（均 `16/16`）、双配置扫描规划定向测试和 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟；扫描测试另补充 WObj1 ABC 继承、空 ROI 拒绝和 InoRobot 局部 XYZ 转换断言。真实点云 GUI 全流程及机器人现场验收仍未完成。

<a id="evidence-0175"></a>

| 日期 | 主模块 | 变更与验证 | 风险/未完成 |
|---|---|---|---|
| 2026-09-18 | `30_pointcloud_stitch`（同步 `40_pointcloud_matching`） | 新增默认关闭的 2.5D 高程图路径、受限 coarse-to-fine 搜索、有效掩膜/去趋势/网格诊断、legacy 回退与 `visibility_hard_rejected`；应用 JSON/GUI 状态透传。Debug 全量 CTest 15/15，Debug/Release 定向测试各 2/2。源码树外真实回放：`1/test_1` 2.5D 退化后回退 `legacy_icp` 并生成正式 PLY；`5_40/test` 可见比例 `0.2246`，来源 `none`，硬拒绝且不生成正式合并 PLY。 | 未实现 pose graph、硬件时间戳、`Δt`、40 的工件移动判定；GUI 人工冒烟、PageHeap、现场夹具和便携 Release 部署未完成。Release 环境未找到 Visual Studio redist 子目录。 |

<a id="evidence-0176"></a>

重大目标、职责、数据契约或验收口径发生变化时，应在本节追加日期和简短说明，并同步更新受影响的架构、模块或接口文档。

<a id="evidence-0177"></a>

**九、PLY 临时工件线路迭代记录**

<a id="evidence-0178"></a>

主模块：`60_geometry_workpiece`；协作模块：`50_coordinate_conversion`、`10_pointcloud_io`；UI 编排：`apps/pointcloudview`。

<a id="evidence-0179"></a>

| 日期 | 主模块 | 变更与验证 | 风险/未完成 |
|---|---|---|---|
| 2026-09-20 | `60_geometry_workpiece`（协作 `50_coordinate_conversion`、`10_pointcloud_io`、`apps/pointcloudview`） | 现有“打开 PLY”入口改造为 PLY 临时工件线路：PLY 线路参数对话框（手眼 XML、Start/End 各 6 个位姿、输出目录、只读 LineProfileXz、手眼矩阵校验状态）→ 按 LineProfileXz 转换到 `robot_base` → 人工第一组三点后自动平面拟合 → 人工第二组三点同面校验 → 自动建立 WObj1、边缘分割、ROI、Mask → 由 `ArtifactTransaction` 一次性提交四件套。模块 60 新增内存扫描描述符路径并合并共享转换尾段，坐标数学完全复用模块 50，正式输出仍只经模块 10。 | 未执行 Debug/Release 构建、全量 CTest 与 GUI 人工验收；真实线扫 PLY、手眼 XML 和生产夹具未验证；Start/End 插值方向、标定方向与 WObj1 朝向仍需真实样件确认。 |
| 2026-09-21 | `apps/pointcloudview`（PLY/JSON 临时工件状态机） | 修复参数对话框取消后状态锁死、自动链同步失败悬空、旧会话串线和 JSON 位姿摘要顺序；新增 `None/Json/Ply` 单一会话来源、替换确认、统一会话失效和自动失败出口；文件夹扫描、多帧配准等新画布入口也遵循确认后失效旧临时工件会话的规则。源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过。 | GUI smoke、真实线扫 PLY/XML 与生产夹具仍待执行。 |

<a id="evidence-0180"></a>

新增行为要点：

<a id="evidence-0181"></a>

- “打开单个 PLY”进入 PLY 线路；文件夹扫描 PLY 仍是只读预览入口，行为不变。
- 每次打开 PLY 都要求重新选择手眼标定 XML，并在选择/变更时立即执行 `loadHandEyeCalibration()` 与 `validateRigidTransform()`。
- 输出目录在流程开始前校验：非空、非盘符根目录、非构建/缓存/源码树目录、且不存在四件套同名冲突；PLY 线路固定 `allowOverwrite = false`，不在自动执行中途询问覆盖。
- 两组三点是 PLY 线路唯一的人工步骤；第一组三点选满后自动拟合平面并自动进入第二组，第二组校验通过后自动完成剩余步骤。
- 现有扫描 JSON 入口、公共 JSON schema、四件套文件名与输出格式保持兼容。

<a id="evidence-0182"></a>

本轮交接记录（纯文本、含未验证项与接手建议）：`docs/pointcloudview_ply_line_handoff_2026-09-20.txt`。

<a id="evidence-0183"></a>

**2026-09-21（代码验收与发布准备）**

<a id="evidence-0184"></a>

- 验收范围：PLY/JSON 会话状态机、手眼转换成功/取消语义、PLY 读取取消透传、两组三点及自动 WObj1/边缘/ROI/Mask/四件套失败出口、JSON 位姿摘要顺序。
- 自动化结果：源码树外 Debug/Release 构建成功；Debug 和 Release 全量 CTest 均为 `15/15`；`git diff --check` 通过。
- 发布边界：本次仅提交上述 PLY/JSON 排错修复、相关测试和 Markdown 文档；工作区既有 `30_pointcloud_stitch`、PointCloudStitch、临时脚本和其他未提交修改不纳入本次提交。
- 未完成验收：GUI smoke、真实线扫 PLY/手眼 XML、机器人运动方向、WObj1 朝向和生产夹具现场数据仍需人工确认。

<a id="evidence-0185"></a>

**十、手眼标定统一与点云倒转修复迭代记录（2026-09-24）**

<a id="evidence-0186"></a>

主模块：`50_coordinate_conversion`；调用方清理：`apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`；复用入口核对：`apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`、`apps/pointcloudstitch` UI 与回归。

<a id="evidence-0187"></a>

- **黄金基准**：扫描 JSON 路径（`prepareTempWorkpiece()` → `transformLineScanToRobotBase()`）是手眼标定黄金基准；离线 JSON、PLY、多帧和拼接路径必须与它逐点一致。
- **根因**：点云上下/法向反转风险来自“重复实现 + LineProfileXz 语义不一致”，不是矩阵方向问题：回归工具自带 XML 解析与 `poseMatrix()`，view 应用自带刚体校验、SLERP 插值、进度轴/`EmbeddedMotion` 启发式和内联 `baseFromFlange * flangeFromDepth`，旧合并路径还把输入 Y 当相机坐标保留。
- **修复**：模块 50 成为唯一数学实现（`composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus` 状态码）；两个调用方的重复解析、重复插值和内联矩阵乘法删除并改调模块 50。未使用矩阵求逆、Z 翻转或镜像作为修复手段。
- **契约**：`RTmatDepth2robot` = `T_flange_depth`；坐标链 `p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`；位姿 `[X,Y,Z,A,B,C]`、`A=Rx/B=Ry/C=Rz`、`Rz(C)*Ry(B)*Rx(A)`、mm/deg、PLY/PNG/JSON 格式与既有错误码均未变化。
- **测试**：`handeye_transform_tests` 覆盖 12 项契约（乘法方向、黄金链路、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、正负行程与反向扫描、FullXyz、法向同旋转不平移、位姿往返、非法矩阵拒绝、无行程/空点云/全无效/超范围/取消、状态码）；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口的 robot_base 逐点一致性断言。
- **验证**：源码树外 Debug/Release 构建成功；Debug/Release 全量 CTest 均 `16/16`；`handeye_transform_tests`、`temp_workpiece_interface_tests`、`stitching_interface_tests`、`multiframe_registration_tests` 定向运行 `4/4`；`pointcloudview` Debug/Release 自关闭启动冒烟均 `ExitCode=0`。真实样本共声明 `6,300,000` 点、有效转换 `4,389,670` 点、无进度越界点；JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因本机缺少 Visual Studio release redistributable 发出警告，未完成便携部署验收。
- **风险/未验证**：真实手眼 XML 的物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一、真实机器人安全/可达性/碰撞与干涉均未验证；本轮所有结论只来自源码树外构建、单元/接口回归和启动冒烟。

<a id="evidence-0188"></a>

- 主模块：`60_geometry_workpiece`；渲染模块：`20_pointcloud_render`；UI 编排：`apps/pointcloudview`。
- 新增 `pcv::texture::mapPlaneBrightness()`：读取同次扫描 PNG/JPG/BMP，转换 `QImage::Format_Grayscale8`，按原始 PLY `sourceIndex` 的左上角 row-major 像素直接采样；只为现有 `planeIndices` 点生成灰度/有效标记，严格校验原始点数、source index 和过滤统计，不执行局部坐标投影或裁剪。
- `PointCloudCanvas` 新增 per-point 颜色覆盖与“纹理灰度”模式；有效纹理点使用灰度，非平面点/无效点回退当前颜色模式；点云、平面和坐标系更新会清理旧纹理缓存，切换其他颜色模式后可复用缓存。
- `pointcloudview` 增加“选择亮度图并贴到平面”入口，后台映射、取消/revision 门禁、替换保护和状态栏统计；不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- 文件：`modules/60_geometry_workpiece/include/pcv/texture/texture_mapping.h`、`modules/60_geometry_workpiece/src/texture_mapping.cpp`、`tests/unit/texture/texture_mapping_tests.cpp`、渲染契约/画布、`apps/pointcloudview/mainwindow.*`、对应 CMake/UI 和文档。
- 验证：源码树外 `C:\pcv-build\texture-full-debug` Debug 构建成功，CTest `17/17` 通过；`C:\pcv-build\texture-full-release` Release 构建成功，CTest `17/17` 通过；纹理映射和渲染契约定向测试均通过。
- 风险/未完成：真实 PLY + 亮度图 GUI 方向、中心裁剪视觉效果、无 WObj1 回退轴和大点云性能未人工验收；Release `windeployqt` 提示缺少本机 Visual Studio release redistributable；未接入实时相机取流。

<a id="evidence-0189"></a>

- 将亮度纹理从局部坐标覆盖整张图改为严格 `sourceIndex -> image pixel` 映射。仅支持单个 PLY 临时工件流程；亮度图必须与原始 PLY 同次扫描、同尺寸、同 row-major 点序。
- 映射只读取 `planeIndices` 对应的原图像素，使用原始 PLY 点数校验图像像素数，拒绝过滤点、缺失/重复/越界 source index、点数变化和当前点序回退；不再执行局部坐标投影、包围盒覆盖、中心裁剪、缩放、翻转或配准。
- WObj1 不参与纹理采样；WObj1 建立/修改不会清除已有纹理。点云、平面索引或 source provenance 变化仍会清除纹理；边缘结果在平面索引不变时保留纹理。
- 验证：源码树外 `C:\pcv-build\texture-raster-debug` Debug 构建成功，CTest `17/17` 通过；`C:\pcv-build\texture-raster-release` Release 构建成功，CTest `17/17` 通过；两配置 `texture_mapping_tests.exe` 均退出码 `0`；两配置 `pointcloudview.exe` 在 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 下启动冒烟均退出码 `0`；`git diff --check` 通过。
- 统计修正：映射结果的 `skippedPointCount` 按平面点总数减成功映射数报告；严格栅格校验失败时不发布覆盖。
- 风险/未完成：未使用真实 PLY 与同次扫描亮度图进行人工 GUI 视觉验收，无法在本轮确认现场图像与 PLY 的 row-major 起点/点序确实一致；未接入实时相机取流；Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable。

<a id="evidence-0190"></a>

**2026-10-07（点云预览工作台 UI 简化）**

<a id="evidence-0191"></a>

- 主模块：`apps/pointcloudview` UI 编排；不修改模块 10、30、50、60 的算法和公共 API。
- 主窗口删除左侧常驻总控中心、步骤导航和文件列表可见布局，顶部保留“打开点云”“打开扫描 JSON”和“工具”菜单；深色 3D 画布占据主体，文件名、点数和任务状态通过状态栏显示。
- 现有 Designer 控件被迁移到五个独立、无模式且可同时打开的工具窗口：数据源、点云清理、平面与 WObj1、边缘与输出、扫描规划；窗口关闭只隐藏，参数、结果和后台任务仍由 `MainWindow` 保留。渲染设置继续复用 `RenderDialog`，相机和多帧配准保留原业务入口。
- 取点或边缘选择时自动隐藏对应工具窗口，主窗口底部显示状态、计数、撤销、取消和确认/完成操作条；Esc、Backspace 及自动 PLY/扫描 JSON 流程继续使用原状态机和 revision 门禁。
- 验证：`C:\pcv-build\ui-simplify-debug`、`C:\pcv-build\ui-simplify-release` 构建成功；Debug/Release 全量 CTest 均 `17/17`；两配置 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟退出码均为 `0`。
- 风险/未完成：本轮未使用真实点云逐一人工验收多窗口布局、预览占客户区 85% 的像素比例、工具窗口关闭恢复和取点交互；真实相机、机器人可达性、碰撞/干涉及现场数据仍需验收；Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable。

<a id="evidence-0192"></a>

- 增加“PLY 纹理实验（原始坐标）”来源：单个 PLY 直接保留原始 XYZ、点序、`declaredPointCount` 和 identity `sourceIndex`，不读取 XML，不执行手眼转换、Start/End 位姿、WObj1 或临时工件四件套。
- 实验模式复用两组三点和同面校验；第一组三点按任意平面方向的垂直距离拟合，第二组通过后直接选择同次扫描亮度图。
- 复用严格 `source_raster_index` 映射：图像像素数必须等于 PLY 声明点数，固定左上角起始 row-major；仅 `planeIndices` 对应点显示灰度，失败、尺寸不符或取消不发布新纹理。
- 新增 headless `texture_experiment_tests`，覆盖声明点数、identity source index、垂直平面、同面校验、严格像素映射、取消和尺寸错误拒绝。
- 本轮验证：源码树外 Debug 构建成功，Debug 全量 CTest `18/18` 通过。Release 构建、Release CTest、真实 PLY/亮度图 GUI 视觉验收和现场 row-major 一致性仍待完成。

<a id="evidence-0193"></a>

**2026-10-08（扫描规划单线误生成双线修复）**

<a id="evidence-0194"></a>

- 主模块 `60_geometry_workpiece`；修改 `scan_planner.h/.cpp`、`scan_planner_tests.cpp` 和模块/架构/数据契约文档。
- 原严格比较 `requiredWidth > cameraWidthMm` 在 ROI 投影宽度略超过视野时立即增加第二条线；新增固定 `0.1 mm` 容差，宽度不超过 `cameraWidthMm + 0.1 mm` 只输出 `P0/P1`，超过阈值仍按实际覆盖要求规划多条线。
- 原始 `requiredWidthMm` 不截断；结果和内部 JSON 记录 `singleLineWidthToleranceMm` / `single_line_width_tolerance_mm`，不新增 UI 参数，不改变 P.json 的 schema、编号和姿态顺序；单线重叠字段沿用请求值，不表示存在相邻扫描带。
- 测试覆盖 Global/LocalRoi 的 `99.9/100/100.099/100.1/100.1001/101 mm`、单线段数、P.json 点数/编号以及多线首尾覆盖与重叠保证；真实相机有效视野和现场 ROI 边界误差仍需验收。
- 本轮验证：复用源码树外 `C:\pcv-build\texture-experiment-debug-v1` / `C:\pcv-build\texture-experiment-release-v1` 执行完整 Debug/Release 构建成功；Debug 扫描定向测试通过，双配置全量 CTest 均 `18/18`（Debug `82.23 s`、Release `8.36 s`），均含扫描边界与 JSON 回归；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 双配置启动冒烟退出码 `0`，`git diff --check` 通过。
- 验证边界：未使用触发问题的真实 ROI 进行 GUI 复验；容差最多接受总宽 `0.1 mm` 的视野超出，真实相机有效宽度仍需现场确认。Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable，便携部署未验收。现有无关修改保留，未提交。

<a id="evidence-0195"></a>

**十一、2026-10-09：文档整理与下一阶段开发登记**

<a id="evidence-0196"></a>

- 范围：全仓既有 47 个 Markdown 文档（含本文件）核对，新增正式 v0.4 需求基线、10/60 规范模块文档及 v0.4 收敛开发方案；本轮没有代码或 CMake 更新。
- 当前状态：10/20/30/50/60 已有实现，40 仅兼容聚合、移动判定未实现；扫描规划与外部全剖/点剖刀路规划分开；原始坐标纹理仍属实验。生产 namespace/API 与工具 target 迁移尚未收敛。
- 文档治理：当前文档顶部给出规范入口、日期与状态；历史正文、旧目录/target 和历史测试结论保留追溯。新接口只能在实际实现后标记完成。
- 后续顺序：公共边界与兼容映射 → 生产调用方迁移 → 兼容引用审计与独立验收；方案见 [module_convergence_v0.4.md](../development/development_plan_v0.5.md)。本次仅交付方案，不实施代码迁移或删除兼容层。
- 验证：51 份 Markdown 落盘内容核对一致，161 个本地 Markdown 链接目标存在；一级标题、代码围栏和替换字符检查通过，未发现锚点链接；`git diff --check` 通过。不运行构建、CTest 或 GUI。历史记录中的 `15/15`、`16/16`、`17/17`、`18/18` 等不代表本次验证，也不代表当前所有配置的测试数量。
- 工作区：本次仅产生 Markdown 修改及新增，未提交；检查时另见 `.pcv_tmp_build.bat`、`.pcv_tmp_configure.bat`、`.pcv_tmp_ctest.bat`、`.pcv_tmp_run.bat` 删除状态，本次未操作这些文件。
- 风险：真实点云 GUI 全流程、实际控制点与 Start/End、现场 row-major 一致性、手眼物理方向、机器人可达性/碰撞、Release 便携部署和外部刀路 schema/页面形态仍待验收或冻结。整理记录不改变这些结论。

<a id="evidence-0197"></a>

**本轮最终验收**

<a id="evidence-0198"></a>

- 源码外Debug/Release完整构建成功，NMake Makefiles、MSVC19.51.36252.0、Qt6.8.3；双配置全量CTest均21/21。拼接接口/seam/多帧/新算法/新会话5项各100次，合计500次无失败，1156.95 s。命令和日志见上述开发记录。
- pointcloudview双配置windows平台自关闭启动ExitCode=0。新worker const共享副本串并行1/2/4等价、来源映射及取消/revision/关闭测试通过。
- 用户已明确真实点选留待现场；样本1几何质量、≤0.1 mm目标及性能中位数未验证，真实GUI、PageHeap、生产夹具和便携部署也未验证。保留测量起伏及潜在残余错位，不宣称消除双层。工作区只保留本任务代码/测试/文档修改，不提交/推送/上传。

已合并的重复证据：[evidence-0103](history_pre_v0.5.md#evidence-0103)、[evidence-0097](history_pre_v0.5.md#evidence-0097)、[evidence-0105](history_pre_v0.5.md#evidence-0105)、[evidence-0107](history_pre_v0.5.md#evidence-0107)、[evidence-0109](history_pre_v0.5.md#evidence-0109)、[evidence-0113](history_pre_v0.5.md#evidence-0113)、[evidence-0114](history_pre_v0.5.md#evidence-0114)、[evidence-0117](history_pre_v0.5.md#evidence-0117)、[evidence-0118](history_pre_v0.5.md#evidence-0118)。

<a id="source-03"></a>

## docs/requirements/pointcloudview_v0.4.md

原始来源：`docs/requirements/pointcloudview_v0.4.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0208"></a>

**1. 文档职责与目标**

<a id="evidence-0209"></a>

源码、CMake、公共头和测试定义实际能力。本文件负责当前范围、实现状态与完成条件；[模块/UI 协同文档](../architecture/module_ui_integration_v0.4.md) 负责命名、依赖、所有权与交接；[v0.3 数据契约](../requirements/pointcloudview_v0.3.md) 保留既有 schema、文件名、坐标、错误码及历史增量；[target.md](../../target.md) 负责业务目标与追加式变更记录。[开发方案](../development/development_plan_v0.5.md) 登记尚未实施的收敛任务。

<a id="evidence-0210"></a>

`pointcloudview` 的目标是统一点云处理、局部标注、外部全剖/点剖刀路结果接入与展示。后端负责人为陈振佳，前端/外部项目负责人为于海军；两方共同负责接口冻结与端到端验收。目标链路中的外部项目步骤不能写为本仓库已实现。

<a id="evidence-0211"></a>

**2. 模块与能力状态**

<a id="evidence-0212"></a>

| module | target | current_state | target_state |
|---|---|---|---|
| 10 `pointcloud_io` | `pcv_m10_pointcloud_io` | PLY/缓存、过滤、相机帧、writer、事务已有实现；相机实验受开关控制 | 规范 IO API 与生产调用收敛 |
| 20 `pointcloud_render` | `pcv_m20_pointcloud_render` | Qt/OpenGL 画布、点选、纹理灰度覆盖已有实现 | 完成真实桌面交互和纹理视觉验收 |
| 30 `pointcloud_stitch` | `pcv_m30_pointcloud_stitch` | 2026-10-09 算法恢复到 `6329426`：ICP、多帧、seam、manifest 与事务编排；后续 2.5D/快速模式已回滚 | 分组请求成为主实现，旧入口仅适配 |
| 40 `pointcloud_matching` | `pcv_m40_pointcloud_matching`（`INTERFACE`） | 仅聚合 30/60；业务未实现 | 后续版本定义位置确认与移动判定 |
| 50 `coordinate_conversion` | `pcv_m50_coordinate_conversion` | 唯一手眼、插值与坐标数学实现 | `pcv::coordinates` 公共门面与生产调用收敛 |
| 60 `geometry_workpiece` | `pcv_m60_geometry_workpiece` | 平面/边缘/ROI/WObj、临时工件、扫描规划、严格栅格纹理已有实现 | `geometry/workpiece` 公共门面与交接收敛 |
| 70/80（历史模块） | 兼容 `INTERFACE` target | 分别转发 60/10 | 保留兼容周期，独立审查后才可清理 |
| 90/100 | 无功能 target | 干涉平面/质量报告未实现 | 后续独立需求与验收 |

<a id="evidence-0213"></a>

依赖为 `30 -> 10/50`、`60 -> 10/50`、`40(兼容) -> 30/60`、`20 -> core`。共享模块不依赖 `apps/`；最终文件操作由 10 `ArtifactTransaction` 实施。现有 30/60 入口仍会编排事务，并非全部是纯结果接口。

<a id="evidence-0214"></a>

**3. 当前业务链与契约**

<a id="evidence-0215"></a>

- PLY/扫描 JSON 临时工件：读取 → 50 转换到 `robot_base` → 两组三点拟合/同面校验 → WObj1、边缘、ROI/Mask → 10 原子提交 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`。会话取消、输入 revision 变化与同步失败不可发布部分四件套。
- 手眼链固定为 `T_base_depth = T_base_flange * T_flange_depth`，`RTmatDepth2robot=T_flange_depth`。`LineProfileXz` 用 `[X,0,Z]`，输入 Y 仅表示 `y/signedTravel`；`FullXyz` 保留完整 XYZ。控制器位姿 `[X,Y,Z,A,B,C]`，`A=Rx/B=Ry/C=Rz`，`Rz(C)*Ry(B)*Rx(A)`；长度 mm、角度 deg。
- 扫描规划是相机扫描轨迹，支持 `Global/LocalRoi`，UI 默认 LocalRoi；范围来自 WObj1 XY 投影，默认高度/相机宽度/重叠/两端延长为 `50/100/50/50 mm`，单线容差 `0.1 mm`，产品固定 `-X→+X` 线序与 `+Y→-Y` 行程。输出项目 `scan_trajectory_v1` 与 InoRobot `sr2026-p-json-mvp-2`；P[10] 起始、最多 9990 点、XYZ 为 WObj1 局部坐标，姿态 `[X,Y,Z,RZ,RY,RX]`，不写控制器。
- 纹理采用严格 `source_raster_index`：原始 PLY 点数等于亮度图像素数，左上角 row-major，仅当前平面点接收灰度；不裁剪/缩放/翻转/配准，无合法 provenance 或经过过滤/降采样/拼接时拒绝。`PLY 纹理实验（原始坐标）` 是独立实验入口，不转换到 `robot_base`、不建立 WObj1、不输出四件套。
- 拼接算法与公共类型恢复到 `6329426`；GUI 默认完整配准，保留仅手眼转换，恢复原严格质量判定、seam/preview 和事务输出策略。共享模块 50 当前手眼实现及输出覆盖确认保留；2.5D/快速模式增量已回滚。
- 输出 PLY 为 binary little-endian；正式 Mask PNG 为 Grayscale8 0/255，显示用亮度灰度可为 0～255。JSON 字段、文件名、错误码和原有默认值以已发布契约与对应公共头为准；本次整理不修改 schema。新增扫描诊断字段和拼接模式是各日期记录的增量，不是本次新增能力。

<a id="evidence-0216"></a>

**4. 未实现边界与 UI 交接**

<a id="evidence-0217"></a>

40 正式目标声明：对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

<a id="evidence-0218"></a>

40 当前为“规划目标，未实现”：无基准模板持久化、基准比较、移动阈值/6DoF 判定、机器人执行/修正下发或新输出闭环。现有临时工件的 baseline 文件不构成已经实现的 40 移动判定。

<a id="evidence-0219"></a>

主线维护 `MainWindow` 壳、生命周期、共享模块与稳定 API。同事工作区为 `features/**`、登记的 UI 专属 `.ui`/资源/样式以及 UI 测试/说明；临时 adapter 仅在 `adapters/**`，带 owner、日期、替代 API 与删除条件。外部高点标注、全剖/点剖刀路项目由同事负责，其 schema、部署接入方式与页面形态仍待冻结；扫描规划不替代刀路规划。

<a id="evidence-0220"></a>

**5. 完成条件与验证状态**

<a id="evidence-0221"></a>

文档整理完成条件：全仓 Markdown 均登记状态/规范入口，链接与目录映射有效，当前/目标/历史明确，更新记录进入 `target.md`。

<a id="evidence-0222"></a>

代码收敛完成条件：生产调用迁移到正式公共 API、旧入口仅适配、无重复算法，取消/错误/事务与来源映射保持兼容，迁移批次与回滚点有记录。完整软件验收还需独立执行源码树外 Debug/Release 构建、CTest、GUI smoke、真实数据与高倾角基线回放；本轮按用户要求只静态检查，不执行这些命令。

<a id="evidence-0223"></a>

历史验证数量仅对应当时源码/选项。当前源码新增的测试与未完成性能/真实数据验收不能借用旧结论；相机、机器人可达性/碰撞、便携部署和外部项目联调仍需独立验收。

<a id="source-04"></a>

## docs/requirements/pointcloudview_v0.3.md

原始来源：`docs/requirements/pointcloudview_v0.3.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0225"></a>

**2026-10-09：PointCloudStitch 算法契约回滚**

<a id="evidence-0226"></a>

- 模块 30 的公共类型与 PLY/preview/映射/manifest 输出编排恢复到 `6329426`；后续 heightmap/fine 参数、诊断和报告增量不再属于当前实现。下方对应增量记录保留追溯。
- 坐标约定、模块 50 统一手眼数学、模块 10 事务契约及其他模块保持当前版本。具体范围、验证和风险见 `docs/modules/30_pointcloud_stitch.md` 本轮记录。

<a id="evidence-0227"></a>

**2026-10-08：位姿粗配准＋重叠区快速精配准增量契约（已回滚）**

<a id="evidence-0228"></a>

- `RegistrationProfile::{Legacy,PoseOverlapFine}` 及 fine 两层/迭代/预算选项追加到 `IcpOptions` 尾部，API 默认 Legacy；默认数值为体素 0.5/0.2 mm、距离 3.0/0.6 mm、20/15 迭代、10 mm/1°。只解可观测 6DoF，不改变手眼/ABC/单位/PLY/provenance。
- 新路径的质量政策为 warn-only；至少 100 个有效对应是求解条件，不是正式输出的旧结构门槛。无解/越界/无重叠回退机器人位置并记录锚点重置；非有限输入、取消、映射和输出错误仍硬失败。Legacy 原验收保持不变。
- `fineApplied/poseFallback/chainAnchorReset/structuralMetricsComputed/fineReason/pairCorrection/cumulativeCorrection` 追加诊断尾部。`accepted` 保留旧质量含义，不用它表示新模式文件提交权限；新模式严格验收未执行，旧覆盖指标报告未计算。
- `StitchingArtifactGroup.writePreview=true` 兼容旧调用，GUI 新模式关闭。正式输出沿用 `stitched_robot_base.ply/.pcvmap` 与 `stitching_manifest.json`，报告新增模式、质量政策、严格状态、逐对两层指标/矩阵、完成与回退数、输入位姿/过滤统计和阶段计时；全部有效点保留。
- 不设置累计漂移上限，不以正式文件生成证明正确对齐。真实样本回放缺 Start/End 位姿时必须标为未验证。构建/测试结果见本轮最终验证记录。

<a id="evidence-0229"></a>

版本：v0.3
最后核对日期：2026-10-09（文档静态核对）
状态：数据契约基线（v0.3）；架构和 UI 交接已进入 v0.4，发布验收待完成

<a id="evidence-0230"></a>

**1. 目标和事实来源**

<a id="evidence-0231"></a>

v0.3 继续作为 JSON、PLY、坐标、错误码和默认数值的数据契约基线；模块职责、目录、target 和 UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准。源码、CMake 和测试优先于历史文档。

<a id="evidence-0232"></a>

当前未实现模块为 `90_interferenceplane` 和 `100_qualityreport`。本版本不为这两个模块建立功能 target，也不报告虚假成功。

<a id="evidence-0233"></a>

**2. 模块基线**

<a id="evidence-0234"></a>

| 模块 | 职责 | 规范 target | 状态 |
|---|---|---|---|
| `10_pointcloud_io` | PLY/缓存、相机输入、过滤、PNG/PLY/JSON writer、`ArtifactTransaction` | `pcv_m10_pointcloud_io` | 已实现；相机为可选实验能力 |
| `20_pointcloud_render` | Qt/OpenGL 画布、VBO、点选 | `pcv_m20_pointcloud_render` | 已实现，桌面验收待完成 |
| `30_pointcloud_stitch` | 多帧拼接唯一编排：读取、ICP/多帧配准、坐标转换调用、seam fusion、manifest 与事务提交 | `pcv_m30_pointcloud_stitch` | 已实现 |
| `40_pointcloud_matching` | 工件位置确认规划占位；本版不实现移动判定或新输出闭环 | `pcv_m40_pointcloud_matching`（`INTERFACE`） | 规划中，未实现 |
| `50_coordinate_conversion` | 手眼标定、位姿插值、坐标转换（唯一数学实现） | `pcv_m50_coordinate_conversion` | 已实现 |
| `60_geometry_workpiece` | 平面、边缘、ROI、WObj、临时工件流程、扫描路径规划 | `pcv_m60_geometry_workpiece` | 已实现 |
| 历史 `70_roi_template` | 实现已并入 60，保留兼容转发 | `pcv_m70_roi_template` | 兼容入口 |
| 历史 `80_plane_output` | writer/事务已并入 10，保留兼容转发 | `pcv_m80_plane_output` | 兼容入口 |
| 相机能力（原 `05_camerainput`） | 海康相机取流（实验扩展），已并入 `10_pointcloud_io` | 同 `pcv_m10_pointcloud_io` | 实验中，不纳入发布基线 |
| `90_interferenceplane` | 干涉平面检查 | 无 | 未实现 |
| `100_qualityreport` | 质量报告 | 无 | 未实现 |

<a id="evidence-0235"></a>

迁移期兼容 target：旧模块名 `pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，以及 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`（仅转发）。兼容层在所有调用方迁移并完成独立审计前保留。

<a id="evidence-0236"></a>

**3. 数据流程**

<a id="evidence-0237"></a>

**2026-09-18：2.5D 高程图配准增量契约**

<a id="evidence-0238"></a>

- `30_pointcloud_stitch` 新增可选 2.5D 高程图配准路径，`IcpOptions.heightMapRegistrationEnabled` 默认 `false`；关闭时旧 ICP 行为和输出保持不变。
- 新路径只允许受限 `Δx/Δy` 与小范围 `Δz/yaw`，网格分辨率自动估计并限制 `0.5~2.0 mm`，有效掩膜缺失网格不参与评分。`IcpDiagnostics` 和回归/GUI JSON 透传网格、有效比例/点数、评分前后、改善率、受限 ICP 层数、来源和拒绝原因。
- 选择规则固定为：2.5D 全部验收通过选 `heightmap_restricted_icp`；2.5D 失败且旧 ICP 通过选 `legacy_icp`；两者失败选 `none`。最终有效可见比例 `<0.25` 时设置 `visibility_hard_rejected` 并阻止正式输出，不能用命中点内覆盖率绕过。
- 本增量不改变手眼矩阵、机器人位姿语义、PLY 编码、`robot_base` 坐标约定，也不实现 pose graph、硬件时间戳、`Δt` 估计或 40 的工件移动判定。
- 验证记录：Debug 全量 CTest 15/15；Debug/Release 定向 `multiframe_registration_tests` 与 `stitching_interface_tests` 各 2/2。源码树外回放中 `1/test_1` 成功并回退 `legacy_icp`；`5_40/test` 以可见比例 `0.2246` 硬拒绝且无正式合并 PLY。GUI 人工、生产夹具和便携 Release 部署尚未验收。

<a id="evidence-0239"></a>

```text
10 输入/相机采集/预处理
    ↓
20 渲染与交互
    ↓
60 平面、边缘与工件几何
    ↓
60 ROI/WObj/临时工件
    ↓
50 坐标转换与 60 领域结果
    ↓
30 多帧拼接、ICP、seam fusion、manifest
    ↓
10 ArtifactTransaction 成套提交
```

<a id="evidence-0240"></a>

单帧处理：输入校验 → `10` 读取/缓存/预处理 → `20` 渲染和点选 → `50` 坐标转换 → `60` 平面/边缘/ROI/WObj/临时工件 → `10` 事务成套输出。

<a id="evidence-0241"></a>

多帧处理：`30` 是唯一拼接编排：组织输入并读取 PLY，调用 `50` 转换，执行 ICP/多帧配准和 seam fusion，生成 manifest 后由 `10` 提交正式文件。取消或失败时不得发布部分结果或复用旧正式输出。

<a id="evidence-0242"></a>

**4. 稳定契约**

<a id="evidence-0243"></a>

- 长度单位为 mm，角度单位为 deg。
- 位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 手眼标定方向为 `RTmatDepth2robot` = `T_flange_depth`（flange_from_depth），坐标链固定为
  `p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`（即 `T_base_depth = T_base_flange * T_flange_depth`）。
  禁止默认对手眼矩阵求逆，禁止固定 Z 轴翻转、镜像或 180° 旋转补丁。
- 扫描 JSON 路径是手眼标定的黄金基准；离线 JSON、PLY 临时工件、多帧和拼接路径必须复用模块 50 的同一实现并与它逐点一致。
- `LineProfileXz` 的相机点为 `[X,0,Z]`，输入 Y 只表示扫描进度（`ratio = input.y / signedTravel`，负行程表示反向扫描）；
  `FullXyz` 保留完整 XYZ。点和法向使用同一刚体旋转，法向不做平移。
- 平面输出为 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- PNG、PLY、JSON 先写入 staging 目录，全部成功后成套提交；任一失败必须回滚。
- 共享库不弹窗、不访问 QWidget；应用负责提示和线程生命周期。
- `PointCloudCanvas` 实现位于 `modules/20_pointcloud_render/src/pointcloud_canvas.cpp`。
- 诊断工具优先通过 `pcv::interface::StitchingRequest` / `stitch()` 使用统一拼接接口；`stitchRawLineProfiles` 仅为兼容入口。
- 40 的正式目标是生成含 `robot_base` 的平面点云和坐标元数据以支持未来移动比较；当前不实现模板持久化、比较、阈值、机器人下发或新闭环。

<a id="evidence-0244"></a>

**4.1 扫描路径 JSON 增量契约（2026-09-30）**

<a id="evidence-0245"></a>

- schema 为 `scan_trajectory_v1`，`kind=robot_scan_trajectory`，字段使用 `snake_case`。
- 机器人位姿数组固定为 `[X,Y,Z,A,B,C]`，单位分别为 mm/deg。WObj1 X 是横向相机覆盖轴，Y 是扫描行程轴；扫描线按 `-X → +X` 编号并全部同向。全局使用拟合平面有效点，局部使用当前 ROI 有效点。
- `center_pose` 是投影包围盒中心沿 WObj1 `+Z` 偏移 `scanHeightMm` 后的最终中心位姿；`required_area.width_mm`、`required_area.travel_length_mm` 由有效点自动计算，另有 `travel_extension_mm` 与 `final_travel_length_mm`；默认 `camera_width_mm=100`、`requested_overlap_mm=50`，`actual_overlap_mm`、`line_spacing_mm` 和 `scan_line_count` 描述扫描带。
- `travel_direction` 产品固定为 `positive_y_to_negative_y`；每条线起点为 `vMax+50`、终点为 `vMin-50`。`points` 按 P 点顺序保存 `point_id`、`point_name`、`robot_pose`、`scan_line_index`、`line_endpoint`；`segments` 保存每条线的起止 P 点。
- 项目 JSON 使用 `scan_trajectory_v1`；InoRobot 兼容 `P.json` 使用 `sr2026-p-json-mvp-2`、`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`、点号从 10 开始且最多 9990 点，XYZ 转为 WObj1 局部坐标，姿态重排为 `[X,Y,Z,RZ,RY,RX]`。本期不调用控制器写入 API。
- 两种扫描 JSON 都由 `ArtifactTransaction` 原子提交。失败、取消或 revision 过期不得发布旧结果或部分文件，覆盖必须由用户明确确认。

<a id="evidence-0246"></a>

验证记录：2026-09-30 源码树外 Debug/Release 构建成功，`scan_planner_tests` 双配置通过，全量 CTest 双配置均 `16/16`，`pointcloudview` 双配置自关闭启动冒烟退出码 `0`，`git diff --check` 通过。人工 GUI 流程因 computer-use connector 初始化错误（`unsupported Codex auth method: apikey`）未完成；真实点云 ROI 和设备现场行为仍需验收。

<a id="evidence-0247"></a>

详细字段和错误码以模块公共头、模块文档和测试源码为准。

<a id="evidence-0248"></a>

**5. 开发和验收**

<a id="evidence-0249"></a>

每次需求登记主模块编号，修改公共头、CMake 或跨模块接口时列出影响模块和回归测试。模块文档追加日期、需求、文件、行为、验证和风险。

<a id="evidence-0250"></a>

验证顺序：模块测试 → 直接依赖构建 → 应用构建 → Debug/Release 全量 CTest → GUI 或真实夹具验收 → 文档和 Git 检查。

<a id="evidence-0251"></a>

当前自动化测试用于验证共享算法和接口。Windows 桌面点选、矩形选区、旋转/平移/缩放、关闭生命周期以及真实生产拼接夹具仍需单独验收。

<a id="evidence-0252"></a>

**6. 兼容层清理条件**

<a id="evidence-0253"></a>

只有在所有公共 include 和生产调用方切换到规范模块路径、诊断工具仅使用模块 API、应用不再包含重复算法、Debug/Release 构建和全量 CTest 通过，并完成 GUI/夹具验收后，才能单独删除兼容 target 和 forwarding header。

<a id="evidence-0254"></a>

**7. 发布状态**

<a id="evidence-0255"></a>

当前状态为“迁移完成，发布验收待完成”。未完成真实 GUI 验收和生产夹具逐字段比对前，不标记为 Release Candidate。

<a id="evidence-0256"></a>

**8. 变更记录**

<a id="evidence-0257"></a>

- 单线判定改为 `requiredWidthMm <= cameraWidthMm + 0.1 mm`，吸收严格浮点比较造成的单线/双线边界跳变；超过阈值仍按实际宽度保证多带覆盖。
- 内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm`，保留原始范围；单线时 `actual_overlap_mm` 沿用请求值，扫描线数为 1 时没有相邻带的实际重叠。InoRobot `P.json` 字段、编号和姿态顺序保持兼容。
- 回归覆盖两种模式的 `99.9/100/100.099/100.1/100.1001/101 mm`、P 点与段数量及 P.json 点数；真实相机有效视野和现场 ROI 边界仍需设备验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`（含扫描边界回归），双配置应用启动冒烟退出码 `0`，`git diff --check` 通过；真实 ROI GUI 复验及 Release 便携部署未完成。

<a id="evidence-0258"></a>

- 登记主模块 `50_coordinate_conversion`；调用方清理涉及 `apps/pointcloudstitch/regressionrunner.cpp` 与 `apps/pointcloudview/pointcloudprocessor.cpp`；相关入口 `apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`。
- 根因：坐标转换存在重复实现（回归工具自带的 `RTmatDepth2robot` 解析与 `poseMatrix()`、view 应用自带的刚体校验/SLERP/进度启发式/内联矩阵乘法），且旧合并路径的 LineProfileXz 语义与 JSON 黄金路径不同（把输入 Y 当相机坐标保留、按进度轴与 `EmbeddedMotion` 启发式可选位姿）。点云上下/法向反转风险来自这两点，而不是矩阵方向。
- 修复：模块 50 成为唯一数学实现，新增 `composeBaseFromDepth()`（固定 `T_base_depth = T_base_flange * T_flange_depth`）、`computeScanTravel()`（带符号行程）、`lineProfileCameraPoint()`/`lineProfileScanRatio()`（`[X,0,Z]` 与 `y/signedTravel`）、`RobotCloudStatus` 状态码；两个调用方的重复实现删除，改为调用模块 50。未使用求逆、Z 翻转或镜像作为修复手段。
- 契约影响：位姿顺序 `[X,Y,Z,A,B,C]`、旋转约定、单位、PLY/PNG/JSON 文件格式、四件套文件名和既有错误码均未变化；模块 50 的改动为增量符号，`transformPointToRobotBase()` 仅追加可选 `QString *error` 尾参；`apps/pointcloudview/pointcloudprocessor.cpp` 的合并缓存版本号从 8 提升到 9 以失效旧算法缓存。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`；4 项直接相关测试定向运行 `4/4`；`pointcloudview` Debug/Release 启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio release redistributable 发出警告，便携部署未验收。
- 未验证：真实手眼 XML 的物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一、真实机器人安全/可达性/碰撞与干涉，均未在本轮范围内。

<a id="evidence-0259"></a>

**2026-09-20（PLY 临时工件线路接入：打开 PLY → robot_base → 四件套）**

<a id="evidence-0260"></a>

- 登记主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`（唯一坐标转换实现）、`10_pointcloud_io`（`ArtifactTransaction` 与 PLY 读写）、`apps/pointcloudview`（参数收集、状态编排与结果展示）。
- 数据契约不变：四件套文件名 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`；binary little-endian PLY；Grayscale8 PNG 且像素只取 0/255；JSON 仍为 `sr2026-temp-workpiece-info-mvp-2`，字段名、精度（WObj1 六元组保留 3 位小数）与错误码未变化；未创建新的 PLY 专用 schema。
- 位姿与坐标约定不变：`[X,Y,Z,A,B,C]`，`A=Rx`、`B=Ry`、`C=Rz`，旋转 `Rz(C)*Ry(B)*Rx(A)`，长度 mm、角度 deg；PLY 线路固定按 `LineProfileXz` 解释，不提供 `FullXyz` 切换。
- 新增行为：现有“打开单个 PLY”入口改为 PLY 线路（文件夹扫描 PLY 仍是只读预览）。参数对话框强制选择手眼 XML、手工输入 Start/End，并显示标定绝对路径、有效性、`camera -> robot_base` 方向与失败原因；输出目录在流程开始前校验（非空、非盘符根、非构建/缓存/源码树目录、无四件套同名冲突），PLY 线路固定 `allowOverwrite = false`。第一组三点选满后自动平面拟合并自动进入第二组，第二组同面校验通过后自动建立 WObj1、边缘分割、ROI、Mask 并一次性提交四件套。
- 模块 60 新增 `TempWorkpieceOptions::useInMemoryScan` / `scanInfo` 与 `TempWorkpiecePreparation` 的转换诊断字段；JSON 入口行为、公共头既有字段和 `temp_scanning_info.json` 读取逻辑保持兼容。
- 验证：仅完成代码级与测试代码级修改和静态检查；本轮未执行 Debug/Release 构建、CTest 和 GUI 人工验收，不得把上述功能记为已通过验收。

<a id="evidence-0261"></a>

- 根因：参数对话框取消或 JSON 解析失败会遗留 `WaitingForParameters`；自动链同步前置失败只返回；`m_tempWorkpieceSessionActive` 与 PLY 来源标记分离维护，可能让旧会话和覆盖策略串线；JSON 位姿摘要显示顺序错误。
- 修复：应用层改用 `None/Json/Ply` 单一来源状态，参数确认后才启动新任务；旧会话替换需确认，统一失效不删除磁盘文件；主动放弃取点保留会话，自动链失败统一进入 `Failed` 并允许重试/切换；JSON 显示改为 `[X,Y,Z,Rx,Ry,Rz]`。
- 契约：JSON schema、四件套文件名/格式、坐标转换数学和阶段错误码保持不变，取消由 `cancelled=true` 区分。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke、真实线扫 PLY/XML 和生产夹具未验证。

<a id="evidence-0262"></a>

- PLY/JSON 临时工件状态机和底层取消/成功语义完成代码验收：Debug/Release 构建成功，双配置全量 CTest 均为 `15/15`。
- JSON schema、四件套文件名/格式、坐标旋转约定和阶段错误码保持不变。
- GUI smoke、真实线扫 PLY/XML、机器人运动方向和生产夹具验收仍待执行。

<a id="evidence-0263"></a>

**2026-09-17（PointCloudStitch 高倾角配准修复第一轮：命中口径 + 鲁棒粗对齐 + 提速）**

<a id="evidence-0264"></a>

- 登记主模块 `40_pointcloud_matching`，配套 `30_pointcloud_stitch` 选项透传与失败信息细化；未改动坐标链、手眼矩阵、PLY 读写、渲染与平面输出模块。
- 证据基线与根因：现网 `stitching_report.json` 显示失败数据集的结构点 15 mm 命中率只有 0.182 / 0.229 / 0.376 / 0.487，而旧验收把 3 mm 覆盖率除以全部裁剪区结构点（门槛 0.65），在这些数据上任何刚体变换都无法满足；同期命中点内覆盖率为 0.633 / 0.639 / 0.519 / 0.716。另外 `5_40` 的切向粗对齐已找到约 8 mm 的正确候选，却因 5 mm 覆盖率增益只有 1.55% 低于 5% 门槛被丢弃。
- 契约变更（已确认）：默认验收口径改为“命中点分母 + 证据门槛（命中点 ≥ max(minimumStructuralPoints, 10% 裁剪区结构点数)）”，命中率不再作为硬门槛，低于 0.25 只输出警告；失败原因码区分为 `structural_points_insufficient` 与 `structural_coverage_insufficient`；`structuralCoverageOfMatched=false` 可逐位复现旧行为。对外错误码仍为 `PCV_STITCH_001`，fail-closed 语义不放松。
- 配准质量：切向粗对齐目标函数默认改为 trimmed 最近邻目标（最小 K 个距离均值，K=25%），旧覆盖率目标保留可切换；新增可选 trim 加权（默认关，实测在平面主导配对上会拉走已恢复位姿）、预对齐 p90 > 2 mm 时首层对应半径放宽到 4 mm、法向一致性门控与按主结构方向的裁剪框路由（两者均实现但**默认关闭**，见下条回放证据）。
- 提速：ICP 内对应搜索分块并行（块序合并，与串行逐位一致）、步长试探改为不落地的变换计数、relaxed retry 增加证据预判并记录跳过原因。速度指标按确认结论不设硬指标，以“判定一致 + 可测变快”为准。
- 文件：`modules/40_pointcloud_matching/include/pcv/registration/multiframe_registration.h`、`modules/40_pointcloud_matching/src/multiframe_registration.cpp`、`modules/30_pointcloud_stitch/include/pcv/interface/stitching_interface.h`、`modules/30_pointcloud_stitch/src/stitching_interface.cpp`、`tests/unit/registration/multiframe_registration_tests.cpp`、`docs/modules/30_pointcloud_stitch.md`、`docs/modules/40_pointcloud_matching.md`。
- 验证：Debug 构建成功、`ctest -C Debug` 14/14 通过（含 4 组新增用例：命中口径与证据门槛、4 mm 切向偏移下 trimmed/legacy 判定差异、结构裁剪框路由、稠密夹具上 ICP 内并行逐位等价）。
- 真实数据回放结果（Release `registration_diagnostic`，输入为原 PLY 硬链接 + 由报告反解位姿）：`1/test_1` 仍接受，修正与基线差 0.008 mm / 0.0009 度（在 0.01 mm/0.005 度预算内）；`5_40/test` **由拒绝转为接受**（命中率 0.2246、命中点内 3 mm 0.6566、修正 (-3.7708, -2.7613, 1.6065)/0.1154 度，严格档直接通过）；`6_50/test` 仍拒绝（命中率 0.3703、命中点内 3 mm 0.4989），按停止判据不放宽门槛。法向门控与结构路由在 `1` 组上把修正漂移到约 1.8 mm，故两者默认关闭、仅作为显式开关保留。
- 未验证项（不得视为已通过）：`4_30` 与 `3` 两组未回放；提速与基线报告不是同一构建配置，同配置 A/B 未执行；法向门控/结构路由在真实数据上只有“会改变结果”的证据、没有“更好”的证据；Release 全量 CTest 与 GUI 人工冒烟未执行。

<a id="evidence-0265"></a>

**2026-09-15（PointCloudStitch 间歇 0xc0000005：并行路径 Qt 隐式共享容器并发 detach）**

<a id="evidence-0266"></a>

- 登记主模块 `30_pointcloud_stitch`，同步审计 `40_pointcloud_matching`；未提交、未推送、未删除任何旧构建目录，陡峭平面相关未提交修改（`apps/pointcloudstitch/*`、`docs/modules/40_pointcloud_matching.md`、`modules/40_pointcloud_matching/{include,src}`、`tests/unit/registration/multiframe_registration_tests.cpp`）全部保留。
- 根因一（已动态复现）：`applyTrajectorySeamFusionKernel` 用 `merge.points = *points` 与调用方共享容器（GUI 入口 `apps/pointcloudstitch/seamfusion.cpp` 即此路径），并行 worker 随后以非 `const` 的 `merge->points[...]`、`merge->cloudIds[...]`、`buckets[i]`、`centers[i]`、`directions[i]` 读取；Qt 非 `const operator[]` 会 `detach()`，多个 worker 并发 detach 同一 refcount > 1 缓冲时并发改写同一指针成员并重复 deref 旧缓冲，造成堆破坏，随后在任意分配点（表面为 `QVector<SeamFusionDiagnostic>` 扩容）表现为 `0xc0000005`。在未修复源码上运行新增用例，100 次中第 34 次以 `-1073741819`（`0xC0000005`）退出。
- 根因二（ABI 风险，已量化并排除）：工作区在 `IcpOptions` **结构体中部**插入 6 个字段、在 `IcpDiagnostics` 中部插入 23 个字段，实测 `sizeof(IcpOptions)` 280 → 304、`offsetof(maximumCorrectionTranslation)` 96 → 120、`sizeof(IcpDiagnostics)` 896 → 1080、`offsetof(planePrealignmentAttempted)` 500 → 688。若沿用 `NMake Makefiles JOM` 这类不跟踪头文件依赖的增量构建，旧 object 与新 object 会按不同布局访问同一结构体（与 2026-09-01 记录同类）。本轮全部使用**全新**源码树外构建目录 + `NMake Makefiles`（CMake 自带依赖扫描），构建脚本的头文件依赖检查通过，不存在新旧对象混用。
- 修复行为：seam fusion 引入只读 `SeamWorkerView`（const 指针集合）与稳定 const 视图，worker 只调用 const API、只返回私有 `SeamBandTask`/`CoreChunk`，共享结果与诊断在显式 join 屏障后按确定顺序串行提交，`merge->*` 仅在所有 future 结束后改写；registration 并行 overlap 改为 `cloudView->at()` 只读访问与显式捕获，ICP 修正矩阵累计保持串行。`result.diagnostics.reserve(...)` 仅作确定性/防御性措施，不作为根因修复。
- 行为与契约不变：输入长度、`cloudId` 范围、取消语义、失败回滚、输出点序与来源映射、串并行结果一致性、`IcpOptions/IcpDiagnostics/SeamFusionDiagnostic` 字段与 JSON schema、共享 `CancellationToken` 生命周期均未改变；未引入对 GUI 对象或临时 lambda 的悬空引用。
- 验证：
  - Debug 清洁构建（新目录 `C:\pcv-build\stitch-crash-debug-clean-caf0b46`，`NMake Makefiles`，MSVC 19.51.36252.0 / 14.51.36231，SDK 10.0.28000.0，全 target 100%，退出码 0）。修复后另用全新目录 `C:\pcv-build\stitch-crash-debug-clean-v2-caf0b46` 全量重建（100%，退出码 0）。
  - `ctest --test-dir <debug-v2> -C Debug -R "seam_fusion_tests|multiframe_registration_tests|stitching_interface_tests" --repeat until-fail:100 --output-on-failure` → 3/3 通过，0 失败，用时 344.32 s，退出码 0（无 `0xc0000005`）。
  - `ctest --test-dir <debug-v2> -C Debug --output-on-failure` → 14/14 通过，0 失败，退出码 0。
  - Release 全量构建（`C:\pcv-build\stitch-crash-release-caf0b46`，100%，退出码 0）后 `ctest -C Release -R "seam_fusion_tests|multiframe_registration_tests|stitching_interface_tests"` → 3/3 通过，退出码 0。
- 未验证项（不得视为已通过）：
  - 真实夹具重放**未验证**。`D:\stitch_test` 下 `1/2/3/4_30/5_40/6_50` 只有 PLY、`hand_eye_convert.xml` 与上一次运行的输出；驱动 `registration_diagnostic stitch` / GUI 所必需的 `--pose-info`（`A<n> Start … ; END …`）位姿 TXT 不存在，且 PLY 文件名（如 `4_X565.ply`）不含工具要求的 `A<n>` 扫描编号，因此本轮**没有**以任何合成姿态替代真实夹具验收。
  - GUI 人工复测（PointCloudStitch 走 30° 数据并观察不崩溃）未执行。
  - PageHeap / Application Verifier / WinDbg 首次堆损坏捕获未执行：本机 `D:\Windows Kits\10\Debuggers\x64` 为空，未安装调试器；改用“未修复版本 100 次复现 + 修复版本 100 次通过 + 事件日志故障偏移”作为证据。
  - 用户旧的 `build\Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 目录已不存在，无法核对其生成器与 `.obj.d` 是否为空，因此“用户 GUI 崩溃是否由 JOM 陈旧对象引起”仍属未证实假设。
- 剩余风险：`isCancelled` 回调在并行 worker 中会被多线程调用，调用方必须保证其线程安全（`CancellationToken` 为原子量，安全）；新增测试中的取消回调已改用 `std::atomic`。

<a id="evidence-0267"></a>

**2026-09-15（PointCloudStitch 陡峭平面配准修正）**

<a id="evidence-0268"></a>

- 登记主模块 `40_pointcloud_matching`，配套报告/状态字段位于 `apps/pointcloudstitch`；未修改坐标链、手眼矩阵、原始 PLY、`workpiece_list.json` 或窗口生命周期。
- 主平面拟合保留浅倾角 Z 高度带兼容路径；当该路径不适用时采用 1 mm 采样、固定候选序列的方向无关 RANSAC + PCA 精化。正式上限为相对 robot-base Z 轴 75 deg，超过上限按 `plane_tilt_exceeds_limit` 拒绝。
- 陡峭面使用目标平面的 `u/v/n` 局部重叠框，统一用于重叠计数、三层 ICP、步进保留、宽候选后的重建及结构验收。非平面结构和局部 `u/v` 边界点提供 Point-to-Point 辅助残差，主平面继续使用 Point-to-Plane。陡峭面必须具备两侧至少 100 个非平面结构特征点、`observableDof >= 3`、三层完成以及既有双向 3 mm 覆盖率阈值；纯平面/低特征情形按 `steep_plane_feature_insufficient` fail-closed。
- `stitching_report.json` 记录平面拟合方式、法向/倾角/RMS/内点、局部重叠、可观测性及陡峭面拒绝原因；坐标链异常继续只报警。
- 验证：Debug 构建 `multiframe_registration_tests`、`stitching_interface_tests`、`seam_fusion_tests`、`pointcloudstitch` 成功；新增 60/75 deg 特征面通过、76 deg 限制拒绝、60 deg 纯平面拒绝的回归。全量 CTest、GUI 人工操作及真实 `D:\stitch_test\6_50`/陡峭夹具重放待完成。

<a id="evidence-0269"></a>

**2026-09-09（亮度图取流验证 GUI）**

<a id="evidence-0270"></a>

- `10_pointcloud_read` 增加 `extractIntensityImage()`，并在扫描组帧中同步保留 `CameraFrame::intensityData`。
- 新增实验性独立 target `intensity_viewer`：固定 `Range_Image=7` 和 `MV3D_LP_RegisterImageDataCallBack`，支持实时 `Mono8` 预览及非覆盖 PNG 保存。
- 自动化验证：Debug 构建及 CTest `19/19` 通过；真实设备亮度帧、多批次同步和 GUI 人工操作仍待现场验收。

<a id="evidence-0271"></a>

**2026-09-07（PointCloudStitch 配准与拼接耗时优化）**

<a id="evidence-0272"></a>

- 登记主模块 `30_pointcloud_stitch`、`40_pointcloud_matching`，应用配套为 `apps/pointcloudstitch`。本次仅优化多帧配准/拼接路径及其诊断输出，不改变手眼坐标转换、`robot_base` 输出坐标系、点源映射和 seam fusion 参数合同。
- `40_pointcloud_matching` 增加配准阶段耗时统计：`IcpDiagnostics` 记录预处理、粗搜索、三层 ICP、结构校验、严格尝试和最终尝试耗时及各层迭代次数；`MultiFrameRegistrationResult` 聚合预处理、粗搜索、各 ICP 层、结构校验、retry 和注册总耗时。严格尝试与 relaxed retry 共用 `PairPreprocessCache`（crop、平面、距离诊断、分层采样基点）；step factor 试算复用 scratch buffer，变换与重叠计数合并并支持达到阈值提前退出；每层增加无改善提前收敛，最终安全/结构验收保持不变。
- 采用平衡模式默认值：`tangentCoarseAlignmentEnabled=false`、`wideStructureDiagnosticEnabled=false`；新增 `tangentCoarseAlignmentAuto`，仅在机器人初值未满足现有覆盖/重叠条件时自动触发切向粗对齐。relaxed retry 仅针对 `safety_boundary`、`prealignment_boundary`、`structural_validation` 触发，取消、无重叠和输入错误不重试；诊断模式可显式重新开启完整粗搜索。
- `30_pointcloud_stitch` 的 seam fusion 改为按 `cloudId` 一次分桶，每条 seam 只处理相邻两帧点；融合带匹配标记使用局部数组，keep/核心点判定保持单次全量扫描。原有 `halfWidth`、`mutualDistance`、`decisionCellSize`、真实投影重叠、未匹配点保留、fail-closed、取消语义和输出点序保持不变。
- `pointcloudstitch` 增加“诊断输出模式”：标准模式不写逐帧来源及 pre/post ICP 诊断 PLY，诊断模式才写出；来源点按 cloud bucket 单次遍历生成，避免按帧反复扫描合并点云。`stitching_report.json` 新增 `timings` 汇总以及逐帧/逐层计时字段。
- 验证：`multiframe_registration_tests`、`seam_fusion_tests` 连续 8 次通过；真实 `robot_base` 数据（3 帧、约 90 万至 246 万点、`stride=8`）平衡模式与诊断模式修正矩阵逐位一致，wide 粗搜索由诊断模式约 414 ms/对降至平衡模式约 8 ms/对；阶段聚合耗时、逐对层级迭代和报告字段已核对。`stitching_interface_tests` 完成编译链接，当前沙箱因 `QTemporaryDir` 目录创建限制无法运行；GUI 仅完成 `uic+cl` 语法编译，未完成人工冒烟。
- 风险/待验收：用户环境需复验完整 Debug/Release CTest、GUI 标准/诊断模式切换、真实多帧 seam 输出点序/计数等价性，以及粗搜索和 retry 在生产夹具上的触发比例；历史 `seam_fusion_tests` 间歇性访问违规风险仍需持续关注。

<a id="evidence-0273"></a>

**2026-09-07（ROI 匹配接口收敛）**

<a id="evidence-0274"></a>

- `modules/70_roi_template/src/temp_workpiece_interface.cpp` 不再直接按索引复制 ROI 点，改为调用 `pcv::matching::matchRoiToRobotBase`；准备阶段已在 `robot_base` 的临时工件路径使用 identity transform 复用统一 fail-closed 校验，输出点数取匹配结果的 `matchedPointCount`。
- `pcv_m40_pointcloud_matching` 的 `RoiMatchResult` 继续提供完整性统计、覆盖率、缺失/拒绝计数、取消状态和错误原因；`roi_matching_tests` 纳入 CTest，覆盖有效输入及失败关闭路径。
- 本次仅同步源码事实和文档，未在当前会话重新执行构建或 CTest；GUI、真实相机和生产夹具验收仍未完成。

<a id="evidence-0275"></a>

**2026-09-06（模块重命名、相机并入 10 与 30/40 职责重划）**

<a id="evidence-0276"></a>

- 模块重命名：`05_camerainput` 并入 `10_pointcloud_read`；`10_pointcloudread`、`20_pointcloudrender`、`30_pointcloudstitch`、`40_pointcloudregistration`、`50_coordinateconversion`、`60_planefitting`、`80_planeoutput` 分别改为 `10_pointcloud_read`、`20_pointcloud_render`、`30_pointcloud_stitch`、`40_pointcloud_matching`、`50_coordinate_conversion`、`60_plane_fitting`、`80_plane_output`；旧 target 保留为迁移期兼容入口（仅转发）。
- 相机输入并入 10：相机公共头、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 `Mv3dLp` 实现位于 `modules/10_pointcloud_read`；`PCV_BUILD_EXPERIMENTAL_CAMERA` 仍为可选实验能力，无 SDK 时可构建纯函数转换与 Mock 路径。
- 30/40 职责重划：`30_pointcloud_stitch` 收敛为唯一拼接编排模块（读取、坐标转换/多帧匹配调用、seam fusion、诊断、正式输出与失败回滚）；seam fusion 公共类型与实现迁入其 stitching 目录；`40_pointcloud_matching` 改为平面/ROI 点匹配与 `robot_base` 完整回传模块，新增 `pcv::matching::{RoiMatchInput,RoiMatchResult,matchRoiToRobotBase}`（fail-closed、覆盖率与取消诊断），保留 `registerRobotBaseFrames()` 等兼容包装；`70_roi_template` 的 ROI 匹配改调 40 统一接口；50 仍是坐标转换唯一数学实现。
- 文档同步：docs 索引、架构概览、代码构成、模块文档与本文件同步新目录/target 名、相机并入说明、30/40 新职责、数据流与兼容层说明；历史研究文档仅修正仍被当作当前规范的引用并加“历史名称”说明。
- 验证：Windows Debug 三种配置（相机 OFF、相机 ON 无 SDK、相机 ON 真实 SDK）配置、构建与全量 CTest 通过；Release（相机 OFF）构建与 CTest 待执行。GUI 人工验收、真实生产夹具与旧 target 外部使用情况仍待独立验收。

<a id="evidence-0277"></a>

**2026-09-04（拼接报告元数据真实化与方向自检）**

<a id="evidence-0278"></a>

- 配准/拼接报告元数据真实化：`RobotBaseFrame` 携带 `flangeFromDepth`，GUI 路径补传 `signedTravel`/`dominantTravelAxis`，修复 `stitching_report.json` 中 flange=identity、signed_travel=0、dominant_travel_axis=-1 的失真；`stitching_report.json` 新增 `orientation_check`（净旋转 Z 系数 + 台面上下采样分布），辅助真机核对输出上下方向。
- 已知问题（本次未改拼接算法）：实测本机拼接输出净旋转 Z 系数为 -1（台面≈820.75 时工件主体 z≈716~819 位于台面下方，相对 +Z 向上约定整体上下颠倒），根源指向扫描位姿 A/B/C 与手眼标定 z 方向约定的组合，修正需真机判定（候选见 30/40 模块变更记录）。
- 验证：本会话沙箱禁止外部程序，未编译；待 Debug 构建、全量 CTest 与真实数据验收。

<a id="evidence-0279"></a>

**2026-09-04（渲染包围盒轴线和整数刻度）**

<a id="evidence-0280"></a>

- 深色 `PointCloudCanvas` 将 X/Y/Z 彩色轴固定到包围盒最小角点的三条正向边，三轴以该角点共点；交点不标识坐标数字，包围盒、网格和刻度只属于当前会话的显示层；
- 网格按包围盒归一化比例生成，每个坐标轴固定 10 个区间；轴标签使用加粗大字号，映射到实际 mm 坐标后显示为整数；点云数据、拾取索引、平面流程、PLY、JSON、PNG 和四件套输出契约不变。

<a id="evidence-0281"></a>

**2026-09-04（全量 Markdown 中文同步）**

<a id="evidence-0282"></a>

- 按当前源码、CMake 和测试同步全部项目 Markdown：补充 `pointcloudcli`、实验相机和 `registration_diagnostic` 的功能边界，统一模块 target、构建生成器和验收口径；
- 历史 v0.1/v0.2、交接、契约和研究文档保留原始版本语义，仅修正当前状态提示、失效路径和中文说明；
- 本次仅完成文档检查，未运行 Debug/Release 构建或 CTest，因此不新增本次构建或测试通过结论。

<a id="evidence-0283"></a>

**2026-09-03（CLI 构建路径骨架）**

<a id="evidence-0284"></a>

- 新增 `PCV_BUILD_CLI`（默认 `OFF`）和独立 `pointcloudcli` target；开启后可在关闭 `pointcloudview`、`pointcloudstitch` 的配置中单独构建 CLI，CLI-only 路径仅配置 `Qt6::Core`、`Qt6::Gui`、`Qt6::Concurrent` 及 CLI 所需共享模块，不配置 Widgets/OpenGL，既有 GUI target、UI 和默认构建行为不变；
- `pointcloudcli temp-workpiece run <job.json>` 已登记为首期临时工件业务入口：复用扫描校验、坐标转换、三点平面拟合、第二组三点验证、自动 WObj1、ROI/Mask 和四件套原子输出；实时相机采集、拼接、干涉平面和质量报告不在本期范围；
- `pointcloudcli inspect ply <file.ply>` 和 `pointcloudcli read ply <file.ply>` 已接入 10 模块 PLY 读取接口；前者只检查 Header，后者输出完整读取摘要，均不默认写出文件；
- CLI 的早期骨架阶段仅提供 `--help`、`--version`、空参数及未知参数契约；当前实现已在此基础上接入 PLY Header/完整读取摘要和 `temp-workpiece run`，仍不接入 Qt Widgets、OpenGL 或 GUI 事件循环；
- `pointcloudcli` 不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target；`PCV_BUILD_CLI=ON` 与 `PCV_BUILD_TESTS=ON` 时注册 15 项 CLI CTest，覆盖参数契约、PLY Header/完整读取、三种 PLY 格式、Bounds、临时工件矩形/索引 ROI、覆盖保护和路径穿越拒绝；
- 验收范围：独立 CLI target 构建与参数测试，及默认 GUI 配置回归。GUI 人工操作和真实生产夹具不属于本项自动化验收。

<a id="evidence-0285"></a>

**2026-09-13（CLI 一键构建与便携部署）**

<a id="evidence-0286"></a>

- `scripts/build_windows.ps1` 新增 `-CliOnly`、`-Deploy`、`-DeployDir`；CLI-only 自动探测 CMake、MSVC 和 Qt，显式关闭两个 GUI 应用，`-Deploy` 仅允许 Release 并执行 Qt/VC 运行库部署、依赖检查和清空 PATH 的 15 项契约测试。部署目标必须在源码树外且不能是盘符根目录；已有目标目录会在安全检查后清空重建。
- `pointcloudcli temp-workpiece run` 新增 `--data-dir`/`PCV_DATA_DIR`/exe 同级 `data`/`%LOCALAPPDATA%\\PointCloudSuite\\data` 可写目录回退，缓存不再依赖固定盘符；GUI 数据目录和默认 `PCV_BUILD_CLI=OFF` 保持不变。
- 新增 `scripts/build_pointcloudcli_release.bat` 作为 Release 便携构建入口；便携目录不包含业务输入文件，不提供安装程序或签名。

<a id="evidence-0287"></a>

**2026-09（相机取流扩展，风险收敛）**

<a id="evidence-0288"></a>

- 登记实验模块 `05_camerainput`（`pcv_m05_camerainput`，历史名称：2026-09-06 起并入 `10_pointcloud_read`，见本模块变更记录）：`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，默认构建包含相机扩展，仍可显式关闭；SDK 存在时链接真实 `Mv3dLp`，否则保留 Mock 演示路径；
- `20_pointcloudrender` 新增 `PointCloudCanvas::setCloudStreaming`（实时流式刷新，保留覆盖层）；
- `apps/pointcloudview` 新增 `CameraDialog`（相机实时取流页面）与 `live_pipeline`（JSON pipeline 执行器，camera.nextFrame/filter.voxel/canvas.publish）；
- 实时路径仍为实验链路；`live_pipeline` 尚未接入生产相机链；
- 验证：实验开关 OFF 的 Debug CTest `13/13`，开关 ON 且 SDK 存在的 Debug CTest `17/17`；完整 SHA-256 缓存回归通过；
- 未验证项：真实相机 GUI、`SDKCoordinateType` 坐标映射、实机软触发/重连、关闭时序和 130 万点性能，均为发布阻断项。

<a id="evidence-0289"></a>

**2026-09（采集工作台 Designer UI）**

<a id="evidence-0290"></a>

- `apps/pointcloudview` 使用 Qt Designer 增加顶部工具栏、中央画布工作区及采集、清理、平面、输出四步导航；既有算法和输出契约不变；
- 实验相机对话框显示当前 `DeviceInfo` 字段和 Mock 源，未实现的相机参数以禁用控件显式提示，不纳入 v0.3 稳定能力；
- 图标资源仅服务 UI，不引入共享模块依赖。相机 GUI 使用 SDK 回调取流，默认请求 `Range_Image=7`；实时帧仅预览，冻结有效帧后进入既有点云处理和输出流程。

<a id="evidence-0291"></a>

**2026-09（全界面浅色主题补强）**

<a id="evidence-0292"></a>

- 主窗口与相机对话框补充嵌套面板、菜单、弹出列表、滚动条、SpinBox 和复选框的浅色样式，避免系统调色板导致深色残留；
- OpenGL 画布继续使用浅灰白清屏色和深色叠加文字，不改变渲染公共接口。

<a id="evidence-0293"></a>

**2026-09（浅色 UI + 深色 OpenGL 画布）**

<a id="evidence-0294"></a>

- 主窗口、工具栏、面板和对话框保持浅色视觉基线；中央 OpenGL 画布采用深色背景，以提高彩色点云、边缘和坐标轴的识别度；
- 画布叠加文字同步使用浅色高对比度，不改变渲染公共接口或数据契约。

<a id="evidence-0295"></a>

**2026-09-04（独立渲染设置窗口）**

<a id="evidence-0296"></a>

- `apps/pointcloudview` 新增 Qt Designer `RenderDialog`，通过工具栏打开“渲染 / 内容”双页设置窗口；原有点尺寸、颜色模式、透明度和映射范围与窗口双向同步；
- 支持坐标轴、XY 地板网格、坐标标签、法向量、包围盒和统计信息显示开关；设置仅作用于当前会话的 `PointCloudCanvas`，不影响 PLY、JSON、PNG 或四件套输出。

<a id="evidence-0297"></a>

**2026-09-01（当前差异风险收敛）**

<a id="evidence-0298"></a>

- P0：帧槽通知改为加锁 setter/clear；FrameSource 默认拒绝不支持能力；转换单飞、仅保留最新待处理帧，按 generation 丢弃旧结果；关闭先清通知并等待转换和 SDK callback；
- P0：真实异常回调映射 `FrameKind::Exception`、异常类型和描述；callback 注册返回码检查；SDK lease 保证一次 Initialize 对应一次 Finalize；
- P0：PLY 缓存恢复完整 SHA-256，缓存版本升至 3；同尺寸同时间戳的未采样区域修改必须 cache miss；
- P0：移除 `CMAKE_DEPENDS_USE_COMPILER OFF`，统一 `VSLANG=1033` 并对 MSVC ABI/64 位探测失败硬失败；
- P1：生产链统一 `PipelineRunner`、canonical warning 透传、PLY 完整回读、换帧清除点索引覆盖层；P2：DP2240 实测坐标/软触发/重连/性能后再定帧率目标。

<a id="evidence-0299"></a>

**2026-09-01（P1 生产链与契约补强）**

<a id="evidence-0300"></a>

- `CameraDialog` 的实时帧统一经 `PipelineRunner` 执行，内置链为 `camera.nextFrame → filter.voxel（可选） → canvas.publish`；生产链携带 `frameNumber`、时间戳和 stream generation，旧 generation 不得发布。
- 外部 pipeline 使用 `pointcloudview-live-pipeline-v1`，支持命令行/`PCV_LIVE_PIPELINE_FILE`/应用目录配置文件；解析、顺序、重复 ID、节点类型和参数任一失败时回退内置链并记录 warning。
- `CachedCloudResult`/`LoadResult` 增加 warnings；canonical PLY 写入失败保持主读取成功，但 warning 透传到日志和状态栏。
- binary PLY writer 增加 `Point3D` 标准布局、可平凡复制、字段偏移和大小约束；非 little-endian 使用逐字段编码。
- 新增 pipeline 严格校验、预转换帧透传、warning 及 binary writer 回读回归；实验相机 Debug CTest `17/17` 通过。

<a id="evidence-0301"></a>

**9. 风险与验收门槛**

<a id="evidence-0302"></a>

**2026-09-02（registration_diagnostic 通用 CLI）**

<a id="evidence-0303"></a>

- `registration_diagnostic` 保留 `pointcloudview` GUI，并支持通用 `--input`/`--input-dir` 多帧输入、`--pose-info` 位姿匹配及 `inspect` PLY header `format` 检查；底层仍统一调用 `pcv::interface::stitchRawLineProfiles`。
- 验证：Windows Debug 工具构建通过；全量 CTest `13/13` 通过。真实 PLY/XML 拼接夹具和 GUI 点击验收未执行。

<a id="evidence-0304"></a>

**2026-09-02（Unicode 路径与 Binary PLY 门禁）**

<a id="evidence-0305"></a>

- 移除构建脚本和 CMake 的 ASCII-only 路径拒绝；保留 MSVC `VSLANG=1033` 与 header dependency 检查。
- 外部输入兼容 ASCII、binary little-endian 和 binary big-endian；所有成功输入必须生成并校验 binary little-endian canonical PLY，失败时主流程停止。正式、预览、诊断、平面和临时工件 PLY 均通过共享 Binary writer 输出。

<a id="evidence-0306"></a>

- 核心门槛：实验开关 OFF/ON 均可配置，Debug/Release 全量 CTest 通过；Windows 使用 `NMake Makefiles` 并验证公共头依赖，JOM/Ninja 不作为当前支持生成器；
- 相机门槛：Mock 连接/首帧/停止/断开/重开、`POINTCLOUDVIEW_SELFTEST_CLOSE=1` smoke 及 DP2240 实机 GUI/坐标/重连证据；当前未完成；
- 不将 `live_pipeline` 单测或 DLL 部署视为生产链路验收。

<a id="evidence-0307"></a>

**2026-09-01（扫描 JSON 主线路崩溃修复：构建依赖跟踪）**

<a id="evidence-0308"></a>

- 现象：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`，主线路无法跑通；
- 根因：Qt Creator 的 `NMake Makefiles JOM` 构建树未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节），`modules/10_pointcloudread`（历史目录名，2026-09-06 起为 `modules/10_pointcloud_read`）的 `ply_reader.h` 扩展了 `PlyReadOptions`/`PlyReadResult` 后，`cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 未重新编译，与新编译的 `ply_reader.cpp.obj` 按不同结构体布局链接，`readPly()` 返回值写穿了 `readPlyCached` 的栈槽；
- 影响面：扫描 JSON 线路（`prepareTempWorkpiece` → `readPlyCached`）、普通"打开点云"与 `pointcloudstitch` 的读取路径；单元测试因可执行文件在头文件变更之后重建而全部通过，无法暴露该问题；
- 历史处置已被 2026-09-01 的 `VSLANG=1033` 策略替代；不再使用 `CMAKE_DEPENDS_USE_COMPILER OFF`，以恢复 ABI 探测并保持 `/showIncludes` 依赖解析；
- 验证：新建 `NMake Makefiles` 构建目录 Debug 构建通过，`.obj.d` 记录 `ply_reader.h`，全量 CTest 13/13 通过；相机开关 ON 的独立构建和 CTest 17/17 通过。`NMake Makefiles JOM` 实测生成空 `.obj.d`，脚本现已拒绝该生成器；Ninja 在中文 MSVC 下依赖同样失效。
- 未验证项：GUI 人工复测（打开扫描 JSON 与打开点云）需在重建后的可执行文件上执行；用户现有的 `build/Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 目录需重新配置并重新构建后才恢复正常。

<a id="evidence-0309"></a>

**2026-09（DP2240 实机 scan-height 无效帧修复）**

<a id="evidence-0310"></a>

- 现象：Range_Image 深度图回调（完整 3000 行）被 ScanFrameAssembler 以 Height=1060 误判丢弃，UI 显示帧：无效；
- 根因：Height 参数（1060）是传输窗口高度，非完整扫描行数（3000 来自 SDK 内部 max_scan_rows）；深度图模式回调本就是整幅扫描；
- 修复：装配器无活动分片时整幅 chunk 直接发布并自适应行数（动态提升 expectedRows）；活动分片期间的高度异常仍拒绝；Height 参数改为仅诊断，装配器以 expectedRows=0 首帧自适应；
- 验证：frame_source_tests 更新整幅放行/活动分片拒绝/自适应用例，单测 17/17 通过；实机确认 startup.log 出现 complete scan rows=3000/3000 fragments=1 discarded=0 size=2100x3000，无 valid=false 空帧；
- 已知限制：实时预览 pipeline 默认 maxPoints=300000，完整 630 万点被截断到 30 万点显示，画布完整点数渲染待评估。

<a id="evidence-0311"></a>

**2026-09（对齐 3DMVS 取流方式）**

<a id="evidence-0312"></a>

- 依据 3DMVS V3.1.4 实测日志与用户手册：真实相机 DP2240 取流 ImageMode=7（深度图）+ SDK 深度图转点云；虚拟相机走同一流程；缓存节点/重发包为 SDK 内部默认；
- 实现：默认 ImageMode=7；事件驱动取帧（onFramePublished）；转换移出 GUI 线程（QtConcurrent）；异常事件（FrameKind::Exception）+ 自动重连；软触发（SoftTrigger）；虚拟相机识别；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 通过；应用构建通过；
- 未验证项：真实相机软触发/重连/深度图实机复测待执行。

<a id="evidence-0313"></a>

**2026-09（DP2240 实机取流修复）**

<a id="evidence-0314"></a>

- 现象：DP2240（MV-DP2240-01P）接入取流返回"无效帧"；
- 根因：SDK 日志确认设备为 HFR 线激光轮廓传感器，ImageMode=4（点云）下实际回传深度图帧（ImageType_Depth C16）；原转换器只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 分派；FrameSource::convertFrame 真机 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应（不强制）；对话框取流模式选项 + 帧类型诊断日志；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 通过；应用构建通过；实机 DP2240 复测待执行。

<a id="evidence-0315"></a>

**2026-09-02（DP2240 无效帧：ABC32f 默认链路）**

<a id="evidence-0316"></a>

- 诊断：`startup.log` 的 Range C16 回调为有效帧（`ImageType_Depth=0x011000b8`、`2100x3000`、`12600000` 字节），但深拷贝后调用 `MV3D_LP_MapDepthToPointCloud` 返回 `0x8006000c (MV3D_LP_E_ALGORITHM)`；SDK 日志 `TO_PROFILE_S32 config[0] [0,0,0,0,0,0]` 后内部错误，不能以手写 scale/offset 推导替代；
- 实现：`Mv3dLpCamera` 和 `CameraDialog` 默认请求 `ImageMode_PointCloud_Image=4`；SetParam、GetParam 和回读模式必须全部成功，否则按回调关闭、等待、StopMeasure、CloseDevice、Finalize 路径清理。模式 4 仅接收 `ImageType_PointCloud=0x026000c0` 的 ABC32f；C16/Profile 和非法 payload/bValid=false 直接拒绝；
- 保留：`Range_Image=7` 是显式诊断选项，仍仅可调用 SDK 转换且失败 fail-closed；实时预览、latest-frame-wins、后台转换、冻结后处理和正式输出契约不变；
- 验证边界：自动化覆盖模式/类型策略与 ABC32f 长度/尺寸门禁。`DB1723975 / MV-DP2240-01P` 仍须现场确认模式 `4`、类型 `0x026000c0`、`width * height * 12`、持续预览、冻结、软触发、断线重连、关闭时序和目标帧率；若模式 4 仍输出 C16，保持失败并单独评估原始 SDK 回调内转换方案。

<a id="evidence-0317"></a>

**2026-09-02（DP2240 完整扫描回调链）**

<a id="evidence-0318"></a>

- 根因：实机 `ImageMode=4` 的 `Profile -> PointCloud` 转换成功，但每个回调仅为 `3200x24~30` 行，且 `nFrameNum` 与分片高度连续组成一次约 3000 行扫描；最新帧槽逐批覆盖造成画布只显示线轮廓；
- 实现：默认恢复 3DMVS `Range_Image=7`，严格 Set/Get 回读后读取设备 `Height`。SDK 原始回调内完成 C16/ABC16 到 ABC32f 的权威转换；内部组帧器以首批 `nFrameNum` 作为运行序号锚点，仅在后续编号连续、宽度一致且累计高度等于 `Height` 时发布完整扫描；不假定首批编号为 0；
- 边界：组帧失败、丢包、停止、断线与重连均清空未完成扫描。`LatestFrameSlot`、实时 canvas、冻结及既有输出只接收完整 ABC32f；冻结按钮拒绝残缺扫描；
- 验证：自动化覆盖完整帧、连续/末批、跳行、重复、宽度变化、重置和内存上限。DP2240 仍须现场确认 `ImageMode=7` C16 回调、`MV3D_LP_MapDepthToPointCloud=MV3D_LP_OK`、约 `2100x3000` 完整扫描、冻结、停止、重连及坐标方向。

<a id="evidence-0319"></a>

**2026-09-03（DP2240 TriggerMode 兼容）**

<a id="evidence-0320"></a>

- 现象：连续取流启动在 `MV3D_LP_GetParam("TriggerMode")` 返回 `0x80060004 (MV3D_LP_E_PARAMETER)`，回调尚未注册，界面无帧信息。
- 根因：当前实现把可选触发节点当作所有设备的启动前置条件；3DMVS `SimpleView_Callback` 官方示例打开设备后直接注册回调并 `StartMeasure`，未要求读取 `TriggerMode`。
- 修复：连续模式对 `MV3D_LP_E_PARAMETER`/`MV3D_LP_E_SUPPORT` 按 SDK 默认自由采集兼容并继续启动；节点存在时尽力设置并回读 `TriggerMode=0`，失败仅告警。软触发仍须完整探测 `TriggerMode=1`、设备支持的 `TriggerSource` 并 Set/Get 回读，否则 fail-closed；不猜测 `TriggerSelector` 枚举值。
- GUI：软触发按钮仅在本次设备探测成功后启用；连续模式超过 5 秒未收到完整扫描时提示可能仍处于硬件触发配置。
- 验证边界：自动化覆盖触发策略分类；真实 DP2240 仍须确认连续回调、软触发能力和设备端触发配置。

<a id="evidence-0321"></a>

**2026-09-03（DP2240 完整扫描高度判断）**

<a id="evidence-0322"></a>

- 修正 `Height` 目标选择：优先使用 SDK `nMax`（完整扫描容量），仅在不可用时回退 `nCurValue`，不硬编码扫描高度。
- 组帧错误记录实际 chunk/累计行数与目标行数；GUI 区分无回调与已有回调但尚未完成完整扫描。
- 单测覆盖 `nCurValue=24,nMax=3000` 选择、3000 行完整帧直发及高度不匹配诊断。
- 实机仍需确认 DP2240 的 `Height` 参数语义、完整扫描输出、坐标方向、软触发和断线重连。

<a id="evidence-0323"></a>

**2026-09-02（DP2240 Profile 原始回调转换）**

<a id="evidence-0324"></a>

- 新证据：`DB1723975 / MV-DP2240-01P` 已确认请求/回读 `ImageMode=4`，但稳定回传有效 `ImageType_Profile=0x023000b9`，例如 `3200x26x6=499200` 字节；SDK 定义明确支持 `ImageType_Profile -> ImageType_PointCloud`，且帧 scale/offset 为零，禁止手写坐标换算；
- 实现：在 `Mv3dLpCamera` SDK 回调内对原始 Profile 调用 `MV3D_LP_ImageConvert`，对 Range 深度调用 `MV3D_LP_MapDepthToPointCloud`，成功后立即复制 ABC32f 到 `LatestFrameSlot`。后台、实时预览、冻结和正式输出只处理拥有式 ABC32f；
- 门禁与风险：状态必须为 `MV3D_LP_OK`，输出类型 `0x026000c0` 且长度 `width * height * 12`；失败 fail-closed。实机仍需确认回调内 SDK 转换无重入/关闭死锁、坐标方向和性能；若直接 Profile 转换失败，后续仅评估 SDK 两步 `Profile -> Profile_ABC32 -> PointCloud`，不自动启用近似算法。

<a id="evidence-0325"></a>

**2026-08-31**

<a id="evidence-0326"></a>

- 完成 v0.3 模块目录、规范 target、UI 独立文件和模块文档治理。
- 完成共享 seam fusion、平面拟合和渲染组件实现迁移。
- 完成 `registration_diagnostic` 到模块接口的迁移。
- Debug/Release 构建和全量 CTest 已执行；GUI 人工验收、生产夹具验收和兼容层删除仍待完成。

已合并的重复证据：[evidence-0111](history_pre_v0.5.md#evidence-0111)、[evidence-0099](history_pre_v0.5.md#evidence-0099)、[evidence-0091](history_pre_v0.5.md#evidence-0091)、[evidence-0093](history_pre_v0.5.md#evidence-0093)、[evidence-0113](history_pre_v0.5.md#evidence-0113)、[evidence-0114](history_pre_v0.5.md#evidence-0114)。

<a id="source-05"></a>

## docs/requirements/pointcloudview_v0.2.md

原始来源：`docs/requirements/pointcloudview_v0.2.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0337"></a>

**1. 版本定位**

<a id="evidence-0338"></a>

v0.2 以 v0.1 的可运行功能和输出契约为基线，包含维护清理、文档同步以及第一组 n 点平面拟合增强。当前实现仍以源码、CMake 和测试为真相源；历史 v0.1 文档保留为基线归档。

<a id="evidence-0339"></a>

**2. v0.1 功能基线（保持不变）**

<a id="evidence-0340"></a>

- 读取 ASCII、Binary Little Endian、Binary Big Endian PLY，并在 Qt/OpenGL 画布中查看。
- 支持真实点选择、第一组 n 点（`n >= 3`）平面提取、RANSAC/PCA 精拟合、连通域过滤和 WObj1 坐标系计算。
- 第一组全部控制点参与初始 PCA/最小二乘平面拟合；少于 3 点、重复、越界、非有限或近似共线的控制点被拒绝。RANSAC 候选和最终连通域必须包含全部控制点。
- 第二组固定 3 点只用于验证第一拟合平面，不替换模型；验证保留 `1.0°` 法向夹角和 `0.4 mm` 最大距离阈值。
- 输出 Grayscale8 的 PNG Mask、robot_base 平面 PLY 以及 `sr2026-temp-workpiece-info-mvp-2` JSON。
- 读取临时扫描 JSON，执行 camera 到 `robot_base` 的转换，并生成临时工件四件套。
- 共享模块保持 `pcv_output`、`pcv_registration`、`pcv_interface` 的依赖边界；工作线程不得访问 QWidget/OpenGL。

<a id="evidence-0341"></a>

**3. v0.2 变更**

<a id="evidence-0342"></a>

本版本删除以下不参与编译或无仓库内引用的内容：

<a id="evidence-0343"></a>

- `src/interface/temp_workpiece_interface.cpp` 中的历史 `#if 0` 实现、`vectorArray` 和 `matrixArray`；
- `tests/unit/interface/temp_workpiece_interface_tests.cpp` 中的历史 `#if 0` 测试块和旧 `main`；
- `apps/pointcloudview/mainwindow.cpp` 中的 `buildUiLegacy()` 历史 UI；
- `include/pcv/interface/temp_workpiece_interface.h` 中的 `kTempPlaneName`。

<a id="evidence-0344"></a>

上述删除不得改变活动 API 的运行逻辑、输出字段、路径规则、错误码或测试语义。源码、头文件、测试和 CMake 构建范围必须不再出现这些符号和 `#if 0`；版本文档可保留删除记录。

<a id="evidence-0345"></a>

第一组平面取点在达到 3 点后继续保持 Picking 状态，用户可继续添加任意数量的控制点，点击“确定平面”后才启动后台拟合；撤销每次移除最后一个控制点。拟合失败保留当前控制点并恢复选择，允许继续取点或撤销后重试。

<a id="evidence-0346"></a>

**4. 接口、契约与兼容性**

<a id="evidence-0347"></a>

- 临时工件输入继续使用 `sr2026-temp-scanning-info-mvp-2`；输出继续使用 `sr2026-temp-workpiece-info-mvp-2`。
- 位姿数组保持 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`；矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- PNG 前景为 `255`、背景为 `0`，像素尺寸默认为 `0.05 mm/px`；四件套采用临时目录成套提交和失败回滚。
- `kTempPlaneName` 是公开头文件符号。本版本假设不存在仓库外编译消费者；若发现外部依赖，应在后续兼容性修订中恢复弃用别名，不得静默破坏外部构建。

<a id="evidence-0348"></a>

**5. 非目标与延期事项**

<a id="evidence-0349"></a>

- 不统一 `apps/pointcloudstitch/handeye_transform.{h,cpp}` 与共享 `pcv_registration` 的 P3 副本语义。
- 不决定 `plane.wobj_num=1` 与 `plane.name="WObj1"` 的 P4 正式契约。
- 不新增机器人控制、手眼标定求解、ICP、数据库或生产追溯功能。

<a id="evidence-0350"></a>

**6. 验收要求**

<a id="evidence-0351"></a>

**静态验收**

<a id="evidence-0352"></a>

- `git diff --check` 通过；
- `pointcloudprocessor_obstacle_tests` 覆盖 3 点兼容入口、n 点拟合、控制点退化/越界/非有限值、控制点容差和 n 点参考组三点验证；
- 在 `apps/`、`src/`、`include/`、`tests/` 和 CMake 文件中扫描不到 `vectorArray`、`matrixArray`、`kTempPlaneName`、`buildUiLegacy` 和 `#if 0`；
- staged diff 仅包含本次 4 个代码文件及 v0.2 文档文件；
- 不产生源码树内构建、缓存、日志或点云文件。

<a id="evidence-0353"></a>

**构建与测试验收**

<a id="evidence-0354"></a>

在 Windows Developer PowerShell 或具备授权的本机环境执行：

<a id="evidence-0355"></a>

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release
```

<a id="evidence-0356"></a>

Debug 全量构建、全部 CTest（重点是 `temp_workpiece_interface_tests`）和 Release 构建均成功后，才可创建本地发布 commit。GUI 手工点击流程仍需人工验收，不能由单元测试替代。

<a id="evidence-0357"></a>

**7. 构建错误处理**

<a id="evidence-0358"></a>

若系统找不到 `cmake.exe`、`ctest.exe` 或 Qt 6，应使用本机 Qt Creator Kit/Developer PowerShell，或通过 `-QtDir`、`CMAKE_PREFIX_PATH` 或 `Qt6_DIR` 指定 Qt。`cl.exe` 不在 PATH 时构建脚本会通过 `vswhere.exe` 初始化 MSVC。若出现编译器、链接器或 CTest 错误，须保留完整错误文本并在修复前暂停提交。

<a id="evidence-0359"></a>

**8. 本地 Git 发布边界**

<a id="evidence-0360"></a>

- 在当前 `master` 分支创建一个本地 commit，建议消息为 `chore: clean dead code and document v0.2 maintenance baseline`。
- 不创建 tag，不执行 `git push`，不操作任何 remote。
- 提交前后均检查 `git status --short`、提交统计和最近一次 commit；不得使用 `git reset --hard` 覆盖用户修改。

<a id="evidence-0361"></a>

**9. v0.2 版本结论**

<a id="evidence-0362"></a>

v0.2 保持既有输出契约，并将第一组平面拟合增强为 n 点控制。其发布条件是静态残留检查、Debug/Release 构建和 CTest 全部通过；在构建工具权限或其他错误未闭环时，版本状态必须标记为未验证。

<a id="evidence-0363"></a>

**10. 变更记录**

<a id="evidence-0364"></a>

- 2026-08-27：删除历史死代码和零引用符号，新增 v0.2 维护需求与本地上传清单，补充构建权限错误处理和兼容性风险说明。
- 2026-08-28：第一组平面提取改为手动结束的 n 点控制，全部控制点参与初始 PCA/最小二乘拟合；第二组继续固定三点验证。

<a id="source-06"></a>

## docs/requirements/pointcloudview_v0.1.md

原始来源：`docs/requirements/pointcloudview_v0.1.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0366"></a>

**1. 项目背景**

<a id="evidence-0367"></a>

PointCloudSuite 面向机器人/工业视觉点云数据的查看、平面提取和二维图像导出。v0.1 的目标是建立一个可运行、可验证、可扩展的点云处理基线：输入 PLY 点云，经过真实点选择和算法处理后，输出平面结果、边缘结果及可追溯的运行文件。

<a id="evidence-0368"></a>

本版本不是机器人控制器或自动加工系统，不包含机器人运动控制、手眼标定求解、轨迹规划或生产数据库。

<a id="evidence-0369"></a>

**2. 产品目标与边界**

<a id="evidence-0370"></a>

**2.1 已实现目标**

<a id="evidence-0371"></a>

- 读取 ASCII、Binary Little Endian、Binary Big Endian PLY。
- 在 Qt/OpenGL 画布中旋转、平移、缩放和重置视角。
- 通过 GPU Picking 选择真实点，使用三点提取目标平面。
- 使用 RANSAC/PCA、距离分类和连通域过滤获得有效平面。
- 建立工件坐标系，将平面点映射为二维 Mask 并导出 PNG。
- 对 ASCII 读取提供后台、内存映射及自适应 `1/2/4` worker 分块解析路径；紧凑
  binary `float x/y/z` 布局使用内部映射快速路径。
- 通过 `pcv_output` 统一写出 PNG、robot_base 平面 PLY 和 JSON 三件套，并使用
  稳定错误码和 JSON 原子提交。
- 通过 `pcv_registration` 读取 Eye-in-Hand XML 并完成线扫点云到 `robot_base` 的刚体变换。
- 通过 `pcv_interface` 读取临时扫描信息，生成临时工件平面、ROI 和成套交换文件。
- 主界面提供“打开扫描 JSON”入口，后台执行临时工件生成并展示统计；该动作不替换当前画布缓存。

<a id="evidence-0372"></a>

**2.2 系统边界**

<a id="evidence-0373"></a>

| 范围内 | 范围外 |
|---|---|
| PLY 读取、缓存、显示、点云清理、平面/边缘处理 | 机器人通讯与运动执行 |
| 工件坐标计算与坐标变换记录 | 手眼标定求解与标定设备控制 |
| PNG、JSON、机器人基坐标平面 PLY 导出 | CUDA、PCL、VTK、Open3D |
| CMake 构建、单元测试和启动日志 | 多工位协同、MES/数据库追溯 |

<a id="evidence-0374"></a>

**3. 技术架构与代码说明**

<a id="evidence-0375"></a>

- `apps/pointcloudview/`：桌面程序入口、`MainWindow`、Qt Designer UI、OpenGL 画布和业务流程。
- `src/io/` + `include/pcv/io/`：PLY 头解析、ASCII/二进制读取、取消、进度和缓存文件。
- `src/filtering/` + `include/pcv/filtering/`：体素处理和统计离群值去除。
- `src/infrastructure/`：运行时目录（缓存、日志、导出）的平台无关管理。
- `src/output/` + `include/pcv/output/`：平面输出上下文校验、PNG/PLY/JSON 契约。
- `src/registration/` + `include/pcv/registration/`：手眼标定和线扫坐标转换。
- `src/interface/` + `include/pcv/interface/`：临时扫描输入和临时工件输出接口。
- `tests/`：PLY 读取、缓存、滤波、输出、临时工件接口和手眼转换测试。

<a id="evidence-0376"></a>

主程序的 `m_points` 是后续处理唯一输入。新点云发布时必须先替换画布缓存，再清除旧平面、边缘和选点状态。

<a id="evidence-0377"></a>

**4. 默认用户流程**

<a id="evidence-0378"></a>

1. 启动程序并初始化 Qt Desktop OpenGL、运行目录和日志。
2. 打开单个 PLY，或递归扫描文件夹建立数据源列表。
3. 后台读取并校验点云，完成后发布到画布和 VBO。
4. 可选执行体素处理、统计离群值去除。
5. 开启取点，依次 GPU Picking 选择 P1/P2/P3，确认候选平面。
6. RANSAC/PCA 精拟合、距离分类和连通域过滤，显示平面与统计信息。
7. 按机器人基坐标轴投影自动生成右手工件坐标系。
8. 执行边缘分割和二维 Mask 生成，保存 PNG；同时可保存同名 JSON 和机器人基坐标平面 PLY。

<a id="evidence-0379"></a>

异步任务只在工作线程读取和计算，不访问 QWidget 或 OpenGL；结果返回时校验画布版本，过期结果直接丢弃。

<a id="evidence-0380"></a>

**5. 核心功能与默认参数**

<a id="evidence-0381"></a>

**5.1 点云读取与缓存**

<a id="evidence-0382"></a>

按属性名称读取 `x/y/z`，可选 `nx/ny/nz`，不依赖列顺序；缺少必需属性、类型不支持、非有限值或数据截断时拒绝发布。ASCII 优先使用 `QFile::map`，映射失败回退分块缓冲；根据数据规模自适应使用 `1/2/4` 个 worker 写入预分配数组的不重叠区间，保持点顺序。缓存和日志位于 `%LOCALAPPDATA%/PointCloudSuite/pointcloudview/`，不写入源码树。

<a id="evidence-0383"></a>

**5.2 点云清理**

<a id="evidence-0384"></a>

默认体素尺寸 `0.25 mm`；统计邻域 `K=45`、标准差倍数 `1.30`。每个体素保留真实输入点，不生成质心；结果为空或参数无效时保留原缓存并提示。

<a id="evidence-0385"></a>

**5.3 平面**

<a id="evidence-0386"></a>

三点计算初始平面，RANSAC 抵抗离群点，PCA/最小二乘完成精拟合，再按距离阈值分类并做 UV 连通域过滤。

<a id="evidence-0387"></a>

**5.4 工件坐标系**

<a id="evidence-0388"></a>

自动建立 WObj1：最终提取平面 XYZ 包围盒中心作为 O/WObj1 原点；根据机器人基坐标 X/Y 轴投影自动选取 X+/Y+ 辅助点，再使用 O/X+/Y+ 三点法计算最终坐标系。Z 轴使用拟合平面法向并调整至机器人基坐标 +Z 同向。输出 `T_base_workpiece`、逆矩阵及控制器 `[X,Y,Z,A,B,C]` 姿态角（A=Rx、B=Ry、C=Rz，矩阵为 `Rz(C)×Ry(B)×Rx(A)`），JSON `plane.equation` 只取该三点法最终结果。

<a id="evidence-0389"></a>

**5.5 边缘与二维输出**

<a id="evidence-0390"></a>

确认平面与 WObj1 后，导出矩形由平面点 WObj1 XY 范围自动计算，四边增加 `50 mm`，向上取整到 `10 mm`，固定 `1 px = 0.05 mm`。JSON 流程的 `plane_mask.png` 使用工件坐标系下的边缘分割 `Format_Grayscale8` Mask：背景为 `0`，边缘前景为 `255`。边缘分割完成后自动进入四件套事务提交。

<a id="evidence-0391"></a>

**5.6 临时工件接口**

<a id="evidence-0392"></a>

接口读取 `temp_scanning_info.json`（schema `sr2026-temp-scanning-info-mvp-2`），要求扫描坐标系为 `camera`、标定方向为 `camera -> robot_base`，并校验点云、XML、位姿、三点索引和必填的 ISO 8601 `created_at`。临时扫描接口只接受 `LineProfileXz`；缺少 `point_cloud_layout` 时默认该布局并提示，显式使用 `FullXyz` 时拒绝输入。转换使用点内 `y` 作为扫描比例，并按 Start/End 位姿插值；无效点、行程范围外点、无效刚体矩阵或缺少时间戳分别按稳定错误码拒绝。用户完成平面二次验证和 WObj1 后，边缘分割与四件套提交自动执行，输出 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、边缘 `plane_mask.png` 和 `temp_workpiece_info.json`。四个文件先写入临时目录，再一次性提交；任一失败均回滚。

<a id="evidence-0393"></a>

**6. 输出与数据契约**

<a id="evidence-0394"></a>

- 二维 PNG：平面或边缘 Mask，左上角为像素原点，前景 `255`、背景 `0`。
- JSON：统一 schema `sr2026-temp-workpiece-info-mvp-2`，只允许顶层字段 `schema_version`、`kind`、`created_at`、`plane`、`image`、`roi`、`outputs`。`plane` 只包含 `name`、`equation`，`image` 只包含名称、像素尺寸、物理尺寸和像素尺寸，`roi` 固定为字符串 `rectangle`，`outputs` 只包含三条规范化绝对路径；`plane.equation` 顺序为控制器格式 `[X, Y, Z, A, B, C]`，A=Rx、B=Ry、C=Rz，矩阵约定为 `Rz(C)×Ry(B)×Rx(A)`。临时工件 `created_at` 原样沿用扫描 JSON，普通平面输出使用生成时间。
- `_plane_robot_base.ply`：最终连通域平面点，binary little-endian，顶点属性仅为 `float x/y/z`，坐标保持机器人基坐标；来源和坐标系元数据通过 PLY `comment` 与同名 JSON 保存。
- 输出由 `pcv_output` 统一写出；PNG、PLY、JSON 先写临时目录后成套提交并支持回滚，任一文件失败均视为整体失败。
- `cache/*.pcvbin`：运行缓存；`logs/startup.log`：启动诊断日志。

<a id="evidence-0395"></a>

**7. 构建、测试与验证**

<a id="evidence-0396"></a>

环境为 Windows 10/11、Qt 6.8.3 MSVC x64、C++17、OpenGL 3.3 Core。构建入口为 `scripts/build_windows.ps1`，测试入口为 `scripts/run_tests.ps1`；根目录还提供 `windows-msvc-debug` CMake preset。当前工作区包含 `pcv_output`、`pcv_registration`、`pcv_interface` 及对应测试。

<a id="evidence-0397"></a>

**8. 非功能需求与已知限制**

<a id="evidence-0398"></a>

- 设备/文件错误不能导致界面崩溃；耗时操作不得阻塞主线程。
- 共享模块不得依赖 Qt Widgets，运行文件不得写入源码树。
- 千万级点云受 CPU 内存和显存限制；ASCII 首次读取仍可能较慢。
- Designer 存在历史重复 `objectName` 警告；平坦 2.5D 表面可能缺少 XY 方向约束。

<a id="evidence-0399"></a>

**9. 待确认事项**

<a id="evidence-0400"></a>

1. 手眼标定和机器人基坐标输入的正式接口。
2. 平面/边缘输出与下游 OpenCV 或机器人程序的最终验收格式。
3. 超大图像分块写出、缓存清理周期和运行数据保留策略。
4. 生产环境的误差阈值、点云规模和显卡配置。

<a id="evidence-0401"></a>

**10. v0.1 版本结论**

<a id="evidence-0402"></a>

v0.1 已形成从 PLY 输入、OpenGL 查看、真实点选、平面/边缘处理到 PNG/JSON/PLY 输出的完整点云处理基线，为后续算法增强、机器人接口和产品化追溯提供稳定文档契约。

<a id="evidence-0403"></a>

**11. 本版本变更记录**

<a id="evidence-0404"></a>

- 建立 PointCloudSuite 主程序与共享模块的产品边界和代码说明。
- 固化 PLY 读取、后台解析、平面/边缘算法及默认参数。
- 固化工件坐标、PNG/JSON/机器人基坐标 PLY 输出契约。
- 增加临时扫描到临时工件的手眼转换和四件输出契约。
- 汇总当前构建、测试、限制和下一版本待确认事项。

<a id="evidence-0405"></a>

**2026-08-24：接口、边缘 Mask 与统一 JSON 契约**

<a id="evidence-0406"></a>

- 删除障碍检测及其非阻断告警，不再把障碍状态写入主程序流程或文档验收项。
- 新增 `pcv_registration` 和 `pcv_interface`，统一临时扫描输入、手眼转换、平面/ROI 提取和输出提交。
- 平面输出 JSON 使用 `sr2026-temp-workpiece-info-mvp-2`，输出路径为规范化绝对路径，姿态顺序固定为控制器格式 `[X,Y,Z,A,B,C]`。
- 用户选择输出目录时直接写入该目录；PNG、PLY、JSON 以及临时工件四件套均采用临时目录提交和失败回滚。

<a id="source-07"></a>

## docs/pointcloudview_phase2_development_handoff_2026-08-25.md

原始来源：`docs/pointcloudview_phase2_development_handoff_2026-08-25.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0408"></a>

**新对话接续提示**

<a id="evidence-0409"></a>

以下接续提示保留用于历史复盘；本阶段已完成时无需再次执行：

<a id="evidence-0410"></a>

```text
继续 PointCloudView 第二阶段开发。请先阅读：
D:\workpiece\pointcloudview\pointcloudview\docs\pointcloudview_phase2_development_handoff_2026-08-25.md

必须先执行 git status --short，保留用户修改，不修改 workpiece_list.json。
按交接记录完成“平面二次验证与 WObj1 坐标系”，然后进行完整 Debug 构建、CTest 和 git diff --check。
不要开始第三阶段 ROI、2D 正式输出或四件套提交。
```

<a id="evidence-0411"></a>

**阶段目标和边界**

<a id="evidence-0412"></a>

第二阶段接收第一阶段已经转换到 `robot_base` 的 `LineProfileXz` 点云，完成：

<a id="evidence-0413"></a>

```text
第一组三点取平面
    -> 候选/全量平面分类
    -> 第二组三点强制验证
    -> 平面包围盒中心作为 WObj1 原点
    -> 按机器人基坐标轴投影自动建立 WObj1
    -> 建立并显示 WObj1
```

<a id="evidence-0414"></a>

本阶段禁止自动执行矩形 ROI、2D 图像正式输出和临时工件四件套提交；不得自动调用 `finalizeTempWorkpiece()`。

<a id="evidence-0415"></a>

第一阶段约束已经调整为只采用 `LineProfileXz`，不采用 `FullXyz`。手眼转换公式仍为：

<a id="evidence-0416"></a>

```text
p_robot_base = T_base_flange(t) * T_flange_depth * p_depth
```

<a id="evidence-0417"></a>

**冻结的业务规则**

<a id="evidence-0418"></a>

- 二次验证适用于临时扫描和普通 PLY 的全部三点取平面流程。
- 第一组三点是平面种子，第二组三点只用于平面一致性验证；WObj1 轴不依赖任何取点方向。
- 第二组三点内部不得重复且不能共线。
- 法向夹角使用 `acos(abs(dot(normal1, normal2)))`，方向相反视为同一法向。
- 法向夹角阈值固定为 `1.0 deg`。
- 第二组三点到第一拟合平面的最大距离阈值固定为 `0.4 mm`。
- 共线、非法输入或复用点：只清除第二组三点，允许重新选择第二组。
- 夹角或距离超限：清除第一平面候选、第二组三点和 WObj1，强制返回第一组三点流程。
- 快速候选和全量分类生成新平面结果时都必须重置二次验证状态。
- WObj1 原点固定为最终平面点 XYZ 包围盒中心 `m_planeCenter`；原点 `[0,0,0]` 合法。
- 自动辅助点仅用于显示和距离诊断，不参与 WObj1 轴计算。
- Z 轴使用最终拟合平面单位法向，继续复用现有右手系和 Y 点定向逻辑。
- WObj1 失败时保留已经通过的平面二次验证，但禁止进入后续流程。

<a id="evidence-0419"></a>

**阶段实现变更（已提交）**

<a id="evidence-0420"></a>

执行 `git status --short` 时共有以下 10 个修改文件：

<a id="evidence-0421"></a>

```text
M apps/pointcloudview/mainwindow.cpp
M apps/pointcloudview/mainwindow.h
M apps/pointcloudview/pointcloudprocessor.cpp
M apps/pointcloudview/pointcloudprocessor.h
M docs/contracts/point_cloud_plane_output_v0.1_proposal.md
M docs/requirements/pointcloudview_v0.1.md
M include/pcv/interface/temp_workpiece_interface.h
M src/interface/temp_workpiece_interface.cpp
M tests/unit/interface/temp_workpiece_interface_tests.cpp
M tests/unit/pointcloudview/pointcloudprocessor_obstacle_tests.cpp
```

<a id="evidence-0422"></a>

这些文件包含用户已有的第一阶段和临时流程改动。禁止 `git reset`、`git restore` 或覆盖式回退。

<a id="evidence-0423"></a>

**已落地并完成自动化验证的第二阶段改动**

<a id="evidence-0424"></a>

**纯几何 API**

<a id="evidence-0425"></a>

`apps/pointcloudview/pointcloudprocessor.h` 已新增：

<a id="evidence-0426"></a>

- `PlaneConsistencyStatus`
- `PlaneConsistencyResult`
- `PlaneBoundsCenterResult`
- `validatePlaneConsistency(...)`
- `calculatePlaneBoundsCenter(...)`

<a id="evidence-0427"></a>

`apps/pointcloudview/pointcloudprocessor.cpp` 已初步实现：

<a id="evidence-0428"></a>

- 两组三点索引、有限值、组内重复和跨组复用校验；
- 第二组三点共线校验；
- `abs(dot)` 法向夹角计算；
- `1.0 deg` 和 `0.4 mm` 阈值判断；
- 最终平面点 XYZ 包围盒中心计算，并允许中心为 `[0,0,0]`。

<a id="evidence-0429"></a>

实现尚未编译。继续开发时优先收敛参考平面的归一化和距离计算：

<a id="evidence-0430"></a>

```cpp
const float planeNorm = referenceNormal.length();
const float normalizedD = referencePlane.d / planeNorm;
referenceNormal /= planeNorm;
distance = std::abs(QVector3D::dotProduct(referenceNormal, point) + normalizedD);
```

<a id="evidence-0431"></a>

**UI 门禁**

<a id="evidence-0432"></a>

`MainWindow::updatePlaneExtractionUi()` 已部分修改：

<a id="evidence-0433"></a>

- `verificationPassed = m_secondPlaneValidated && m_secondPlaneSamePlane`；
- 第二组三点按钮依赖候选存在，不应依赖候选已经确认；
- X/Y 轴点按钮不再作为 WObj1 建立条件；
- “确定候选平面”只要求候选平面和二次验证通过。

<a id="evidence-0434"></a>

此部分尚未编译，也尚未完成周边状态同步。

<a id="evidence-0435"></a>

**尚未完成的实现**

<a id="evidence-0436"></a>

按以下顺序继续，避免状态机死锁。

<a id="evidence-0437"></a>

1. 修正 `validatePlaneConsistency()` 的平面归一化和距离实现，然后先编译处理器测试目标。
2. 重写 `MainWindow::validateSecondPlaneSelection()`，统一调用 `pointcloud::validatePlaneConsistency(...)`。
3. 实现验证结果分流：
   - `Passed`：保存角度/距离并允许按机器人基坐标轴投影自动建立 WObj1；
   - `Collinear`、`InvalidInput`、`ReusedPoint`：只清除第二组三点；
   - `AngleExceeded`、`DistanceExceeded`：清除当前候选、两组三点相关状态、WObj1、边缘/图像状态并回到第一组三点选择。
4. 在 `startPlanePointSelection()`、`abandonPlanePointSelection()`、`runPlaneExtraction()` 或 `planeExtractionFinished()`、`cancelPlaneCandidate()` 和新画布发布路径中统一重置二次验证状态；删除目前明显重复的 reset 语句。
5. 修改 `startSecondPlanePointSelection()`：候选存在即可开始，不能再要求 `m_planeCandidateConfirmed`；开始时关闭轴点模式并重置旧验证状态。
6. 修改 `handleCanvasPointPicked()`：轴点额外排除 `m_secondPlanePointIndices`，画布标记保留第一组、第二组和 X/Y 轴点。
7. 修改 `startWorkpieceAxisSelection()`：要求二次验证通过；使用候选存在和中心有限值判断，不能使用 `m_planeCenter.isNull()`。
8. 修改 `clearWorkpieceAxisSelection()`：只清除轴点和 WObj1，保留第一/第二组三点及验证通过状态。
9. 在 `planeExtractionFinished()` 中调用 `calculatePlaneBoundsCenter()`，失败时拒绝候选；新候选必须重置二次验证和 WObj1。
10. 修改 `confirmPlaneCandidate()`：所有会话统一要求二次验证和独立 X/Y 轴点；删除临时会话用第一组三点的 `P1/P2/P3` 自动建立 WObj1 的旧分支；统一调用：

<a id="evidence-0438"></a>

```cpp
pointcloud::buildWorkpieceCoordinateSystem(
    m_points,
    m_planeCenter,
    fittedNormal,
    m_xAxisPointIndex,
    m_yAxisPointIndex,
    true);
```

<a id="evidence-0439"></a>

11. 删除 `confirmPlaneCandidate()` 成功后自动调用 `extractPlaneImage()` 的临时会话分支。
12. 删除 `planeImageExtractionFinished()` 中临时会话自动调用 `finalizeTempWorkpiece()` 的逻辑；保留函数本体给第三阶段显式调用。

<a id="evidence-0440"></a>

**当前代码中已确认的旧逻辑问题**

<a id="evidence-0441"></a>

- `startSecondPlanePointSelection()` 仍要求 `m_planeCandidateConfirmed`，与新状态机冲突。
- `validateSecondPlaneSelection()` 仍在 UI 层手工计算，角度阈值仍为旧值，且失败行为未按结构化状态区分。
- `startWorkpieceAxisSelection()` 仍使用 `m_planeCenter.isNull()`，会错误拒绝合法原点 `[0,0,0]`。
- 轴点选择只排除了第一组三点，尚未排除第二组三点。
- 轴点清除后画布只恢复第一组三点标记，未保留第二组三点。
- `planeExtractionFinished()` 仍手工计算中心，尚未调用纯几何 API。
- `confirmPlaneCandidate()` 仍允许临时会话复用第一组三点建立 WObj1。
- `planeImageExtractionFinished()` 仍会自动调用 `finalizeTempWorkpiece()`。
- 多个状态重置位置存在重复的 `m_secondPlaneValidated = false` 和 `m_secondPlaneSamePlane = false`。

<a id="evidence-0442"></a>

**后续验证与测试**

<a id="evidence-0443"></a>

在现有 `tests/unit/pointcloudview/pointcloudprocessor_obstacle_tests.cpp` 中补充纯几何覆盖：

<a id="evidence-0444"></a>

- 同向同平面通过；
- 反向点序产生反向法向仍通过；
- 夹角 `0.99 deg` 通过；
- 夹角 `1.01 deg` 返回 `AngleExceeded`；
- 最大距离 `0.4 mm` 通过；
- 最大距离大于 `0.4 mm` 返回 `DistanceExceeded`；
- 第二组三点共线返回 `Collinear`；
- 两组复用点返回 `ReusedPoint`；
- 空输入和索引越界返回 `InvalidInput`；
- 包围盒中心计算正确；
- 包围盒中心为 `[0,0,0]` 时仍 `ok == true`；
- 使用独立 X/Y 点建立 WObj1，原点、Z 轴、矩阵及逆矩阵正确；
- X/Y 点过近、轴向退化或共线时拒绝。

<a id="evidence-0445"></a>

角度用例可令第一平面为 `z = 0`，第二组三点满足 `z = tan(theta) * x`。

<a id="evidence-0446"></a>

**验证要求**

<a id="evidence-0447"></a>

由于修改了 P0 级 `apps/pointcloudview/pointcloudprocessor.{h,cpp}` 和 UI 状态机，完成后必须完整 Debug 重编译和全量 CTest。

<a id="evidence-0448"></a>

当前可用命令：

<a id="evidence-0449"></a>

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B .codex-build-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" -DPCV_BUILD_TESTS=ON -DPCV_BUILD_TOOLS=ON && "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build .codex-build-debug'

& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir .codex-build-debug --output-on-failure

git diff --check
git status --short
```

<a id="evidence-0450"></a>

最低验收目标包括：

<a id="evidence-0451"></a>

```text
handeye_transform_tests
temp_workpiece_interface_tests
pointcloudprocessor_obstacle_tests
ply_reader_tests
cloud_cache_tests
```

<a id="evidence-0452"></a>

2026-08-26 已在 `.codex-build-debug` 重新配置并完整构建；CTest 当前结果为 `8/8 通过`。
该结果覆盖自动化测试，不包含真实 GUI 点选和现场设备数据验收。

<a id="evidence-0453"></a>

**风险和人工验收**

<a id="evidence-0454"></a>

- 当前改动量较大且夹杂第一阶段未提交代码，继续开发必须逐段检查 `git diff`。
- 真实 GUI 点击流程仍需使用可控 PLY 夹具 人工验证：候选生成、二次验证失败回退、二次验证通过、独立轴点选择、WObj1 显示。
- 关闭窗口时后台任务和 QWidget/OpenGL 线程约束仍需烟测。
- 未使用真实 XML/PLY 夹具 验证数值精度时，不得宣称生产级验证。
- 不修改 `workpiece_list.json`，不开始第三阶段 ROI 和正式四件套输出。

<a id="source-08"></a>

## docs/contracts/point_cloud_plane_output_v0.1_proposal.md

原始来源：`docs/contracts/point_cloud_plane_output_v0.1_proposal.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0456"></a>

**目录和成套文件**

<a id="evidence-0457"></a>

默认在 `runtime_data/jobs/<job_id>/point_cloud/plane/` 下生成同一 `base_name` 的三个文件；
当 `JobContext.destinationDirectory` 非空时，直接写入该目录：

<a id="evidence-0458"></a>

* `<base>.png`：`Grayscale8`、8 位单通道、背景 `0`、前景 `255`。
* `<base>.json`：当前 schema 的 `plane/image/roi/outputs` 元数据。
* `<base>_plane_robot_base.ply`：`binary_little_endian`，坐标为 `robot_base`。

<a id="evidence-0459"></a>

JSON 只包含 `schema_version`、`kind`、`created_at`、`plane`、`image`、`roi` 和 `outputs`。`plane` 只包含 `name` 与控制器格式 `[X, Y, Z, A, B, C]` 形式的 `equation`；其中 A=Rx、B=Ry、C=Rz，`R = Rz(C) × Ry(B) × Rx(A)`，equation 的 XYZ 来自 WObj1 原点 O，ABC 只来自 O/X+/Y+ 三点法最终计算结果。`image` 只包含名称、像素尺寸、物理尺寸和像素尺寸；`roi` 固定为字符串 `rectangle`；三个 `outputs` 路径均为规范化绝对路径。诊断、平面模型、轴、矩阵、统计和来源扫描元数据不属于正式 JSON。

<a id="evidence-0460"></a>

图像记录 `width_px`、`height_px`、`width_mm`、`height_mm` 和 `pixel_size_mm`。

<a id="evidence-0461"></a>

写入顺序是临时目录中的 PNG、PLY、JSON，再依次提交正式文件；任一步失败都会回滚。稳定错误码包括 `PCV_CONTRACT_001`、`PCV_IMAGE_001`、`PCV_PLANE_001`、`PCV_FRAME_001`、`PCV_OUTPUT_001` 和 `PCV_OUTPUT_002`。

<a id="evidence-0462"></a>

**临时工件四件套**

<a id="evidence-0463"></a>

`pcv_interface::prepareTempWorkpiece` 读取 `sr2026-temp-scanning-info-mvp-2` 扫描 JSON 和 XML 标定，要求有效的 `created_at`，按 `LineProfileXz` 完成手眼转换并发布临时画布。用户完成平面二次验证和 WObj1 后，JSON 流程自动执行工件坐标系下的边缘分割和矩形 ROI，并由 `finalizeTempWorkpiece` 输出 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、边缘 `plane_mask.png` 和 `temp_workpiece_info.json`；该 JSON 的 `created_at` 原样使用扫描输入时间。四件文件全部先写入临时目录，再一次性提交；输入缺失、路径穿越、标定无效、平面失败或写入不完整时不留下部分正式结果。

<a id="source-09"></a>

## docs/architecture/module_ui_integration_v0.4.md

原始来源：`docs/architecture/module_ui_integration_v0.4.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0466"></a>

**2026-10-09：PointCloudStitch 算法回滚到 6329426**

<a id="evidence-0467"></a>

- 主模块 30 的算法、公共类型、编排与测试恢复到 `6329426`，依赖仍为 `30 -> 10/50`；共享模块 50 当前实现保留，应用继续调用统一手眼入口。
- GUI 恢复完整配准/仅手眼转换两种模式，保留输出覆盖确认；后续 heightmap/fine 源码、字段、回放参数和专属测试注册移除。输出与严格失败策略恢复基线，其他模块不回滚。
- 以下快速模式与 2.5D 记录仅保留历史追溯。本轮实际验证见 `docs/modules/30_pointcloud_stitch.md` 文末。

<a id="evidence-0468"></a>

**2026-10-08：PointCloudStitch 快速重叠精配准接入（已回滚）**

<a id="evidence-0469"></a>

- 30 唯一实现 `RegistrationProfile::PoseOverlapFine`，50/10 的数学与事务契约不变，40 仍仅兼容转发。API 默认 Legacy；PointCloudStitch UI 默认位姿粗配准＋两层重叠区精配准。
- 原始 robot_base 相邻样本求 pair 矩阵，按帧序累计后作用整帧；失败回退当前原机器人位姿，重置链锚点。每对 10 mm/1°，累计不限但报告。worker 只读、主线程顺序提交不变。
- 新模式质量仅警告，不执行旧结构验收/2.5D/seam；`fine_applied` 与 `pose_fallback` 不能被 UI 显示为“严格通过”。正式 PLY/映射/manifest 事务提交，无预览，报告 `strict_validation_status=not_run`；输出身份与几何质量分开。
- GUI 参数与模式/覆盖确认允许变更，窗口生命周期和异步调度不变。文件、位姿、转换、取消或写出错误仍不发布部分结果。验收与未验证项参见模块 30 当次记录。

<a id="evidence-0470"></a>

状态：v0.4 协同约束与迁移记录（2026-10-09 静态核对）。正式需求与实现状态见 [v0.4 基线](../requirements/pointcloudview_v0.4.md)，后续批次见 [开发方案](../development/development_plan_v0.5.md)，业务目标与代码变更追加到 [target.md](../../target.md)。源码、CMake、公共头和测试是实现事实来源；本次未构建、未运行 CTest/GUI，下方带日期的验证只对应当次变更。

<a id="evidence-0471"></a>

**1. 六模块职责与数据流**

<a id="evidence-0472"></a>

| 模块 | 规范目录/target | 职责 | 依赖 |
|---|---|---|---|
|10 pointcloud_io|`modules/10_pointcloud_io` / `pcv_m10_pointcloud_io`|PLY/缓存、相机输入、过滤、PNG/PLY/JSON writer、`ArtifactTransaction`|core/infrastructure/Qt Core+Gui|
|20 pointcloud_render|`modules/20_pointcloud_render` / `pcv_m20_pointcloud_render`|OpenGL、画布、点选、渲染 DTO|core|
|30 pointcloud_stitch|`modules/30_pointcloud_stitch` / `pcv_m30_pointcloud_stitch`|多帧拼接编排、ICP、多帧配准、seam fusion、诊断、manifest|10/50|
|40 pointcloud_matching|`modules/40_pointcloud_matching` / `pcv_m40_pointcloud_matching`|规划中的工件位置确认占位；本版不实现移动判定|30/60（兼容聚合）|
|50 coordinate_conversion|`modules/50_coordinate_conversion` / `pcv_m50_coordinate_conversion`|手眼标定、位姿插值、坐标变换唯一实现（`T_base_depth = T_base_flange * T_flange_depth`）|core|
|60 geometry_workpiece|`modules/60_geometry_workpiece` / `pcv_m60_geometry_workpiece`|平面拟合、平面/边缘几何、ROI、WObj、临时工件、扫描路径规划、严格栅格纹理|10/50|

<a id="evidence-0473"></a>

数据流固定为：输入 PLY/相机帧 → 10 读取/预处理 → 50 转换到 `robot_base` → 30 拼接或 60 几何处理 → 10 事务 staging/commit → 应用/UI 展示。共享模块不得依赖 `apps/`。

<a id="evidence-0474"></a>

**2. 40 模块正式声明**

<a id="evidence-0475"></a>

对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

<a id="evidence-0476"></a>

当前状态：**规划目标，未实现**。本版不实现基准模板持久化、当前/基准比较、移动阈值和 6DoF 判定、机器人执行/坐标修正下发或新的正式输出闭环。`pcv_m40_pointcloud_matching` 与旧 registration/matching header 仅为 `INTERFACE` 转发入口。

<a id="evidence-0477"></a>

**3. 旧名称映射**

<a id="evidence-0478"></a>

| 旧 | 新/状态 |
|---|---|
|`modules/10_pointcloud_read`, `pcv_m10_pointcloud_read`|`modules/10_pointcloud_io`, `pcv_m10_pointcloud_io`；旧 target 转发|
|`modules/40_pointcloud_matching` 的 multiframe/ICP|物理迁入 30；旧 registration header 转发|
|`modules/40_pointcloud_matching` 的 ROI|物理迁入 60；旧 matching header 转发|
|`modules/60_plane_fitting`|`modules/60_geometry_workpiece`；`pcv_m60_plane_fitting`/`pcv_m60_planefitting` 转发|
|`modules/70_roi_template`|实现迁入 60，target/header 兼容转发|
|`modules/80_plane_output`|实现迁入 10，target/header 兼容转发|
|`src/filtering` / `pcv_filtering`|实现迁入 10；头路径 `pcv/processing/`，实际 namespace `pcv::detail::filtering`；旧 target/header 转发|
|`pcv::detail::io`、`pointcloud::`|保留一个版本周期；新 API 优先 `pcv::io/stitching/coordinates/geometry/workpiece`|

<a id="evidence-0479"></a>

**当前 API 与目标 namespace（迁移未完成）**

<a id="evidence-0480"></a>

| current_header | current_namespace | target_namespace | migration_state |
|---|---|---|---|
| `pcv/io/ply_reader.h`、`cloud_cache.h`、`ply_writer.h` | `pcv::detail::io` | `pcv::io` | 生产调用仍使用旧 namespace |
| `pcv/processing/*` | `pcv::detail::filtering` | `pcv::io` 预处理门面 | 路径已迁，namespace 未迁 |
| `pcv/io/plane_output.h` | `pcv::output` | `pcv::io` | 正式 writer 实现在 10，旧 namespace 保留 |
| `pcv/io/artifact_transaction.h` | `pcv::io` | `pcv::io` | 已落地 |
| `pcv/interface/stitching_interface.h` | `pcv::interface` | `pcv::stitching` | 分组请求已落地，内部仍映射旧 Options |
| `pcv/stitching/multiframe_registration.h`、seam/heightmap 头 | `pointcloud::` | `pcv::stitching` 门面 | 底层类型暂保留兼容 |
| `pcv/registration/handeye_transform.h` | `pointcloud::` | `pcv::coordinates` | 数学已统一，namespace 未迁 |
| `pcv/planefitting/plane_fitting.h` | `pcv::planefitting` | `pcv::geometry` | 待提供公共门面 |
| `pcv/workpiece/temp_workpiece_geometry.h`、`roi_matching.h` | `pcv::roi`、`pcv::matching` | `pcv::geometry/workpiece` | 路径不等于 namespace |
| `pcv/workpiece/temp_workpiece_interface.h` | `pcv::interface` | `pcv::workpiece` | 待迁生产调用 |
| `pcv/scan/*`、`pcv/texture/*`、`pcv/camera/*` | `pcv::scan/texture/camera` | 同当前 | 保持独立领域接口 |

<a id="evidence-0481"></a>

本轮确定策略：生产调用迁到新门面，底层旧类型/namespace 通过 alias/forwarding 保留；不进行底层全部改名。代码迁移按公共边界 → 应用/UI → 兼容审计分批，清理删除独立安排。编译验证属于后续验收；2026-10-09 仅执行文档静态检查。

<a id="evidence-0482"></a>

**命名与弃用规则**

<a id="evidence-0483"></a>

- 目录和文件：`lower_snake_case`；target：`pcv_mNN_lower_snake_case`；namespace：`pcv::<domain>`。
- 类型：`PascalCase`；函数和局部变量：`lowerCamelCase`；常量：`kPascalCase`。
- JSON 字段：`snake_case`，单位写入字段名，例如 `_mm`、`_deg`、`_ms`。
- 新公共 API 使用 `pcv::io`、`pcv::stitching`、`pcv::coordinates`、`pcv::geometry`、`pcv::workpiece`。
- 旧 `pointcloud::`、`pcv::detail::*`、旧 target 和旧 header 保留一个版本周期，按迁移批次以 `[[deprecated]]` 或 forwarding/说明标记；当前并非全部标记完成。namespace 本身不用统一弃用属性，兼容层不得新增实现。
- 每批重命名必须更新本映射表、直接依赖构建结果和回滚点，禁止无记录的全文批量替换。

<a id="evidence-0484"></a>

**4. 公共 API 与数据约定**

<a id="evidence-0485"></a>

- 30 提供分组式 `pcv::interface::StitchingRequest`（`input`、`registration`、`seam`、`artifacts`）和 `stitch()`；旧 `StitchingOptions` 已标记 `[[deprecated]]`，但当前 `stitch(request)` 内部仍映射 Options 并调用旧入口；反向适配是待实施任务。registrationdialog 与 registration_diagnostic 仍调用旧入口。
- 10 提供 `pcv::io::ArtifactTransaction`：`stageBinaryPly`、`stageGrayscalePng`、`stageJson`、`commit`、`abort`。相对路径必须位于输出根目录；默认拒绝覆盖，可选 `allowOverwrite`；取消、失败时回滚并清理 staging/backup。
- PLY 正式输出为 binary little-endian；正式 Mask PNG 为 `Grayscale8` 0/255，显示亮度纹理保留 0～255。`stageGrayscalePng` 会二值化，不能作为保真亮度 writer；`stageJson` 写序列化 bytes，schema 校验由领域调用方承担。JSON 命名规范为 `snake_case` + 单位后缀，但既有字段不因命名整理而改名。
- 坐标约定：`robot_base`；控制器位姿 `[X,Y,Z,A,B,C]`，`A=Rx`、`B=Ry`、`C=Rz`，旋转 `Rz(C)*Ry(B)*Rx(A)`；长度 mm、角度 deg。
- 手眼链：`RTmatDepth2robot` = `T_flange_depth`（flange_from_depth）；`p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`。禁止默认求逆，禁止固定 Z 翻转/镜像/180° 补丁。`LineProfileXz` 的相机点是 `[X,0,Z]`，输入 Y 只表示扫描进度（`ratio = y / signedTravel`）；`FullXyz` 保留完整 XYZ；法向与点共用同一旋转且不平移。
- 当前 30/60 业务入口会编排领域计算与 10 事务 staging/commit；最终文件操作归 10。纯结果/manifest 与提交编排的进一步分离属于目标状态，现有便利入口继续保持行为。取消不得发布部分结果或复用旧正式输出。

<a id="evidence-0486"></a>

**5. 文件所有权白名单**

<a id="evidence-0487"></a>

后端 owner：陈振佳；前端/外部项目 owner：于海军（职责基线见 target.md）。新增文件必须在交接包登记实际 owner。

<a id="evidence-0488"></a>

主线维护：`modules/**`、`src/**`、`apps/pointcloudview/mainwindow.*`、根 CMake、稳定公共头、模块测试和本文件。

<a id="evidence-0489"></a>

UI 同事限定：`apps/pointcloudview/features/**`、`apps/pointcloudview/ui/` 中经交接登记的 UI 专属 `.ui`、UI 专属资源/样式文件、UI 测试与交互说明。主线持有 `MainWindow` 壳、生命周期、模块依赖和稳定 API。现有 `pointcloudstitch` 视觉本轮不纳入统一。

<a id="evidence-0490"></a>

临时 adapter 只能位于 `apps/pointcloudview/adapters/**`，文件头必须标记 `Experimental` 或 `Deprecated`，并注明 owner、创建日期、替代 API、删除条件和截止版本；不得成为模块间长期依赖。

<a id="evidence-0491"></a>

**6. UI 与刀路规划接入边界**

<a id="evidence-0492"></a>

扫描轨迹是现有相机采集路径；高点标注、全剖/点剖刀路算法和外部项目接入由同事负责，后端只提供公共能力与版本化契约边界。外部 schema 名称/DTO 版本/调用方式以及 QWidget/QDialog 页面形态在同事交接包冻结后接入；未冻结前不得声明完成。本轮不统一 pointcloudstitch 视觉。

<a id="evidence-0493"></a>

扫描路径规划属于 60 模块领域 API。`pcv::scan::planScanTrajectory()` 支持 `Global`（拟合平面有效点）和 `LocalRoi`（当前 ROI 有效点，规划阶段不重新拟合），固定使用 WObj1 X 作为横向覆盖轴、Y 作为行程轴，产品方向固定为 `+Y → -Y`。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、两端延长 `travelExtensionMm=50`；模块从 WObj1 XY 投影包围盒自动计算中心、原始/最终行程和扫描带。P 点 ABC 直接取 WObj1 姿态，返回连续 `P0/P1...` 轨迹点、独立扫描线段、项目 `scan_trajectory_v1` JSON 和 InoRobot `sr2026-p-json-mvp-2` 兼容 JSON。应用/UI 只负责参数转换、结果展示和文件接入，不实现第二套路径算法；本版不做 IK、关节限位、碰撞或干涉平面检查。

<a id="evidence-0494"></a>

`pointcloudview` 主工作流在“边缘处理”后增加第五步“扫描规划”。页面默认局部 ROI，支持全局/局部模式、扫描高度、相机宽度和重叠距离，方向和“两端各延长 50 mm”只读显示；只读显示模块计算的中心、X 宽度、原始/最终 Y 行程、扫描线数量、实际间距和实际重叠。规划通过 `QtConcurrent` 调用领域 API，关闭窗口会取消并等待；发布前核对画布、平面、WObj1、ROI 和请求 revision。页面逐行显示内部 `P0=[X,Y,Z,A,B,C]`，并明确 P.json 的 `P[10]` 起始编号，分别以默认名 `scan_trajectory.json` 和 `P.json` 导出，覆盖前由 UI 明确确认，正式写入只通过模块 10 `ArtifactTransaction`。UI 不直接包含 `modules/*/src`，不调用应用 processor、seam fusion 或私有 writer。

<a id="evidence-0495"></a>

**7. 交接包格式与验收清单**

<a id="evidence-0496"></a>

交接包必须包含：文件白名单、API 依赖表、页面截图或交互说明、构建命令、测试结果、已知问题、临时 adapter 替代 API、owner、创建日期、删除条件、回滚点。

<a id="evidence-0497"></a>

建议交接目录清单：

<a id="evidence-0498"></a>

```text
handoff/
  ownership.md          文件白名单、owner、创建日期
  api_dependencies.md   稳定 API、DTO 版本、临时 adapter 与替代 API
  interaction.md        页面截图或交互说明、错误/取消行为
  verification.md       构建命令、实际测试结果、未验证项
  risks_and_rollback.md 已知问题、删除条件、回滚点
```

<a id="evidence-0499"></a>

当前门禁：文档链接/状态/API/target 静态一致性和 `git diff --check`。本次不运行 Debug/Release 构建、CTest 或 GUI；历史 `16/16` 等计数仅对应下方各日期。后续软件验收需要源码树外可写构建、动态回归、主窗/feature GUI smoke 与真实数据；40 移动判定、刀路算法和现场机器人验收不属于已完成能力。Release 便携部署风险参见历史 windeployqt 记录，不宣称本次已重新检查。

<a id="evidence-0500"></a>

**2026-09-30 扫描规划验收补充**

<a id="evidence-0501"></a>

- Debug/Release 构建成功；双配置全量 CTest 均 `16/16`，`scan_planner_tests` 双配置通过；`pointcloudview` 双配置自关闭启动冒烟均退出码 `0`；`git diff --check` 通过。
- GUI 人工 LocalRoi 单线/多线、参数修改、固定方向、P.json 导出和旧结果失效未完成，原因是 computer-use connector 返回 `unsupported Codex auth method: apikey`；真实相机、机器人可达性、碰撞/干涉和控制器导入仍需现场验收。

<a id="evidence-0502"></a>

**8. 历史记录：2026-09-20 PLY 临时工件线路接入**

<a id="evidence-0503"></a>

主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`、`10_pointcloud_io`；UI 编排 `apps/pointcloudview/mainwindow.*`（仍属主线维护白名单）。

<a id="evidence-0504"></a>

- 现有“打开单个 PLY”入口改造为 PLY 临时工件线路，不新增独立一键入口；文件夹扫描 PLY 保持只读预览。
- 应用层只做参数收集、状态编排、异步生命周期和结果展示：PLY 线路参数对话框直接调用模块 50 的 `loadHandEyeCalibration()` 与 `validateRigidTransform()` 做即时校验，主窗口内不新增手眼转换、平面拟合、ROI 或输出算法。
- 模块 60 的 `prepareTempWorkpiece()` 新增内存扫描描述符分支（`TempWorkpieceOptions::useInMemoryScan` + `scanInfo`），与 JSON 分支共用同一段“读取 PLY → 加载手眼标定 → `transformLineScanToRobotBase()`”实现；JSON 入口行为不变。
- 两组三点是 PLY 线路唯一的人工步骤：第一组选满后自动拟合平面并自动进入第二组；第二组同面校验通过后由既有 JSON 后半段编排自动完成 WObj1、边缘分割、ROI、Mask 与四件套提交，两线路共用同一条自动链，未复制算法。
- 应用层新增工作流状态约束（Idle/ReadingPly/Transforming/SelectingPrimaryPlanePoints/ValidatingPrimaryPlane/SelectingSecondaryPlanePoints/ValidatingSecondaryPlane/Completed/Failed/Cancelled 语义），自动与 worker 阶段禁止重复打开数据源，画布 revision 改变即失效旧会话，取消结果不得发布为成功。
- 四件套契约与错误码未变化；PLY 线路默认禁止覆盖已有正式文件，不在自动执行中途询问覆盖。

<a id="evidence-0505"></a>

验证状态：2026-09-21 源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过。GUI smoke 和真实夹具仍未验收。

<a id="evidence-0506"></a>

**2026-09-21：状态机排错与验证记录**

<a id="evidence-0507"></a>

- `apps/pointcloudview` 将临时工件来源收拢为 `None/Json/Ply` 单一状态；参数对话框取消、JSON 解析失败不再切换工作流状态。
- 旧 `Cancelled/Failed` 会话在新源参数确认后必须经过替换确认；确认后失效旧会话但保留画布，取消则完整保留旧会话。
- 自动链同步失败统一进入 `Failed`，保留当前转换点云和已完成几何结果，允许重试和打开新源；不发布部分四件套。
- JSON 位姿摘要按 `[X,Y,Z,Rx,Ry,Rz]` 显示，公共坐标转换契约不变。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke 和真实夹具未执行。

<a id="evidence-0508"></a>

**2026-09-21：代码验收与发布边界**

<a id="evidence-0509"></a>

- 代码验收：Debug/Release 源码树外构建成功，双配置全量 CTest 均为 `15/15`，`git diff --check` 通过。
- 发布边界：本次仅发布 PLY/JSON 临时工件状态机、取消/失败语义、模块 60 共享转换路径、对应测试和文档；既有模块 30/PointCloudStitch 工作区修改不纳入。
- GUI smoke、真实线扫 PLY/XML、机器人夹具和 WObj1 朝向仍需人工现场验收。

<a id="evidence-0510"></a>

**9. 历史记录：2026-09-24 手眼标定统一与点云倒转修复**

<a id="evidence-0511"></a>

主模块 `50_coordinate_conversion`；调用方清理：`apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`。

<a id="evidence-0512"></a>

- **黄金基准**：扫描 JSON 路径（`pcv::interface::prepareTempWorkpiece()` → `transformLineScanToRobotBase()`）是手眼标定的黄金基准，其余离线 JSON、PLY 临时工件、多帧和拼接路径必须与它逐点一致。
- **根因**：点云上下/法向反转风险来自“重复实现 + LineProfileXz 语义不一致”，不是矩阵方向问题。旧 `apps/pointcloudview/pointcloudprocessor.cpp` 自带刚体校验、SLERP 插值、进度轴与 `EmbeddedMotion` 启发式和内联 `baseFromFlange * flangeFromDepth`，旧 `regressionrunner.cpp` 自带 XML 解析与 `poseMatrix()`；旧合并路径还把输入 Y 当相机坐标保留。
- **修复**：50 成为唯一数学实现，新增 `composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus` 状态码；两个调用方的重复解析、重复插值和内联矩阵乘法全部删除，改调 50 的公共函数。**没有**通过矩阵求逆、Z 翻转或镜像来“修”方向。
- **调用链**：`apps/pointcloudview` 的扫描 JSON 与 PLY 临时工件路径（经 60）、`modules/60_geometry_workpiece`（`transformScanToRobotBase()`、`roi_matching`）、`modules/30_pointcloud_stitch`（`stitching_interface.cpp`、`multiframe_registration.cpp`）、`apps/pointcloudstitch` UI 与回归路径全部复用 50；调用方只保留参数转换和结果映射。
- **验证**：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，`handeye_transform_tests`、`temp_workpiece_interface_tests`（含 JSON 入口 vs 多帧入口逐点一致性）、`stitching_interface_tests`、`multiframe_registration_tests` 定向测试 `4/4`，`pointcloudview` 双配置启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio release redistributable 发出警告，便携部署未验收；真实 XML 物理方向、机器人 A/B/C 现场定义、相机安装方向和实时相机预览仍未完成现场验证。

<a id="evidence-0513"></a>

**10. 已知问题与回滚点**

<a id="evidence-0514"></a>

- 当前工作区原有用户未提交修改作为迁移基线保留；不执行远程提交、上传、删除旧兼容层或真实机器人联调。文件数量以实时 `git status --short` 为准，不在文档中冻结。
- 既有构建目录权限不足，本轮使用新的源码树外可写目录；GUI smoke 和真实夹具未在本次自动化验证中执行。
- 回滚以本文件映射表和三批迁移边界为准：优先恢复对应模块目录/CMake/转发 header，保留用户原有 8 个文件，不使用破坏性 reset/checkout。

<a id="evidence-0515"></a>

**12. 2026-10-06：离线 PLY + 亮度图纹理点云**

<a id="evidence-0516"></a>

- **范围**：在 `pointcloudview` 增加离线测试链路：打开现有 PLY，沿用三点平面提取，选择 PNG/JPG/BMP 亮度图，将图像灰度映射到现有平面点并在 3D 画布显示；不接入实时取流，不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- **60 模块**：新增 `pcv::texture::mapPlaneBrightness()`。当前版本要求单个 PLY 保留原始 `sourceIndex`，亮度图像素数必须等于原始 PLY 点数，按左上角起始 row-major 直接采样；仅 `planeIndices` 对应点着色，图像 `Format_Grayscale8`、忽略 alpha，不做投影、裁剪、缩放、翻转或配准；source provenance 不完整时拒绝并返回诊断。
- **20 模块**：新增 per-point 灰度覆盖 DTO 与校验，增加纹理灰度颜色模式和 OpenGL 上传缓冲。有效平面点显示纹理灰度，非平面点/无效点回退当前颜色模式；点云、平面或坐标系变化清除旧覆盖，模式切换保留缓存。
- **应用/UI**：增加“选择亮度图并贴到平面”入口。图像读取、映射和取消在后台执行，完成后由 GUI 线程发布覆盖并切换纹理灰度；revision 变化、取消或失败不发布半成品，新图成功前不替换旧纹理。状态栏显示尺寸、平面点数、成功/跳过数和映射方式。
- **验证**：源码树外 Debug/Release 构建成功；双配置全量 CTest 均 `17/17`；新增 `texture_mapping_tests` 与扩展后的 `pointcloud_canvas_contract_tests` 通过；`git diff --check` 需在提交前执行。
- **未完成验收**：真实 PLY + 亮度图 GUI 手工流程（方向、中心裁剪、模式切换、替换、取消、无效图片和无 WObj1 回退）、大点云性能、真实相机取流和现场机器人验收仍未完成。Release `windeployqt` 因本机缺少 Visual Studio release redistributable 未完成便携部署。

<a id="evidence-0517"></a>

**13. 2026-10-06：亮度图改为严格 PLY 栅格对应**

<a id="evidence-0518"></a>

- 离线纹理流程限定为单个 PLY 临时工件。转换阶段保留原始 PLY `declaredPointCount`、当前点到原始点的 `sourceIndices` 和过滤统计；文件夹 PLY、过滤/降采样后的点云和无 provenance 点云禁用纹理。
- 亮度图像素数必须等于原始 PLY 点数，固定使用左上角起始 row-major 索引：`x = sourceIndex % width`、`y = sourceIndex / width`。仅 `planeIndices` 对应像素映射到当前平面点；非平面像素完全忽略。
- 亮度图只做 `Format_Grayscale8` 转换，禁止局部坐标投影、包围盒覆盖、中心裁剪、缩放、翻转、配准和对比度增强。任何过滤点、缺失/重复/越界 source index 或点数变化都会使整次映射失败。
- WObj1 不参与纹理采样，WObj1 变化不清除已有纹理；点云或平面索引变化清除纹理，同一平面索引的边缘结果更新保留纹理。
- 最终验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `17/17`；两配置纹理定向测试和 `pointcloudview` 自关闭启动冒烟通过；`git diff --check` 通过。真实 PLY/亮度图的 GUI 视觉验收、现场 row-major 对应关系和实时取流仍未验证。

<a id="evidence-0519"></a>

**14. 2026-10-07：点云预览工作台 UI 简化**

<a id="evidence-0520"></a>

- **主窗口职责**：`apps/pointcloudview/mainwindow.*` 保留后台任务生命周期、取消、revision 门禁、PLY/JSON/相机/多帧业务流程和状态栏；左侧“采集与处理总控中心”、步骤导航、上一步/下一步和常驻文件列表不再作为主窗口可见布局。顶部仅保留两个数据入口和“工具”菜单，3D 预览为客户区主体，验收目标为无工具窗口时至少占客户区 85%。
- **工具窗口职责**：复用现有 Designer 控件建立五个 `Qt::NonModal` 窗口：数据源（文件列表和当前文件信息）、点云清理、平面与 WObj1、边缘与输出、扫描规划。窗口设置为不自动删除，关闭只隐藏；参数、结果和后台 watcher 仍归 `MainWindow` 管理，可同时打开多个窗口。渲染设置继续使用 `RenderDialog`，相机和多帧配准入口未移除。
- **取点交互**：开始第一组三点、第二组三点或边缘选择时隐藏对应工具窗口并显示主窗口底部临时操作条；操作条按当前模式提供计数、撤销、取消和确认/完成，Esc、Backspace、PLY 自动流程和扫描 JSON 流程复用既有语义。操作完成、取消或失败后恢复对应窗口。
- **数据源迁移**：`m_fileList` 仍是既有加载逻辑的唯一列表来源，但控件归属数据源窗口；单文件、文件夹扫描、扫描 JSON 和多帧结果继续更新同一列表和状态栏，不改变输入路径、来源状态或临时工件会话保护。
- **验证与边界**：Debug/Release 构建和全量 CTest 均通过（各 `17/17`），两配置 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟退出码为 `0`。真实点云 GUI 人工验收、85% 实际像素测量、相机和机器人现场验证仍待完成；Release `windeployqt` 缺少 Visual Studio release redistributable。

<a id="evidence-0521"></a>

- `pointcloudview` 的数据源选择增加独立“PLY 纹理实验（原始坐标）”分支。它只读取单个 PLY，保留原始 XYZ、声明点数和 identity source index，不进入模块 50 手眼转换、WObj1 建立或模块 60 临时工件四件套自动链。
- 实验分支仍复用三点平面拟合和第二组三点同面校验；实验拟合固定使用任意平面方向的垂直距离。校验成功后才开放同次扫描亮度图选择。
- 亮度图显示继续由模块 60 `mapPlaneBrightness()` 和模块 20 per-point overlay 完成：图像像素数必须等于 PLY 声明点数，左上角起始 row-major，只有 `planeIndices` 对应点着色；取消、失败、尺寸不符或 provenance 不完整均不得发布新覆盖。
- 新增 `texture_experiment_tests` 为无 QWidget/OpenGL 的应用契约测试，覆盖读取、平面、同面和严格映射；真实 GUI 视觉验收与现场 PLY/亮度图点序一致性仍需人工完成。

<a id="evidence-0522"></a>

- `60_geometry_workpiece` 的扫描线判定增加固定 `0.1 mm` 单线容差，ROI/WObj1 投影宽度不超过 `cameraWidthMm + 0.1 mm` 时只发布一条扫描线 `P0/P1`。
- 只有超过容差才按实际宽度、相机覆盖和请求重叠计算多条线；内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm` 诊断字段，InoRobot `P.json` 契约保持不变。
- 测试补充单线容差边界和超容差多线边界；真实相机有效视野、ROI 边界误差和机器人现场行为仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`；`pointcloudview` 双配置启动冒烟退出码 `0`，`git diff --check` 通过。真实 ROI GUI 复验未完成；Release 便携部署仍缺本机 Visual Studio release redistributable。

<a id="evidence-0523"></a>

**10. 2026-10-09：文档整理与后续开发登记**

<a id="evidence-0524"></a>

- 统一正式 v0.4 基线、10/60 规范入口和目标/实际 namespace 映射；旧文档保留正文，当前入口与历史记录分开。
- 当前待办：分组请求反向适配、registrationdialog/诊断工具生产调用迁移、10/50/60 公共门面与工具 target 收敛；没有在本次文档整理中实施代码迁移。
- 回滚点：本轮开始时工作区干净，变更全部为 Markdown 独立 diff；不触及源码、CMake、数据产物、提交或远端。
- 验证：本轮仅静态核对与 diff 检查，未运行构建、CTest 或 GUI。所有代码更新继续追加到 target.md 和对应模块文档。

已合并的重复证据：[evidence-0109](history_pre_v0.5.md#evidence-0109)、[evidence-0111](history_pre_v0.5.md#evidence-0111)、[evidence-0113](history_pre_v0.5.md#evidence-0113)、[evidence-0114](history_pre_v0.5.md#evidence-0114)。

<a id="source-10"></a>

## docs/architecture/io-module.md

原始来源：`docs/architecture/io-module.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0535"></a>

**支持格式**

<a id="evidence-0536"></a>

- ASCII PLY；
- binary little-endian PLY；
- binary big-endian PLY；
- 任意标量属性顺序；
- 可选 `nx`、`ny`、`nz` 法向属性。

<a id="evidence-0537"></a>

读取器按属性名读取 `x`、`y`、`z`，拒绝缺坐标、不支持的类型、vertex list、坏 header、坏数字、非有限值和截断 payload。

<a id="evidence-0538"></a>

**缓存与取消**

<a id="evidence-0539"></a>

`pcv::detail::io::readPly` 和 `readPlyCached` 保持点序、bounds、source index、取消和错误语义。缓存校验格式版本、源文件状态、点数和 payload 长度，并使用 `QSaveFile` 写入。

<a id="evidence-0540"></a>

共享读取器不访问 QWidget/OpenGL。过滤、PNG/PLY/JSON writer 和 `pcv::io::ArtifactTransaction` 同属本模块；事务提供 staging、相对路径校验、覆盖策略、取消、备份、提交和回滚。具体字段、计时和并行解析约束见 v0.3 数据契约与公共头文件。

<a id="source-11"></a>

## docs/architecture/filtering-module.md

原始来源：`docs/architecture/filtering-module.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0542"></a>

**算法边界**

<a id="evidence-0543"></a>

- 比例降采样按输入顺序保留真实点。
- 体素降采样明确区分 `FirstInputPoint` 和 `Centroid`。
- 统计滤波保留有效点和 source index；邻域不足或结果为空时按接口约定回退并返回诊断。
- 非法参数不得悄悄生成虚拟点。
- 共享过滤模块不得依赖应用或 Qt Widgets。

<a id="evidence-0544"></a>

详细参数、默认值和测试入口以 `modules/10_pointcloud_io`、`include/pcv/processing` 和 `tests/` 为准。

<a id="source-12"></a>

## docs/architecture/code-composition.md

原始来源：`docs/architecture/code-composition.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0546"></a>

**规范模块**

<a id="evidence-0547"></a>

| target | 主要职责 | 依赖边界 |
|---|---|---|
| `pcv_m10_pointcloud_io` | PLY 读写、缓存、相机帧、过滤、PNG/PLY/JSON writer、`ArtifactTransaction`（05/80/filtering 已并入） | `pcv_core`、`pcv_infrastructure`、Qt Core/Gui |
| `pcv_m20_pointcloud_render` | OpenGL context、VBO/FBO、绘制和点选 | `pcv_core`、Qt GUI/Widgets/OpenGL |
| `pcv_m30_pointcloud_stitch` | 多帧输入、ICP/多帧配准、坐标转换调用、seam fusion、诊断和 manifest（唯一拼接编排） | 10、50、Qt Core/Gui/Concurrent |
| `pcv_m40_pointcloud_matching` | 规划中的工件位置确认占位；当前为 30/60 的 `INTERFACE` 兼容聚合 | 30、60 |
| `pcv_m50_coordinate_conversion` | XML 标定、位姿插值、坐标转换（唯一数学实现） | `pcv_core`、Qt Core/Gui |
| `pcv_m60_geometry_workpiece` | 平面/边缘几何、ROI、WObj、临时工件、扫描规划、严格栅格纹理和诊断 | 10、50、Qt Core/Gui |

<a id="evidence-0548"></a>

实验相机能力由 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制，位于 `pcv_m10_pointcloud_io`
（`pcv::camera` 头与实现）。无 SDK 时仍可构建纯函数转换与 Mock 源；真实 Mv3dLp 编译
仅在 SDK 存在时启用。相机取流不属于 v0.4 稳定发布基线。

<a id="evidence-0549"></a>

`pcv::io::ArtifactTransaction` 是 PNG、PLY、JSON 和 manifest 的唯一提交边界。30/60 的现有业务入口也会编排事务 staging/commit；最终文件操作由 10 实施。将领域计算与提交编排进一步分离属于目标状态，不把现有入口描述为纯返回结果。

<a id="evidence-0550"></a>

历史目录名 `05_camerainput`、`10_pointcloudread`、`20_pointcloudrender`、
`30_pointcloudstitch`、`40_pointcloudregistration`、`50_coordinateconversion`、
`60_planefitting`、`80_planeoutput` 仅在兼容层与历史文档中出现。

<a id="evidence-0551"></a>

**应用和工具**

<a id="evidence-0552"></a>

- `pointcloudview`：界面、异步读取、画布生命周期、业务状态和 DTO 转换。
- `pointcloudstitch`：界面、输入组织、进度和结果展示。
- `intensity_viewer`：实验性相机亮度图预览与非覆盖 PNG 保存；仅在实验相机开关开启且至少构建一个桌面应用时生成。
- `registration_diagnostic`：仅链接模块 target，调用 `stitchRawLineProfiles`。
- `pointcloudcli`：独立 C++17 命令行入口，使用 Qt Core/Gui/Concurrent 及所需共享模块，提供 PLY Header/完整读取摘要与临时工件四件套流程；不链接 Widgets、OpenGL 或 GUI application target。
- `tests/`：注册模块单元测试、接口测试和必要的兼容入口测试。

<a id="evidence-0553"></a>

**兼容层**

<a id="evidence-0554"></a>

`pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、
`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、
`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，
以及 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output` 和旧 forwarding
header 在迁移期保留，只做 target/header 转发，不再增加新功能。删除前必须完成引用审计、
全量构建、CTest、GUI 和真实夹具验收，并单独提交。

<a id="source-13"></a>

## docs/modules/30_pointcloud_stitch.md

原始来源：`docs/modules/30_pointcloud_stitch.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0556"></a>

**当前算法版本（2026-10-09 回滚）**

<a id="evidence-0557"></a>

按用户要求恢复 `6329426`（2026-09-18 09:27:46 +08:00）的拼接算法。模块 CMake、`multiframe_registration.h/.cpp`、`stitching_interface.h/.cpp`、多帧与接口测试源码直接由 Git 恢复；seam 实现在该提交后未变化。移除后续新增的 heightmap 源/头与 `pose_overlap_fine_tests` 注册。GUI 恢复两种模式，默认“配准与融合”，回归工具移除后续 heightmap/fine/interface-pipeline 参数。

<a id="evidence-0558"></a>

保留共享模块 50 当前手眼数学与应用调用统一入口，并保留已有输出覆盖确认；未回滚其他模块或项目提交历史。以下 2.5D/快速精配准记录为历史实现，不代表当前能力。本轮实际验证与风险在文末追加。

<a id="evidence-0559"></a>

**2026-10-08（位姿粗配准＋重叠区快速精配准，已回滚）**

<a id="evidence-0560"></a>

- 主模块 30，复用 50 坐标转换及 10 `ArtifactTransaction`；40 不新增实现。`IcpOptions.profile` 默认为 `RegistrationProfile::Legacy`，GUI 默认新 `PoseOverlapFine`，旧完整配准和仅位姿转换入口保留。
- 新路径只在原 robot_base 相邻帧样本的重叠区域执行两层 point-to-plane ICP：体素 `0.5/0.2 mm`、距离 `3.0/0.6 mm`、迭代上限 `20/15`；连续三次增量 `<=0.005 mm/0.0005°` 提前结束。法向邻域至少 `1.5 mm`，避免细层体素小于输入间距时无法估计法向。特征值阈值仍为 `1e-3`，只解可观测方向；低对应比例/退化仅警告。
- 包围盒候选窗增加首层匹配半径容差，允许水平面的零厚度 Z 范围；有效求解仍需至少 100 个真实近邻对应。每对 `10 mm/1°`，越界、无重叠或无解弃用修正，当前帧恢复原机器人位姿并重置链锚点。成功矩阵按 `C[i]=C[i-1]*P[i]` 顺序累计，样本不累计变换，整帧只变换一次；累计不设上限但记录矩阵/漂移。
- 跳过平面身份、平面预对齐、粗/宽搜索、恢复、2.5D、旧验收和 ICP 诊断帧；新模式不执行 seam fusion。`accepted` 不冒充旧验收通过；新字段 `fineApplied/poseFallback/chainAnchorReset/fineReason/pairCorrection/cumulativeCorrection` 区分计算与质量。
- `artifacts.writePreview` 兼容默认 `true`，新 GUI profile 设 `false`。正式输出仍为 `stitched_robot_base.ply`、`.pcvmap`、`stitching_manifest.json`，全量有效点、点序和来源映射保留。报告 `processing_mode=pose_overlap_fine`、`quality_policy=warn_only`、`strict_validation_status=not_run`、`structural_metrics_status=not_computed`；逐对保存矩阵、层数、迭代/对应/残差、退化及回退，汇总完成/回退数量。
- 并行 worker 沿用 const 视图和私有结果、顺序提交。文件/标定/位姿/非有限/映射、内存、取消和事务失败仍为硬失败；GUI 写入已有结果前统一确认覆盖，不修改窗口生命周期。
- 回放工具增加 `--pose-overlap-fine` 和 `--interface-pipeline`，走模块 30 的事务输出；同配置对照可用后者或结合 `--transform-only`，新接口回放不自动覆盖已有输出。
- 本轮验证结果见文末追加的最终验收记录。真实 `D:\stitch_test` 只有原 PLY/XML，位姿 TXT 和历史回放报告缺失，不能据此声称 `1/5_40/6_50` 效果或速度已验收。双平面、局部误配与累计漂移仍可能出现，正式输出成功不代表严格几何验证。

<a id="evidence-0561"></a>

状态：已实现。本模块为多帧拼接的唯一编排模块：读取（10）、坐标转换调用（50）、多帧 ICP/结构校验、seam fusion、拼接诊断和 manifest 均在此收敛；最终文件统一通过 10 的 `pcv::io::ArtifactTransaction` 提交。规范实现位于 `modules/30_pointcloud_stitch`，模块 target 为 `pcv_m30_pointcloud_stitch`；旧 target `pcv_m30_pointcloudstitch` 保留为迁移期兼容入口（仅转发）。

<a id="evidence-0562"></a>

seam fusion 实现与公共类型位于本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；模块 40 的 `include/pcv/registration/seam_fusion*.h` 仅为转发头。

<a id="evidence-0563"></a>

接口：分组式 `pcv::interface::StitchingRequest`（`input`、`registration`、`seam`、`artifacts`）、`StitchingResult`、`stitch()` 和 `applyTrajectorySeamFusion`。旧 `StitchingOptions` / `stitchRawLineProfiles` 为弃用兼容入口。测试：`stitching_interface_tests`、`multiframe_registration_tests`、`seam_fusion_tests` 和并行等价测试。应用只负责输入组织、进度、窗口状态和结果展示。

<a id="evidence-0564"></a>

**历史变更与验证记录**

<a id="evidence-0565"></a>

**2026-09-18（2.5D 高程图配准受控迭代）**

<a id="evidence-0566"></a>

- 需求登记：主模块 `30_pointcloud_stitch`，配套公共匹配类型仍由 40 的规划占位承载；未修改 `modules/50_coordinate_conversion`、手眼矩阵、机器人 `[X,Y,Z,A,B,C]` 语义、原始 PLY 或 `workpiece_list.json`。
- 新增 `pcv::stitching::HeightMap` / `HeightMapRegistrationResult`（`heightmap_registration.h/.cpp`）：按机器人初值重叠框与候选框交集构建 2.5D 高程图，保存有效掩膜、网格计数、源点索引、原始/去趋势高程、局部范围和网格原点；分辨率由中位间距估计并限制在 `0.5~2.0 mm`，异常返回 `heightmap_grid_resolution_invalid` 或 `heightmap_insufficient_data`。
- 搜索为无外部依赖的受限 coarse-to-fine：第一阶段仅 `Δx/Δy`，第二阶段仅小范围 `Δz/yaw`，严格预算 `10 mm/1°`；使用有效重叠鲁棒高程残差、有效掩膜和候选唯一性/实际重叠检查，保留轨迹诊断和取消语义。不引入 pose graph、硬件时间戳或 `Δt` 估计。
- `IcpOptions.heightMapRegistrationEnabled` 默认 `false`，新增网格、有效比例/点数、改善率、预算、受限自由度等字段均追加在结构体末尾；`IcpDiagnostics` 追加 2.5D 尝试、评分、网格、受限 ICP、回退、来源和拒绝字段。开关关闭时沿用旧 ICP。
- 开关开启时每个相邻 pair 同时保留 legacy baseline：2.5D 通过全部新验收且受限 ICP 三层完成时来源为 `heightmap_restricted_icp`；2.5D 失败且 legacy 通过时为 `legacy_icp`；两者失败为 `none`。最终有效可见比例 `<0.25` 优先硬拒绝，报告 `visibility_hard_rejected=true`，不得以命中点内覆盖率放行。
- 应用层 `stitchingwindow.cpp` / `regressionrunner.cpp` 仅透传和序列化上述字段，并显示“2.5D 配准通过 / 失败回退旧 ICP / 低可见性拒绝 / 候选歧义 / 受限 ICP 退化”等状态；未改变窗口生命周期、异步调度或取消对象管理。
- 验证：Debug 定向构建 `cmake --build C:\pcv-build\heightmap-iteration-debug-v1 --config Debug --target multiframe_registration_tests stitching_interface_tests pointcloudstitch` 成功；定向测试 2/2 通过；Debug 全量 CTest 15/15 通过。Release 定向构建 `C:\pcv-build\heightmap-iteration-release-v1` 的三个目标成功，定向测试 2/2 通过。
- 真实回放（源码树外 `D:\pcv-replay\heightmap-v1`，输入为原始 PLY 同盘硬链接，未覆盖原始数据）：`1/test_1` 报告 `heightmap_registration_report_v2.json` 成功，网格 `1.91765 mm`、有效比例 source/target=`0.5623/0.7987`、有效点=`6709/9530`、评分改善 `10.42%`，但受限 ICP 标记退化（3 层完成），故安全回退 `legacy_icp`，生成正式 `regression_registered_robot_base.ply`；`5_40/test` 网格 `1.93265 mm`、有效比例=`0.6691/0.8358`、评分改善 `5.47%`，legacy baseline 虽报告可用，但最终比例 `0.2246` 触发 `visibility_hard_rejected`，来源 `none`，未生成正式合并 PLY。
- Release 部署未做独立便携验收：构建环境未找到 Visual Studio redist 子目录；GUI 人工冒烟、PageHeap、真实生产夹具和现场同步仍未验证。

<a id="evidence-0567"></a>

**2026-09-17（配准新选项透传与失败信息细化）**

<a id="evidence-0568"></a>

- 需求登记：主模块 `30_pointcloud_stitch`（配套 40 模块的验收口径与鲁棒性改动）。
- `pcv::interface::StitchingOptions` 增加透传字段：`structuralCoverageOfMatched`、`minimumStructuralMatchedRatio`、`visibilityWarningRatio`、`tangentCoarseTrimmedScore`、`correspondenceParallelTasks`；`stitchRawLineProfiles` 在组装 `IcpOptions` 时逐项赋值，默认值与 `IcpOptions` 保持一致，不改动既有调用方的行为。
- 失败信息：帧配准未通过时，`message` 现在同时给出机器可读 `failureCode`、原因文本、双向命中率与命中点内双向 3 mm 覆盖率，用于区分“数据不可比（视场重叠很少）”与“算法没对好”。
- 接缝融合本轮无功能改动：现网报告 `seam_fusion_ms=0`（未启用），按方案先测量再决定是否另立子项；`decisionCellSize` 维持“仅校验、保留位”。
- 验证：Debug 构建与全量 CTest 通过（`stitching_interface_tests`、`seam_fusion_tests` 未回归）。
- 风险：新选项默认值即使与 40 模块一致，仍需在真实数据回放中确认 `1/2` 组判定与修正不漂移。

<a id="evidence-0569"></a>

**2026-09-15（seam fusion 并行路径 Qt 隐式共享容器并发 detach 修复）**

<a id="evidence-0570"></a>

- 现象：PointCloudStitch 间歇性 `0xc0000005` 读访问违规，读取地址为 `0xFFFFFFFFFFFFFFFF`，表面故障点在 `QVector<SeamFusionDiagnostic>` 扩容。按“上游堆损坏”处理，未修补崩溃行。Windows 事件日志中 `pointcloudstitch.exe`（2026-09-03/07/08/15）与 `seam_fusion_tests.exe`（2026-09-03 多次）存在同类 `0xc0000005` 历史记录。
- 根因：`applyTrajectorySeamFusionKernel` 以 `merge.points = *points` 形式与调用方共享容器（GUI 路径 `apps/pointcloudstitch/seamfusion.cpp` 正是此入口），随后并行 worker 通过**非 const** 的 `merge->points[...]`、`merge->cloudIds[...]`、`buckets[i]`、`centers[i]`、`directions[i]` 读取。Qt 的非 `const` `operator[]` 会 `detach()`；多个 worker 同时对同一个 refcount > 1 的 `QArrayDataPointer` detach 时，会并发改写同一个指针成员并对旧缓冲重复 deref，造成堆破坏，随后在任意分配点（这里是 diagnostics 扩容）表现为 `0xc0000005`。
- 动态复现：在未修复源码上运行新增回归用例（`seam_fusion_tests`，每次进程独立），连续 100 次中第 34 次以 `-1073741819`（`0xC0000005`）退出；其余 99 次通过。修复后同一用例 100 次全部通过。
- 修复：新增 `SeamWorkerView`（只持有 const 指针），在启动任何任务前一次性建立 `inputPoints/inputCloudIds/inputSourceIndices/inputScanRatios/inputFrameMetadata/inputCorrections` 与 `buckets/centers/directions/seams/usable/options` 的稳定只读视图；`buildSeamBand`/`buildCoreChunk` 的捕获由宽泛的 `[&]` 改为按值捕获该视图，worker 内只使用 `.at()`/`->at()` 等 const API。seam 与 core 两波并行都保留显式 join 屏障，`seams/left/right/usable/handled/diagnostics` 等共享结果全部在串行汇合阶段按确定顺序提交，`merge->*` 只在所有 future 结束后一次性改写。`result.diagnostics.reserve(...)` 仅作为确定性与防御性措施，不作为根因修复。
- 行为不变：输入长度校验、`cloudId` 范围校验、取消粒度与取消语义、失败回滚、输出点序与来源映射、串并行结果一致性、`SeamFusionDiagnostic` 字段与 JSON schema 均未改变；未引入悬空引用。
- 测试：`seam_fusion_tests` 新增 9 帧重叠链夹具（每帧 40 × 200 点，合并 72000 点，跨 2 个 core chunk、8 条 seam），并刻意保留额外共享副本使 Qt refcount > 1。覆盖 `parallelEnabled=false` 与 `maxParallelTasks=1/2/4` 的点数/点序/来源映射/全部诊断字段一致性、重复调用确定性、共享输入缓冲未被改写、并行途中取消、无重叠结构化拒绝、空融合带、以及 30° 倾斜链的“成功或结构化失败”。

<a id="evidence-0571"></a>

**2026-09-07（第二轮性能优化 P1：seam 每 cloud 索引与接口选项）**

<a id="evidence-0572"></a>

- seam fusion：新增 SeamFusionResult::seamIndexBuildMs（按 seam 右侧融合带网格构建累计）。按 cloud 一次性建立 mutualDistance 空间索引并跨 seam 复用的变体已实现并以真实数据（438 万点合并、3 帧）评测：线性链中每 cloud 只服务一条 seam，整 cloud 索引构建 358 ms 明显高于按带网格（约 20 ms 量级），属净回归，已回退并保留按带网格实现（带内候选语义与点序不变）；若未来出现同一 cloud 参与多条 seam 的 X/Y 拓扑，每 cloud 索引才值得重估。
- 接口选项：pcv::interface::StitchingOptions::collectDiagnosticFrames（默认 true 兼容）透传至 40 模块 IcpOptions，标准输出调用方可关闭诊断帧收集。
- 验证（手工 cl 构建）：seam_fusion_tests 连续重复通过（含中途取消输入不被修改用例）；stitching_interface_tests 编译链接通过（沙箱 QTemporaryDir 限制仍阻断运行）；GUI 报告新增 timings 键（target_level_cache_ms/cache_build_ms/correspondence_ms/boundary_check_ms/diagnostic_frame_ms/seam_index_build_ms/formal_output_write_ms）与 diagnostic_frames_collected，uic+cl 语法编译通过。
- 风险：每 cloud 索引在带占比小且多 seam 场景可能高于原按带网格成本，待生产多帧数据复核；输出文件格式写入仍以模块 10 writer 为唯一权威，formal 输出与 pcvmap/preview 的完全单遍融合需流式 writer API（超出本轮模块范围）。

<a id="evidence-0573"></a>

**2026-09-07（seam fusion 分桶优化与接口计时）**

<a id="evidence-0574"></a>

- 阶段 4 seam fusion：applyTrajectorySeamFusionImpl 改为按 cloudId 一次分桶（buckets），每条 seam 只遍历相邻两个 cloud（投影范围、融合带均从桶内计算），keep/核心点单次全量扫描内联判定，每条 seam 的 matched 标记改为左/右融合带局部数组（不再为每条 seam 分配全量点数 QVector<bool>）；保持 halfWidth/mutualDistance/decisionCellSize、真实投影重叠判定、未匹配点保留与 fail-closed、取消粒度与输出点序语义不变。
- 阶段 1 计时：SeamFusionResult 增加 seamFusionMs；pcv::interface::StitchingResult 增加 readMs/transformMs/registrationMs/seamFusionMs/outputMs/totalMs，stitchRawLineProfiles 累计并在 diagnostics 附加 timings_ms 汇总行（含模块 40 各阶段聚合）。
- 验证（手工 cl 构建）：seam_fusion_tests 连续 8 次通过（含中途取消输入不被修改用例）；stitching_interface_tests 编译链接通过，沙箱内 QTemporaryDir 限制导致运行不可用（环境限制，非代码回归）；GUI pointcloudprocessor.cpp/seamfusion.cpp/stitchingwindow.cpp uic+cl 语法编译通过。
- 风险：输出点序/计数与旧实现的等价性通过 seam 单测核对有限；用户环境全量 CTest 待复验。

<a id="evidence-0575"></a>

**2026-09-06（模块重命名与 30/40 职责重划）**

<a id="evidence-0576"></a>

- 目录重命名：`modules/30_pointcloud_stitch`，target 改为 `pcv_m30_pointcloud_stitch`；旧 `pcv_m30_pointcloudstitch` 保留为兼容 INTERFACE，转发到新 target。
- 本模块成为多帧拼接的唯一编排模块：读取、坐标转换调用、多帧匹配调用、seam fusion、拼接诊断、正式输出与失败回滚均在此收敛。
- seam fusion 实现与公共类型迁入本模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；模块 40 的 `include/pcv/registration/seam_fusion*.h` 降为转发头。
- `40_pointcloudregistration` 更名为 `40_pointcloud_matching`，改为平面/ROI 点匹配与 `robot_base` 完整回传，不再负责 seam fusion 编排与最终拼接输出。

<a id="evidence-0577"></a>

**2026-09-04（元数据补全）**

<a id="evidence-0578"></a>

- `stitchRawLineProfiles` 路径为 `RobotBaseFrame` 填入 `flangeFromDepth = calibration.flangeFromDepth`，配合模块 40 使报告/诊断记录真实标定矩阵。
- 验证：未编译；待 Debug 构建与 `stitching_interface_tests`。

<a id="evidence-0579"></a>

**2026-08-31（M5.1 seam pipeline）**

<a id="evidence-0580"></a>

- M5.4：`registration_diagnostic` 已改用 `pcv::interface::stitchRawLineProfiles`，工具不再编译应用 processor；固定四帧输入、no-seam/seam 两套输出均由同一接口流水线执行。
- 验证：工具 Debug 构建通过；真实夹具运行和 GUI/接口逐字段比对待现场数据。

<a id="evidence-0581"></a>

- 移除 `seamEnabled` 的“暂时禁用”入口占位判断，使 `stitchRawLineProfiles` 可以调用共享 seam fusion。
- 无真实投影重叠时允许 fail-closed 保留完整点云并返回诊断；真实重叠但无可用互相对应点时仍返回 `PCV_STITCH_001`，禁止生成正式输出。
- 验证：接口错误安全测试覆盖启用 seam 时的非法 PLY 输入和正式输出不被替换。
- 应用 seam 适配器已改用 `pcv_m40_pointcloudregistration` 的共享结果类型别名，保留旧包装入口但移除第二套数据模型。
- `stitchingwindow.ui` 已移动至 `apps/pointcloudstitch/ui/`，CMake AUTOUIC 继续生成 `ui_stitchingwindow.h`。
- 新增 `pcv::stitching::WorldCloudInput` 纯数据 DTO；应用 `pointcloud::WorldCloudInput` 改为兼容别名，建立拼接输入类型的模块所有权。

<a id="evidence-0582"></a>

**2026-08-31（M5.1 输入 DTO 下沉）**

<a id="evidence-0583"></a>

- `WorldCloudInput` 已移入 `modules/30_pointcloudstitch/include/pcv/stitching/world_cloud_types.h`。
- 应用头仅保留 `pointcloud::WorldCloudInput` 兼容别名；`pointcloudstitch` Debug 构建和完整 CTest（14/14）通过。

<a id="evidence-0584"></a>

接口/测试：`stitching_interface`、`applyTrajectorySeamFusion`；`stitching_interface_tests`、`seam_fusion_tests`。

<a id="evidence-0585"></a>

**registration_diagnostic 通用输入**

<a id="evidence-0586"></a>

工具支持 `registration_diagnostic stitch`（省略子命令时保持兼容）以及 `inspect`。拼接可重复传入 `--input <file>`，或使用 `--input-dir <directory>` 扫描当前目录 `*.ply`；两者可混用并按规范化绝对路径去重。每个文件名需包含 `A<n>` 扫描编号，并通过 `--pose-info` 的 `A<n> Start ... ; END ...` 行匹配 `[X,Y,Z,A,B,C]` 位姿。`inspect` 支持 `--json`，报告原始 `format`、`requires_conversion` 和固定的 `canonical_format=binary_little_endian`；拼接入口仅在 canonical Binary 准备完成后继续。

<a id="evidence-0587"></a>

**变更记录**

<a id="evidence-0588"></a>

- 建立模块边界和 `pcv_m30_pointcloudstitch` 兼容入口；保留现有应用流程。
- M3/M4：`stitching_interface` 已迁移到模块源码，target 负责流程编排并依赖配准、坐标转换和读写模块。
- M5.1：应用 seam fusion 入口已增加 `ok/error/cancelled` 状态并在正式输出前 fail-closed；GUI 仍保留应用数据模型适配，待公共拼接类型完全收敛后删除适配层。

已合并的重复证据：[evidence-0325](history_pre_v0.5.md#evidence-0325)、[evidence-0117](history_pre_v0.5.md#evidence-0117)、[evidence-0118](history_pre_v0.5.md#evidence-0118)。

<a id="source-14"></a>

## docs/modules/20_pointcloud_render.md

原始来源：`docs/modules/20_pointcloud_render.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0599"></a>

**2026-09-06（模块重命名）**

<a id="evidence-0600"></a>

- 目录重命名：`modules/20_pointcloud_render`，target 改为 `pcv_m20_pointcloud_render`；旧 `pcv_m20_pointcloudrender` 保留为兼容 INTERFACE，转发到新 target。职责与依赖边界不变（Qt Widgets/OpenGL 画布、点选与视角交互）。

<a id="evidence-0601"></a>

**2026-09-04（包围盒轴线与整数刻度）**

<a id="evidence-0602"></a>

- 覆盖层的 X/Y/Z 在包围盒最小角点 `(minX,minY,minZ)` 共点，分别沿正 X、正 Y、正 Z 包围盒边延伸；使用红、绿、蓝着色，普通包围盒边不再重复绘制这些有效彩色边；
- 地板网格按 `t=i/10` 归一化后映射到当前包围盒范围（每轴 10 个区间、11 条边界线），彩色 X 边不再以灰色网格重复绘制；
- 轴标题固定为 `X(mm)`、`Y(mm)`、`Z(mm)`，刻度由实际毫米值四舍五入为整数；共点处不绘制坐标数字或刻度短线，同值标签去重，并以屏幕空间矩形跳过重叠或越界标签；
- 新增仅供渲染模块使用的 `overlay_geometry.h` 纯几何助手和回归覆盖，未扩展公共 DTO、点云数据、点选或输出契约。

<a id="evidence-0603"></a>

**2026-09-04（渲染设置覆盖层）**

<a id="evidence-0604"></a>

- `RenderOverlayOptions` 为坐标轴、地板网格、坐标标签、法向量、包围盒和统计信息提供显示开关；默认启用坐标轴、XY 地板、标签、包围盒和统计信息，默认关闭法向量；
- `PointCloudCanvas` 在深色画布上绘制有限网格、包围盒及最多 2,000 条有效法向量，辅助显示不改变点云数据、点选索引或输出契约；
- `RenderDialog` 由应用层编排，渲染模块不依赖 `apps/` 或 Qt Designer 生成代码。

<a id="evidence-0605"></a>

**2026-09（实时预览 MVP）**

<a id="evidence-0606"></a>

- `setCloudStreaming()` 的点云 VBO 改为 `StreamDraw` + 容量复用，避免每帧重新分配 GPU 缓冲；
- 流式换帧继续清除点索引覆盖层、保留视角和世界坐标覆盖层；
- 新增状态缓冲容量复用，上传失败时保持 fail-closed；
- 与 `05_camerainput` 的实时链路保持 GUI 线程上传、后台线程转换的边界；
- 未验证：真实 Windows 桌面交互和 DP2240 约 15 FPS 性能。

<a id="evidence-0607"></a>

- M5.3 收尾：`PointCloudCanvas` 全部构造、OpenGL、VBO/FBO、绘制、拾取、交互和资源释放方法已移入 `src/pointcloud_canvas.cpp`；公共头仅保留声明、信号回调和字段。
- 验证：`pcv_m20_pointcloudrender`、`pointcloudview` Debug 构建通过；完整 Debug CTest 14/14 通过。
- 未验证：Windows 桌面真实点选、矩形选区、旋转/平移/缩放和关闭生命周期。

<a id="evidence-0608"></a>

- M5.3：新增 `src/pointcloud_canvas.cpp` 作为渲染模块独立编译单元，`pcv_m20_pointcloudrender` 继续保持静态库和 Qt/OpenGL 依赖。
- 当前 `PointCloudCanvas` 方法体仍以内联形式保留在公共头中，以维持现有调用方兼容；真实桌面点选、矩形选区和视角交互尚未完成发布验收。
- `registration_diagnostic` 不依赖渲染模块；`pcv_m20_pointcloudrender` 已有独立 `pointcloud_canvas.cpp` 编译单元，后续可在不改变 DTO 的前提下继续收紧 ABI。
- 自动关闭烟测在 offscreen 环境下 15 秒超时；需要 Windows 桌面人工运行验证真实窗口关闭、点选、矩形选区和视角交互。

<a id="evidence-0609"></a>

- 建立模块边界和 `pcv_m20_pointcloudrender` 兼容入口；未移动应用代码。
- M5.3 首轮：`pcv_m20_pointcloudrender` 已从 `INTERFACE` 改为静态库，新增渲染快照版本/点状态契约和独立测试；`PointCloudCanvas` 实现仍待从 `mainwindow.cpp` 抽取。
- M5.3 第二轮：确认 `PointCloudCanvas` 直接持有 `WorkpieceCoordinateSystem`、`PlaneContour` 等应用 processor 类型；在这些类型下沉到公共头前不迁移实体，避免共享模块反向依赖 `apps/`。

<a id="evidence-0610"></a>

**2026-08-31（M5.3 快照集成）**

<a id="evidence-0611"></a>

- `PointCloudCanvas::setCloud()` 和 `setPlaneResult()` 接入 `RenderSnapshot` 校验，并在每次画布数据更新时递增渲染版本号。
- 快照校验失败时拒绝发布点云或平面状态，保留当前画布内容并记录 `qWarning`；未改变 Qt/OpenGL 线程边界。
- 验证：`pointcloud_canvas_contract_tests` 和完整 Debug CTest（14/14）通过。

<a id="evidence-0612"></a>

**2026-08-31（M5.3 DTO 下沉）**

<a id="evidence-0613"></a>

- 新增 `pcv::render::CoordinateFrame` 和 `pcv::render::Contour` 渲染 DTO。
- `PointCloudCanvas` 不再直接接收 `WorkpieceCoordinateSystem`、`PlaneContour`；应用入口负责显式转换，渲染模块保持独立依赖方向。
- OpenGL 实体仍暂留 `mainwindow.cpp`，后续可在不引入 processor 依赖的前提下迁移。
- 验证：`pointcloudview` Debug 构建、完整 Debug CTest（14/14）通过。

<a id="evidence-0614"></a>

**2026-08-31（M5.3 DTO 边界校验）**

<a id="evidence-0615"></a>

- 新增 `validateCoordinateFrame()` 和 `validateContours()`，拒绝渲染坐标系/轮廓中的非有限值。
- `PointCloudCanvas` 在更新坐标系或轮廓前执行 fail-closed 校验，失败时不改变当前 OpenGL 状态。
- `pointcloud_canvas_contract_tests` 新增 NaN/Inf 边界覆盖；完整 Debug CTest（14/14）通过。

<a id="evidence-0616"></a>

**2026-08-31（M5.3 画布实体迁移）**

<a id="evidence-0617"></a>

- `PointCloudCanvas` 实体已从 `apps/pointcloudview/mainwindow.cpp` 移至 `modules/20_pointcloudrender/include/pcv/render/pointcloud_canvas.h`。
- 画布继续使用既有 OpenGL/FBO/VBO 生命周期和 Qt 线程检查；`MainWindow` 仅保留业务编排与 DTO 转换。

<a id="evidence-0618"></a>

**2026-09（全界面浅色主题）**

<a id="evidence-0619"></a>

- 画布清屏与叠加文字保持浅色高对比显示，主窗口和相机对话框的 Designer 样式统一为白色视觉基线；
- 未改变 `PointCloudCanvas` 公共 API、点云数据、点选和覆盖层契约。

<a id="evidence-0620"></a>

**2026-09（画布深色显示区）**

<a id="evidence-0621"></a>

- OpenGL 画布恢复深色清屏背景，叠加标题、统计信息、坐标和状态文字使用浅色高对比度；
- 主窗口外围控件仍保持浅色主题，未改变 `PointCloudCanvas` 公共 API。
- 当前实现以内联形式迁移，后续可在接口稳定后拆分为 `.cpp`；真实 GUI 点选/矩形选区验收仍待执行。
- 验证补充：`pointcloudview`、`pointcloudstitch` Release 构建通过，Release CTest 14/14 通过。
- UI 资源已独立存放于 `apps/pointcloudview/ui/mainwindow.ui`，可直接使用 Qt Designer 编辑；业务源码不再与 `.ui` 文件混放。

<a id="evidence-0622"></a>

**2026-10-06（纹理灰度 per-point 覆盖）**

<a id="evidence-0623"></a>

- 新增 `PointColorOverlay` 及长度、有限值、范围和有效标记校验；`PointCloudCanvas::setTextureGrayOverlay()` 只接受与当前点云等长的数据，并在 GUI 线程提交 OpenGL 缓冲。
- 增加“纹理灰度”颜色模式：有效纹理点显示每点灰度，非平面点和无效点使用当前彩色高度、灰阶或法向量模式；切换模式保留覆盖缓存，点云/平面/坐标系更新清理覆盖。
- 不修改 `pointcloud::Point3D`、PLY 契约或点云算法接口；纹理覆盖只用于当前画布显示。
- 验证：`pointcloud_canvas_contract_tests` 覆盖长度不匹配、非有限颜色、有效标记和模式契约；Debug/Release 全量 CTest 均 `17/17`。
- 未验证：真实 OpenGL 桌面上的纹理方向、点密度和大点云上传性能。

<a id="evidence-0624"></a>

**2026-10-06（严格栅格纹理显示）**

<a id="evidence-0625"></a>

- 画布继续接收等长 per-point 灰度覆盖，但覆盖来源改为原始 PLY `sourceIndex` 对应的亮度图像素；非平面点和无效标记继续回退当前颜色模式。
- 同一 `planeIndices` 的边缘结果更新不清除纹理；点云或平面索引变化清除纹理。坐标系/WObj1 更新不影响纹理覆盖。
- 渲染层不执行图像裁剪、缩放、翻转或空间配准，纹理缓存仅用于当前画布显示。

<a id="evidence-0626"></a>

**2026-10-08（PLY 原始坐标纹理实验显示）**

<a id="evidence-0627"></a>

- 原始坐标实验入口复用现有 per-point 灰度覆盖和“纹理灰度”模式；实验点云不建立 WObj1，渲染层只接收当前平面点对应的严格 source raster 映射结果。
- 非平面点和无效标记继续使用当前颜色模式；映射失败、取消或尺寸不符时 GUI 不提交新的覆盖缓冲。
- 新增 headless 应用契约测试验证覆盖输入，不把 OpenGL/真实样本视觉方向当作自动化测试结论。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-15"></a>

## docs/modules/10_pointcloud_read.md

原始来源：`docs/modules/10_pointcloud_read.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0634"></a>

- `ScanFrameAssembler` 同步拼接几何与 `intensityData`，拒绝亮度缺失、长度不匹配或不完整扫描。
- 新增 `extractIntensityImage()` 纯函数和 `intensity_image_tests`，覆盖 Mono8 提取、无效/短负载拒绝及 PNG 回读。
- 新增独立 `intensity_viewer`，复用 `Mv3dLpCamera` 生命周期、回调和 `LatestFrameSlot`，支持设备刷新、连接、Range_Image 取流、预览和非覆盖 PNG 保存。
- Debug 验证：`intensity_viewer` 构建成功，全量 CTest `18/18` 通过；无设备 GUI offscreen 启动烟测通过。真实设备亮度帧、多批次同步和坐标/高度定义仍待现场验收。

<a id="evidence-0635"></a>

**2026-09-06（模块重命名与相机能力并入）**

<a id="evidence-0636"></a>

- 目录重命名：`modules/10_pointcloud_read`，target 改为 `pcv_m10_pointcloud_read`；旧 `pcv_m10_pointcloudread` 保留为兼容 INTERFACE，转发到新 target。
- 原 `05_camerainput` 并入本模块：相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现均随目录迁入；相机作为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力保留，无 SDK 时纯函数转换与 Mock 源仍可构建，真实 Mv3dLp 编译需 SDK。`pcv_m05_camerainput` 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。相机详细历史与验收记录见 `05_camerainput.md`。

<a id="evidence-0637"></a>

**2026-09-01**

<a id="evidence-0638"></a>

- binary 读取并行快速路径泛化：原 compactFloatXyz 仅支持"恰好 3 个 float 且顺序为 x,y,z"，现扩展为"全部属性固定宽度 + x/y/z 为 float32"即按固定 stride mmap + 多线程按列偏移解码，覆盖 4-float（x,y,z,intensity）、6-float（x,y,z,nx,ny,nz，含 canonical 缓存文件）与乱序属性，worker 数沿用自适应选择（上限 4 核），并支持 PlyReadOptions::binaryWorkerCount 显式指定。
- 新增可选强度通道：PlyReadOptions::readIntensity 开启且文件声明 intensity 属性时，PlyReadResult::intensity（与 points 等长的 QVector<float>）被填充；ASCII 与 binary（快速/慢速）路径语义一致，uchar/ushort/double 等类型归一化为 float。未开启或文件无 intensity 时保持空数组，不改变 Point3D 布局与既有结果结构。
- PlyReadResult::binaryWorkerCount 记录 binary 快速路径实际 worker 数（慢速路径为 1）。
- 测试：ply_reader_tests 新增 4-float LE/BE、乱序属性、6-float 法向、uchar/ASCII intensity 与显式/自适应多线程 worker 用例。

<a id="evidence-0639"></a>

性能优化（同日）：

<a id="evidence-0640"></a>

- P0：PlyReadResult 新增 fastPath 标记，明确区分并行固定 stride 路径与串行保底路径，便于诊断是否走快速路径。
- P1：selectWorkerCount 阈值放宽——点数 >= 10 万或 payload >= 4MB 直接使用全部 4 核；>= 2 万点或 >= 1MB 使用 2 核；低于阈值仍单线程避免线程启动开销。
- P2：decodeBinaryFloat 内联；连续 float32 x,y,z 且 little-endian、stride 4 对齐时按 float[3] 块直接读取（去逐列调用）；标量/intensity 解码改为按整数 kind 预分派的 switch（去除每点 QByteArray 字符串比较）；并行 chunk 的进度原子计数从每点一次降为每 1 万点一次（尾部余数补计）。
- P3：固定宽度属性统一走并行解码——mmap 失败时改为整块读入后仍按 stride 分块并行，非 float32 x/y/z（如 double）同样并行；仅 list 等非常规布局保留串行保底。
- 测试：ply_reader_tests 为各快速路径用例新增 fastPath 断言，并新增 double x,y,z 并行解码用例。

<a id="evidence-0641"></a>

缓存读取优化（同日）：

<a id="evidence-0642"></a>

- P0：sourceFingerprint 使用完整文件 SHA-256；`readPlyCached()` 单次生成 `SourceIdentity` 并复用到路径、校验和写入，缓存版本升至 3，避免未采样区域修改返回旧点云。
- P1：writeBinaryPly 使用整块写；`Point3D` 增加 standard-layout/六个连续 `float` 的 `static_assert`，带/不带 normals 的完整字段回读测试列入后续路线图。
- P2b：readPlyCached 中 canonical PLY 写失败不再清空结果/整体失败；结果保持 `ok=true`，并通过 `CachedCloudResult.warnings` 透传到应用层，同时记录 `qWarning`。
- 测试：cloud_cache_tests 覆盖首次解析、canonical 生成、二次缓存命中和完整指纹失效；PLY writer 回归覆盖 binary little-endian 带/不带 normals 的逐字段回读。

<a id="evidence-0643"></a>

ABI 与事故记录（同日）：

<a id="evidence-0644"></a>

- 本次对 `PlyReadOptions`（新增 binaryWorkerCount、readIntensity）和 `PlyReadResult`（新增 intensity、binaryWorkerCount、fastPath）的扩展改变了结构体尺寸，属于**破坏性 ABI 变更**：仍按旧布局编译的调用方目标文件与新的 `ply_reader.cpp.obj` 链接在一起时，`readPly()` 按新尺寸写入返回值，会越过调用方为返回值预留的栈槽。
- 实际故障：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`。`canonical` 是 `cloud_cache.cpp` 中 `readPlyCached` 的局部变量，位于返回值槽相邻位置；调用链为 `openTempScanningInfo` → `prepareTempWorkpiece` → `readPlyCached` → `readPly`。
- 根因不在算法逻辑，而在构建树：Qt Creator 的 `NMake Makefiles JOM` 目录未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`），`ply_reader.h` 变更后 `cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 没有重新编译，与新编译的 `ply_reader.cpp.obj` 混装进同一个 `.lib`。单元测试可执行文件在头文件变更之后重建，自身布局自洽，因此全部通过、无法暴露该问题。
- 历史处置已被 `VSLANG=1033` 策略替代：配置和构建阶段统一固定 MSVC `/showIncludes` 语言，并保留依赖数据库护栏。详见 `docs/development/build-windows.md`。
- 验证：新建 `NMake Makefiles` 构建目录后，`.obj.d` 中出现 `ply_reader.h`；基线全量 CTest 13/13 通过，相机开关 ON 的全量 CTest 17/17 通过。`NMake Makefiles JOM` 在当前环境生成空 `.obj.d`，不作为支持生成器。
- 约束：今后扩展 `PlyReadOptions`/`PlyReadResult` 必须把新字段追加在结构体末尾，并在本文件变更记录中显式标注"需要全量重建"。

<a id="evidence-0645"></a>

- 建立模块边界和 `pcv_m10_pointcloudread` 兼容入口；源码暂留 `src/io`，未移动用户未提交修改。
- M2：PLY 读写、缓存和 writer 已迁移到 `modules/10_pointcloudread/{include,src}`，旧 `pcv_io` 与头文件作为兼容层保留。

已合并的重复证据：[evidence-0269](history_pre_v0.5.md#evidence-0269)、[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-16"></a>

## docs/modules/05_camerainput.md

原始来源：`docs/modules/05_camerainput.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0653"></a>

**DP2240 Height 语义修正（2026-09-03，终版：仅诊断 + 自适应组帧）**

<a id="evidence-0654"></a>

- 实机证据：DP2240（Range_Image=7）回调经 SDK 转换后为完整 2100x3000 扫描，而 `GetParam("Height")` 返回 current=max=1060——Height 是传输窗口高度，不是完整扫描行数（完整行数 3000 来自 SDK 内部 `max_scan_rows`）。
- 终版行为：`configureExpectedScanHeight()` 仅读取并记录 `Height` 的 current/max/min/increment 用于诊断（不注入组帧器）；组帧器以 expectedRows=0 自适应启动，首个完整回调确定行数；无活动分片时整幅 chunk 直接发布并动态提升期望行数；仅活动分片期间的高度异常（超当前期望且非完整帧）才拒绝。
- 启动日志记录 `Height` current/max/min/inc、`ImageMode` 与诊断扫描高度，不硬编码 3000；GUI 看门狗区分完全没有回调和已收到回调但未形成完整扫描。

<a id="evidence-0655"></a>

**1. 职责**

<a id="evidence-0656"></a>

- 封装海康 3D 激光轮廓传感器 SDK（`Mv3dLpApi.h` / `Mv3dLpDefine.h` / `Mv3dLpImgProc.h`，链接 `Mv3dLp.lib`）；
- 按开发指南 V1.3.2 **回调取图流程**取流：`Initialize → 枚举 → OpenDeviceBySN/IP → （可选参数）→ （可选异常回调）→ RegisterImageDataCallBack → StartMeasure → StopMeasure → CloseDevice → Finalize`；
- **DP2240 默认 Range_Image=7**：启动和重连均必须 SetParam 成功且 GetParam 回读为 `7`；`Height` 仅作诊断读取（其值如 1060 不代表完整扫描行数），组帧器以自适应模式确定行数；C16 仅在原始 SDK 回调内调用 `MV3D_LP_MapDepthToPointCloud`，成功后深拷贝 ABC32f；
- **完整扫描门禁**：回调完整帧直接发布（无活动分片时即使高度超过初始期望也按实际高度放行并提升期望）；分片帧以首个 `nFrameNum` 锚定并按连续轮廓序号组装至自适应目标行数。该字段是设备运行序号，不要求应用启动后从 0 开始。活动分片期间的宽度变化、跳行、重复、倒序、越界、超高或内存超过 256 MiB 时丢弃当前扫描，不发布残缺线轮廓；
- **PointCloud_Image=4 仅用于显式诊断**：Profile 回调仍可在原始 SDK 回调内调用 `MV3D_LP_ImageConvert` 后交给同一组帧器。若实机确认跨批 Y 坐标不连续，保持明确诊断，不按帧号伪造毫米坐标；所有 SDK 转换均验证 ABC32f 类型、尺寸和长度，手写 scale/offset 只供 Mock/离线测试；
- **事件驱动取帧**：帧槽通过加锁 `setFramePublishedCallback`/`clearFramePublishedCallback` 通知；转换在单个 QtConcurrent watcher 中单飞执行，GUI 零阻塞；
- **异常事件与自动重连**：断线发布 `FrameKind::Exception` 事件，UI 触发自动重连（StopMeasure → CloseDevice → 重试 Open → 重新注册 → StartMeasure）；
- **软触发**：`MV3D_LP_SoftTrigger` 支持软触发模式出图；
- **触发参数兼容**：连续模式优先探测 `TriggerMode=0`；DP2240/部分固件对 `GetParam("TriggerMode")` 返回 `0x80060004 (MV3D_LP_E_PARAMETER)` 或 `MV3D_LP_E_SUPPORT` 时，按 3DMVS 回调示例保留 SDK 默认自由采集并继续启动，不把可选节点当作硬失败；若节点存在但设置 `0` 失败，仅记录兼容警告。软触发模式仍必须成功探测、设置并回读 `TriggerMode=1` 与设备支持的 `TriggerSource`，否则明确报告“不支持软触发”并拒绝启动；
- **虚拟相机识别**：3DMVS 虚拟相机（SN 前缀 Vir）按真实设备流程取流，用于无实机验证；
- 回调线程只做 SDK 原始帧的必要转换、结果深拷贝和完整扫描组帧，再发布到最新帧槽（latest-frame-wins）；不执行 QWidget 或项目点云算法；ABC32f 解析、无效点过滤、预览下采样和画布发布在后台/GUI 后续阶段完成；
- 帧转换（含空点/非有限值过滤）为纯函数，单测可覆盖三类帧类型；
- 提供 mock 帧源（含异常事件模拟），无相机环境可跑通同一转换/渲染路径。

<a id="evidence-0657"></a>

**2. 文件与 target**

<a id="evidence-0658"></a>

| 文件 | 说明 |
|---|---|
| `include/pcv/camera/frame_data.h` | `CameraFrame`（SDK 帧的拥有式拷贝）、`ImageMode`/`FrameType` 枚举、`DeviceInfo`、`CameraError` |
| `include/pcv/camera/frame_converter.h` + `src/frame_converter.cpp` | `convertPointCloudToPoints`（ABC32f）、`convertDepthToPoints`（C16）、`convertProfileToPoints`（ABC16）→ `QVector<Point3D>`，可单测纯函数 |
| `include/pcv/camera/frame_source.h` + `src/latest_frame_slot.cpp` | `LatestFrameSlot`（生产者/消费者最新帧槽）、`FrameSource` 抽象接口 |
| `include/pcv/camera/mock_frame_source.h` + `src/mock_frame_source.cpp` | 合成点云帧源（正弦曲面），模拟 SDK 回调契约 |
| `include/pcv/camera/mv3d_lp_camera.h` + `src/mv3d_lp_camera.cpp` | 真机封装（pimpl 隔离 SDK 类型）；回调内将 Profile/Depth 以 SDK 权威转换归一化为 ABC32f，并只发布完整扫描；后台只解析拥有式点云；仅 `PCV_CAMERA_HAVE_HIKSDK` 时编译 |
| `CMakeLists.txt` | 探测 `PCV_HIKSDK_ROOT`，无 SDK 时跳过真机源、mock 单测仍可运行 |

<a id="evidence-0659"></a>

target（并入前）：`pcv_m05_camerainput` 曾为 STATIC（依赖 `pcv_core` + `Qt::Core`；真机源额外链接 `Mv3dLp.lib`）；2026-09-06 起该 target 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。

<a id="evidence-0660"></a>

**3. 稳定契约**

<a id="evidence-0661"></a>

- 帧单位：真机点云坐标仅以 SDK 转换的真实 mm 值为准；`CameraFrame::xScale/yScale/zScale` 仅供 Mock/离线转换测试使用；
- 空点：`SDKEmptyPoint` 控制的无效点以 NaN/零坐标出现，转换时默认跳过非有限值（`skipNonFinite=true`），零坐标可选跳过（`skipZero`，默认 false）；
- 丢包：`CameraFrame::valid`（`bValid`）为 false 时转换器拒绝该帧；SDK 原始 Profile/Depth 与归一化 ABC32f 都要求非零尺寸且 payload 分别严格等于 `width * height * 6/2/12`；
- 回调线程安全：`LatestFrameSlot::publish`、callback setter/clear 和 `take()` 均加锁；停止时先清 callback；SDK callback context 通过 `accepting` 与在途计数隔离关闭期回调；
- 异常事件：`FrameKind::Exception` 帧携带 `exceptionDescription`，转换器拒绝该类事件并给出明确错误；
- 重连：`reconnectOnce()` 单次重连尝试，由 UI 定时器按间隔驱动；
- SDK 生命周期：模块内引用计数管理 `Initialize/Finalize`；每次成功 lease 只对应一次 `Finalize`，`stop()` 幂等；启动或重连失败也会先停止回调、等待在途回调，再关闭设备并释放 lease；
- 共享库不弹窗、不访问 QWidget；`Mv3dLpCamera` 不依赖 Qt Widgets/OpenGL。
- 诊断：真实帧按状态变化首次和每 120 帧记录一次请求/回读 ImageMode、原始/输出类型、尺寸、长度、`bValid`、scale/offset、完整扫描行数/分片数/丢弃扫描数；SDK 状态码同时记录名称，例如 `MV3D_LP_E_ALGORITHM`。

<a id="evidence-0662"></a>

**4. CMake**

<a id="evidence-0663"></a>

- 变量 `PCV_HIKSDK_ROOT`：默认 `C:/Program Files (x86)/3DMVS/Development`；`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，仍可显式关闭；
- 找到 `Includes/Mv3dLpApi.h` 且 `Libraries/win64/Mv3dLp.lib` 时定义 `PCV_CAMERA_HAVE_HIKSDK=1` 并编译真机源；
- 运行时 DLL：`Mv3dLp.dll` 位于 `C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64`，由应用 POST_BUILD 拷贝（见 `apps/pointcloudview/CMakeLists.txt`）。

<a id="evidence-0664"></a>

**5. 测试**

<a id="evidence-0665"></a>

- `tests/unit/camera/frame_converter_tests.cpp`：ABC32f→Point3D、C16 深度→Point3D（scale/offset）、ABC16 轮廓→Point3D、非有限值过滤、skipZero、bValid=false 拒绝、跨类型拒绝、长度校验；
- `tests/unit/camera/frame_source_tests.cpp`：mock 启停、最新帧槽 latest-frame-wins、take 清槽、加锁 callback setter/clear 通知、异常事件（FrameKind/描述）、convertFrame 深度帧分派，以及完整帧/连续分片/末批/跳行/重复/宽度变化/重置/内存上限/整幅高于初始期望放行/自适应首帧的组帧门禁；
- `tests/unit/pointcloudview/live_pipeline_tests.cpp`：JSON pipeline 全链（mock→voxel→publish）、未知节点拒绝、按需节点过滤。

<a id="evidence-0666"></a>

验证：实验开关 ON 的 Debug 构建、`Mv3dLp.dll` 部署和全量 CTest `17/17` 通过；实验开关 OFF 的核心 CTest `13/13` 通过。
已验证（实机 DP2240，2026-09-03）：Range_Image=7 完整扫描组帧（3000/3000 行，2100x3000=630 万点）、Height 诊断适配、连续取流出帧。
未验证项：坐标系方向（`SDKCoordinateType`）、软触发、重连、关闭时序、性能目标与画布完整点数渲染（当前预览 pipeline 默认 maxPoints=300000 截断到 30 万点显示）；`live_pipeline` 尚未接入生产相机链。

<a id="evidence-0667"></a>

**6. 变更记录**

<a id="evidence-0668"></a>

**2026-09-02（DP2240 完整扫描组装）**

<a id="evidence-0669"></a>

- 根因：`ImageMode=4` 的实际 Profile 回调为 `3200x24~30` 分片，`nFrameNum` 按分片高度递增，最后 `2976 + 24 = 3000`；逐分片发布到 `LatestFrameSlot` 只会渲染最后一批线轮廓。实机日志进一步确认首批序号可非 0，因此首批只作为连续序列锚点；
- 实现：真实设备默认恢复 `Range_Image=7`，启动和重连读取 `Height`；回调中的原始 C16 先由 SDK 转为 ABC32f，再由内部组帧器按 `nFrameNum/nHeight` 合成完整扫描，只有完整扫描进入 `LatestFrameSlot`、后台转换、画布和冻结；
- 门禁：分片缺失、重复、倒序、宽度变化、超高或超过 256 MiB 时清空当前扫描并发布明确错误；断线和停止同样清空未完成扫描；
- GUI：默认选择 Range；完整扫描显示行数、分片数和丢弃扫描数；冻结仅接受完整扫描；
- 验证：组帧单测覆盖完整帧、连续分片、末批、跳行、重复、宽度变化、重置和内存上限；DP2240/虚拟相机完整扫描、坐标方向、软触发和重连仍需现场验收。

<a id="evidence-0670"></a>

- `LatestFrameSlot` 增加发布/丢帧统计和复位接口，保持 latest-frame-wins 与线程安全语义；
- `CameraDialog` 增加“冻结当前帧”，冻结结果显式交给主窗口普通点云处理缓存，取流帧仍只用于预览；
- 实时 pipeline 增加 `filter.voxel.maxPoints`（默认 300000）显示预算，超限时确定性截断；
- 真实 C16 深度帧继续只允许 SDK `MV3D_LP_MapDepthToPointCloud` 转换，SDK 失败 fail-closed；
- 未验证：真实 DP2240 坐标方向、软触发、重连和 15 FPS 性能。

<a id="evidence-0671"></a>

**2026-09-02（DP2240 ABC32f 回调修复）**

<a id="evidence-0672"></a>

- 诊断：`DB1723975 / MV-DP2240-01P` 的 Range C16 回调有效（`2100x3000`、`12600000` 字节），但深拷贝后调用 `MV3D_LP_MapDepthToPointCloud` 返回 `0x8006000c (MV3D_LP_E_ALGORITHM)`；SDK 日志中的 `TO_PROFILE_S32 config[0] [0,0,0,0,0,0]` 表明转换上下文不完整；
- 行为：真实设备默认 `ImageMode_PointCloud_Image=4`，严格回读；仅 ABC32f 进入实时预览和冻结后的既有处理。Range 保留为显式 SDK 转换诊断，失败不发布近似坐标；
- 测试：模式 4 的 ABC32f 接收、C16/Profile 明确拒绝、ABC32f 的 `bValid=false` 与长度/尺寸门禁；
- 未验证：实机需确认模式回读为 `4`、回调为 `0x026000c0`、长度为 `width * height * 12`，以及预览、冻结、软触发、断线重连和关闭时序。

<a id="evidence-0673"></a>

- 现场证据：模式请求和回读均为 `4`，但 `DB1723975 / MV-DP2240-01P` 回传有效 `ImageType_Profile=0x023000b9`；例如 `3200x26`、`499200` 字节，严格等于 `width * height * 6`，且 scale/offset 为零；
- 修复：`MV3D_LP_ImageConvert(rawProfile, PointCloud)` 移入 SDK 回调，原始结构有效期间完成后立即深拷贝输出 ABC32f；Range 深度诊断同样在回调内调用 `MV3D_LP_MapDepthToPointCloud`。后台不再重建 SDK 图像结构或调用 SDK 转换；
- 门禁：转换状态必须为 `MV3D_LP_OK`，输出类型为 `0x026000c0`，输出长度为 `width * height * 12`。失败发布无效数据帧供 GUI 显示原因，不触发断线重连，也不发布近似坐标；
- 验证：单测覆盖原始帧动作分派和既有 ABC32f 门禁；真实设备仍需验证 `Profile -> PointCloud` SDK 调用的成功状态、坐标范围、持续帧率、冻结、停止和重连。

<a id="evidence-0674"></a>

**2026-09（本模块创建）**

<a id="evidence-0675"></a>

- 需求：相机点云图取流直连 OpenGL 画布，减少 PLY 落盘/磁盘缓存中间步骤，处理链以 JSON pipeline 编排；
- 文件：本模块全部文件；`apps/pointcloudview`（cameradialog、live_pipeline、mainwindow 相机菜单）；`modules/20_pointcloudrender`（`setCloudStreaming`）；
- 行为：回调帧 → 最新帧槽 → GUI 拉帧 → 后台转换 → `LiveFrame` → `PipelineRunner` → `setCloudStreaming` 渲染；
- 验证：实验开关 ON 的 Debug 构建和 CTest `17/17` 通过；生产链使用 Mock 的回归已覆盖，真实 DP2240 仍未验证；
- 风险：真实相机验收、坐标系映射、回调生命周期关闭时序待实机确认。

<a id="evidence-0676"></a>

**2026-09（DP2240 无效帧修复）**

<a id="evidence-0677"></a>

- 现象：DP2240（MV-DP2240-01P）接入后对话框显示"帧：无效"；
- 诊断：SDK 日志（Mv3dLpLog）显示设备为 HFR 线激光轮廓传感器，`LSLRIOutImageType=2` 且 HFR 管线缓冲为深度图+亮度图——点云模式下实际回传深度图帧（ImageType_Depth C16），而转换器此前只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 路径；`FrameSource::convertFrame` 真机走 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应不强制；对话框加取流模式选项与帧类型诊断日志（startup.log）；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 通过，应用构建通过；
- 风险：实机 DP2240 复测待执行；深度/轮廓手写公式为近似（SDK 转换优先）。

<a id="evidence-0678"></a>

- 现象：DP2240（ImageMode=7 Range_Image）StartMeasure 成功后回调被装配器丢弃，UI 显示 帧：无效；
- 日志：configured scan Height=1060（GetParam(Height) nMax=1060），回调经 MapDepthToPointCloud 输出完整 2100x3000 扫描（3000 行），装配器报 scan chunk height 3000 exceeds configured Height 1060；
- 根因：DP 系列 HFR 的 Height 参数（1060）是传输窗口高，非完整扫描行数（3000，来自 SDK 内部 max_scan_rows）；深度图模式回调本就是整幅扫描，无需行拼接；
- 修复：
  - scan_frame_assembler.h：无活动分片时整幅 chunk（高度大于 configured）按实际高度直接发布并动态提升 expectedRows；仅活动分片期间的高度异常才拒绝；
  - mv3d_lp_camera.cpp：configureExpectedScanHeight 改为仅诊断（Height 不注入装配器），装配器以 expectedRows=0 自适应，首帧确定行数；
- 验证：frame_source_tests 更新整幅放行/活动分片拒绝/自适应用例，全量 17/17 通过；应用构建通过；
- 实机确认（startup.log 2026-09-03 11:40）：complete scan rows=3000/3000 fragments=1 discarded=0 size=2100x3000，不再出现 valid=false 空帧；完整扫描 2100x3000=630 万点进入后台转换；
- 已知限制（预览显示预算）：实时 pipeline 的 filter.voxel 默认 maxPoints=300000，超限确定性截断到 30 万点显示；如需画布复现完整 630 万点，需提高/移除该上限并评估 OpenGL VBO 与渲染性能。

<a id="evidence-0679"></a>

- 依据：3DMVS V3.1.4 日志（Mv3dLpLog）确认真实相机取流 ImageMode=7（Range_Image）、虚拟相机（Vir 前缀）走同一流程；用户手册确认重发包/缓存节点/触发模式；
- 行为：默认 ImageMode=7；事件驱动取帧（callback setter/clear）；转换移出 GUI 线程（QtConcurrent）；异常事件 + 自动重连（reconnectOnce + 2s 定时器）；软触发（SoftTrigger）；虚拟相机识别与标记；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 通过；应用构建通过；
- 未验证：真实相机软触发/重连/深度图取流实机复测待执行；重发包为 SDK 内部默认（Mv3dLpApi 未公开 SetResend 接口，已在文档记录）。

<a id="evidence-0680"></a>

**2026-09（DP2240 安全回归）**

<a id="evidence-0681"></a>

- 真机 C16/ABC16 SDK 转换失败改为 fail-closed，禁止静默发布近似坐标；Mock/离线路径仍保留纯函数换算用于自动化测试。
- 深度/轮廓帧新增非零尺寸及 `payload == width * height` 校验；停止流程清除帧通知后再等待回调和释放 SDK。
- 清洁 `NMake Makefiles` Debug/Release 构建验证：SDK ON 全量 CTest `17/17`，SDK OFF 基线全量 CTest `13/13`；实机 DP2240 GUI/坐标/断线验收待现场执行。

<a id="evidence-0682"></a>

**2026-09（Designer 采集工作台升级）**

<a id="evidence-0683"></a>

- `CameraDialog` 改为 Designer 驱动的设备详情页：设备选择会显示型号、序列号、IP 与真实/虚拟设备状态，并保留 Mock 合成点云源用于无相机演示；
- 主窗口增加 Designer 工具栏和四步向导导航（采集、清理、平面、输出），工具栏仅转发到既有取流、扫描 JSON、文件输入与画布重置入口；
- 激光器、ROI、采集帧率和灵敏度以置灰控件明确标记“SDK 参数接口待接入”，不保存本地假状态、不下发设备；
- 未改变 `DeviceInfo`、`FrameSource`、转换器、实时 Pipeline 或共享模块 API；真实相机 GUI、软触发、重连和坐标仍须现场验收。

<a id="evidence-0684"></a>

**2026-09-06（并入 10_pointcloud_read）**

<a id="evidence-0685"></a>

- 2026-09-06 模块重命名：`05_camerainput` 并入 `10_pointcloud_read`（`modules/10_pointcloud_read`，target `pcv_m10_pointcloud_read`），本模块不再作为独立模块。
- 相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现迁入该模块；相机为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力，无 SDK 仍可构建纯函数转换与 Mock 源，真实 Mv3dLp 编译需 SDK。
- 旧 target `pcv_m05_camerainput` 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。本文档转为历史记录，不再作为当前规范；既有验证结论与历史 CTest 计数仅用于追溯。

已合并的重复证据：[evidence-0605](history_pre_v0.5.md#evidence-0605)、[evidence-0323](history_pre_v0.5.md#evidence-0323)、[evidence-0309](history_pre_v0.5.md#evidence-0309)、[evidence-0311](history_pre_v0.5.md#evidence-0311)。

<a id="source-17"></a>

## docs/modules/100_qualityreport.md

原始来源：`docs/modules/100_qualityreport.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0688"></a>

**2026-09-17**

<a id="evidence-0689"></a>

- v0.4 仍保持未实现状态，不报告虚假质量结果；报告 DTO 和 JSON schema 待后续版本冻结。

<a id="evidence-0690"></a>

- 建立未实现模块占位和后续需求入口。
- 最后核对日期：2026-10-09（文档静态核对）；本轮不实现功能。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-18"></a>

## docs/modules/60_geometry_workpiece.md

原始来源：`docs/modules/60_geometry_workpiece.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0694"></a>

**1. 职责与当前接口**

<a id="evidence-0695"></a>

本模块负责平面拟合、边缘/ROI、WObj1、临时工件、扫描轨迹与严格栅格亮度映射，依赖 10/50、core/infrastructure 和 Qt Core/Gui；不依赖 `apps/` 或 QWidget。

<a id="evidence-0696"></a>

| header | current_api | target_state |
|---|---|---|
| `pcv/planefitting/plane_fitting.h` | `pcv::planefitting::fit` | `pcv::geometry` 公共门面 |
| `pcv/workpiece/temp_workpiece_geometry.h` | `pcv::roi` | `pcv::geometry/workpiece` 门面 |
| `pcv/workpiece/roi_matching.h` | `pcv::matching::matchRoiToRobotBase` | 规范几何门面，旧匹配头保留转发 |
| `pcv/workpiece/temp_workpiece_interface.h` | `pcv::interface::prepareTempWorkpiece/finalizeTempWorkpiece` | `pcv::workpiece` 门面 |
| `pcv/scan/scan_planner.h` | `pcv::scan::planScanTrajectory` 与两种 JSON 序列化 | 保留独立扫描领域接口 |
| `pcv/texture/texture_mapping.h` | `pcv::texture::mapPlaneBrightness` | 保留独立纹理领域接口 |

<a id="evidence-0697"></a>

`pcv/workpiece/` 是当前 header 路径，不表示 `pcv::workpiece` namespace 已落地。旧 60/70 target 与 interface/roi/matching 转发头保留兼容周期。

<a id="evidence-0698"></a>

**2. 平面、ROI 与临时工件**

<a id="evidence-0699"></a>

**3. 扫描轨迹与严格纹理**

<a id="evidence-0700"></a>

扫描规划支持 `Global`（拟合平面）/`LocalRoi`（当前 ROI，不重拟合），UI 默认 LocalRoi；WObj1 XY 投影包围盒定义中心/范围，中心沿 `+Z` 偏移。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、`travelExtensionMm=50`，产品方向固定 `+Y→-Y`、线序 `-X→+X`。宽度 `<= cameraWidthMm + 0.1 mm` 为一条线，超过容差再计算均匀扫描带，实际重叠不低于请求值。

<a id="evidence-0701"></a>

内部点为 `P0/P1...` 的独立扫描段，ABC 继承 WObj1；项目 JSON 为 `scan_trajectory_v1`。InoRobot JSON 为 `sr2026-p-json-mvp-2`，`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`，P[10] 起始、最多 9990 点，XYZ 为 WObj1 局部坐标、姿态顺序 `[X,Y,Z,RZ,RY,RX]`。应用负责后台取消/revision、展示与通过 10 事务导出，不写控制器，不实现 IK/碰撞或全剖/点剖刀路算法。

<a id="evidence-0702"></a>

亮度映射固定 `source_raster_index`，原始 PLY 点数必须等于图像像素数，原点序为左上角 row-major，`pixelX=sourceIndex%width`、`pixelY=sourceIndex/width`。只有平面索引对应点着色；缺失/重复/越界 source index、过滤或点数变化使整次映射失败。不裁剪/缩放/翻转/配准/增强，只转 Grayscale8，忽略 alpha。WObj1 不参与采样，也不因 WObj1 变化清除纹理。

<a id="evidence-0703"></a>

原始坐标纹理实验入口直接保留 PLY XYZ 与 identity source index，两组三点同面校验后接入相同映射；它不转换到 `robot_base`、不建立 WObj1、不生成四件套。显示灰度可为 0～255，不等于正式 Mask 的二值 0/255。

<a id="evidence-0704"></a>

**4. 开发与验收记录**

<a id="evidence-0705"></a>

已有测试为 `plane_fitting_tests`、`plane_fitting_equivalence_tests_v2`、`roi_matching_tests`、`temp_workpiece_interface_tests`、`scan_planner_tests`、`texture_mapping_tests`；实验应用入口另有 `texture_experiment_tests`。

<a id="evidence-0706"></a>

2026-10-09 仅整理规范文档，不运行构建/CTest/GUI。等价提速的性能与第三点到完整显示的 GUI 验收、真实控制点/Start/End、row-major 一致性、WObj1 朝向、机器人可达性与碰撞仍需按各任务独立验收。

<a id="evidence-0707"></a>

历史正文及变更保留在 [60_plane_fitting.md](../modules/60_geometry_workpiece.md) 和 [70_roi_template.md](../modules/60_geometry_workpiece.md)。当前需求见 [v0.4 基线](../requirements/pointcloudview_v0.4.md)，接口/owner 见 [协同文档](../architecture/module_ui_integration_v0.4.md)，后续任务见 [开发方案](../development/development_plan_v0.5.md)。

<a id="source-19"></a>

## docs/modules/50_coordinate_conversion.md

原始来源：`docs/modules/50_coordinate_conversion.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0716"></a>

**2026-09-24（手眼标定统一与点云倒转修复）**

<a id="evidence-0717"></a>

- 需求：以“打开扫描 JSON”路径为手眼标定黄金基准，统一离线 JSON、PLY、多帧和拼接路径的
  camera→robot_base 坐标转换，消除因重复实现或 LineProfileXz 语义不一致造成的点云上下/法向反转风险。
- 根因（两条，都不是矩阵方向问题）：
  1. 坐标转换存在第二套实现：`apps/pointcloudstitch/regressionrunner.cpp` 自带 `RTmatDepth2robot` XML 解析和
     `poseMatrix()`；`apps/pointcloudview/pointcloudprocessor.cpp` 自带 `validateRigidTransform()`、
     `interpolateRigidTransform()`、`RigidTransformLut`、`scanProgress()`，并在点云循环里内联
     `baseFromFlange * flangeFromDepth` 与法向旋转。
  2. LineProfileXz 语义不一致：旧合并路径把输入 Y 当作相机空间坐标保留，并按 `VertexOrder`/`LocalX/Y/Z`
     进度轴与 `EmbeddedMotion`/`Auto` 启发式决定位姿，与 JSON 黄金路径的 `y/signedTravel` + `[X,0,Z]`
     语义不同，可能在同一份 PLY 上得到不同的上下/法向结果。
- 修复：本模块成为唯一数学实现，并新增公共入口集中校验与诊断：
  - `composeBaseFromDepth()` 固定 `T_base_depth = T_base_flange * T_flange_depth`；
  - `computeScanTravel()` 统一由 Start/End 平移差计算主导轴与**带符号**行程；
  - `lineProfileCameraPoint()` / `lineProfileScanRatio()` 显式固化 LineProfileXz 契约；
  - `transformPointToRobotBase()` 变为“校验 + 旋转（法向）/旋转加平移（点）”的单一入口，并新增可选错误输出；
  - `RobotCloudStatus` 状态枚举和 `robotCloudStatusCode()`/`robotCloudStatusText()` 让以下失败可区分：
    手眼标定矩阵无效、Start 位姿无效、End 位姿无效、Start/End 没有有效行程、输入点云为空、
    LineProfileXz 扫描进度超出允许范围、输入点全部无效或不符合 LineProfileXz 契约、取消。
- 调用方清理：`apps/pointcloudstitch/regressionrunner.cpp` 删除重复 XML 解析与 `poseMatrix()`，改用
  `loadHandEyeCalibration()` / `robotPoseToMatrix()`（命令行、报告 JSON 与输出文件保持兼容）；
  `apps/pointcloudview/pointcloudprocessor.cpp` 删除重复校验、插值、进度启发式与内联矩阵乘法，改为调用本模块
  `transformLineScanToRobotBase()` 完成整帧转换，ICP、体素和拼接算法未改动，合并缓存版本号提升到 9。
- 行为/API：已发布的矩阵方向、位姿顺序、旋转约定、单位与既有错误文案保持不变；新增符号均为增量
  （`ScanTravelInfo`、`RobotCloudStatus`、`composeBaseFromDepth`、`computeScanTravel`、`lineProfileCameraPoint`、
  `lineProfileScanRatio`、`robotCloudStatusCode`、`robotCloudStatusText`），`transformPointToRobotBase()` 追加可选的
  `QString *error` 尾参。
- 验证：`handeye_transform_tests` 覆盖乘法方向、黄金链路平移/旋转、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、
  正负行程与反向扫描、FullXyz、法向同旋转不平移、位姿约定与往返、非有限/非正交/非单位/行列式非 +1 的拒绝、
  无行程/空点云/全部无效/超范围/取消和状态码；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口
  逐点一致性比对。源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，4 项直接相关测试 `4/4`，
  `pointcloudview` Debug/Release 启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史
  baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio
  release redistributable 发出警告，便携部署未验收；真实 XML 物理方向、机器人 A/B/C 现场定义、相机安装方向和
  实时相机预览仍未完成现场验证。

<a id="evidence-0718"></a>

**2026-09-21（应用状态修复与位姿显示契约校准）**

<a id="evidence-0719"></a>

- `apps/pointcloudview` 的 JSON Start/End 摘要改为 `[X,Y,Z,Rx,Ry,Rz]`，底层模块 50 的位姿字段和旋转数学未改变。
- PLY/JSON 临时工件入口继续共用 `transformLineScanToRobotBase()`；本轮仅修复应用会话生命周期和失败恢复，不新增坐标转换实现。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke 和真实标定样件仍待验证。

<a id="evidence-0720"></a>

**2026-09-20（PLY 线路复用：参数校验直接调用与取消回调透传）**

<a id="evidence-0721"></a>

- 需求：`apps/pointcloudview` 的“打开 PLY”入口必须复用本模块作为唯一坐标转换实现，并显示手眼标定方向和矩阵校验结果。
- 文件：`apps/pointcloudview/mainwindow.cpp`（PLY 线路参数对话框在选择/变更 XML 后立即调用 `loadHandEyeCalibration()` 与 `validateRigidTransform()`）；`modules/60_geometry_workpiece/src/temp_workpiece_interface.cpp`（转换选项显式固定 `layout = LineProfileXz`、`interpolateRotation = true`、`sampleStride = 1`，并把取消回调同时透传给 `readPlyCached()` 的 `PlyReadOptions::isCancelled`）。
- 行为/API：模块 50 的公共头、函数签名、位姿约定（`[X,Y,Z,A,B,C]`、`A=Rx`、`B=Ry`、`C=Rz`、`Rz(C)*Ry(B)*Rx(A)`）、单位（mm/deg）和错误语义均未变化；未新增导出符号，未新增第二套转换实现。
- 验证：历史记录为静态检查；2026-09-21 源码树外 Debug 全量构建成功，CTest `15/15` 通过。
- 风险：PLY 线路的 Start/End 插值方向与手眼方向仍需真实标定文件和生产样件验收。

<a id="evidence-0722"></a>

**2026-09-21（代码验收记录）**

<a id="evidence-0723"></a>

- Debug/Release 源码树外构建成功，双配置全量 CTest 均为 `15/15`；转换数学、公共位姿字段和错误码契约未改变。
- GUI smoke、真实手眼 XML 和机器人运动方向未在本次自动化环境中验证。

<a id="evidence-0724"></a>

- 目录重命名：`modules/50_coordinate_conversion`，target 改为 `pcv_m50_coordinate_conversion`；旧 `pcv_m50_coordinateconversion` 保留为兼容 INTERFACE，转发到新 target。职责（手眼标定、位姿插值、坐标变换）与依赖边界不变。

<a id="evidence-0725"></a>

- 建立独立模块映射和兼容入口；暂不移动共享实现。
- M2/M3：手眼标定和线扫坐标转换源码已迁移到 `modules/50_coordinateconversion`，旧头文件通过 forwarding header 兼容。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0599](history_pre_v0.5.md#evidence-0599)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-20"></a>

## docs/modules/40_pointcloud_matching.md

原始来源：`docs/modules/40_pointcloud_matching.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0732"></a>

**历史变更记录（现有配准归 30，ROI 归 60）**

<a id="evidence-0733"></a>

**2026-09-18（2.5D 高程图配准契约边界）**

<a id="evidence-0734"></a>

- 本轮 2.5D 高程图配准的唯一编排和正式输出边界仍在 `30_pointcloud_stitch`；40 不新增工件基准比较、移动阈值、6DoF 判定或时间同步能力。
- 30 的 `IcpOptions` / `IcpDiagnostics` 追加 2.5D 开关、网格、有效掩膜、评分、候选预算、受限 ICP 和最终来源字段，默认关闭；40 的规划占位不改变既有 `registerRobotBaseFrames()` 兼容包装和坐标语义。
- 验收口径：2.5D 只允许 `Δx/Δy` 加小范围 `Δz/yaw`，严格 `10 mm/1°`；要求有效比例和有效点数、相对改善、候选唯一性、实际重叠、三层受限 ICP、非退化及最终可见比例同时满足。失败允许回退旧 ICP，但 `<0.25` 最终有效可见比例必须结构化硬拒绝。
- 明确未实现：pose graph、硬件时间戳接口、`Δt` 估计、基准模板持久化、工件移动判定和机器人执行下发；这些不属于本轮 40 功能。
- 真实回放仅作为 30 的受控迭代验证：`1/test_1` 2.5D 因受限 ICP 退化回退 legacy；`5_40/test` 以 `0.2246` 可见比例触发 `visibility_hard_rejected`，不因约 `0.512` 局部 3 mm 覆盖率或 legacy baseline 可用而放行。

<a id="evidence-0735"></a>

**2026-09-17（结构点验收改为命中口径；粗对齐 trimmed 目标；ICP 内并行与提速）**

<a id="evidence-0736"></a>

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

<a id="evidence-0737"></a>

**2026-09-15（并行 overlap 预计算改为只读访问）**

<a id="evidence-0738"></a>

- `registerRobotBaseFrames` 的并行 overlap 预计算不再通过非 `const` 的 `clouds[index]` 访问共享的 `QVector<ConvertedCloud>`；改为 `const QVector<ConvertedCloud> *const cloudView` 只读视图，worker 内统一使用 `cloudView->at(index).sample`。捕获列表由 `[&, index]` 收敛为 `[cloudView, index]`。
- worker 仍只返回私有 `OverlapDiagnostic`，在显式 join 屏障后由 `preparedOverlaps[index] = futures[index].result()` 串行提交，结果与扫描顺序无关。
- 每对 ICP 的修正矩阵累计（`result.registrationCorrections[index]`）与 `clouds[index].full` 变换继续严格按扫描顺序串行执行，本轮**未**并行化该提交路径。
- `clouds` 是函数内局部容器（refcount 恒为 1），因此该处属并发 `detach` 的潜在隐患；修复目标是消除该类风险，不改变任何公共契约：未修改 `IcpOptions`、`IcpDiagnostics`、`SeamFusionDiagnostic` 或 JSON schema，共享 `CancellationToken` 生命周期设计保持不变。
- 测试：`multiframe_registration_tests` 新增 4 帧串行/并行（`maxParallelTasks=1/2/4`）等价性用例，逐字段比较 `overlapDiagnostics`（含 cloudId/面积/覆盖率/IoU/warning）、点数与点序、`cloudIds/sourceIndices/scanRatios`、修正矩阵全部 16 个元素及 ICP 诊断数量，并保留输入额外共享副本；新增 30° 倾斜对在串行与并行下“成功或结构化拒绝且不取消”的回归。

<a id="evidence-0739"></a>

**2026-09-15（陡峭主平面配准）**

<a id="evidence-0740"></a>

- `IcpOptions` 增加 `maximumPlaneTiltDegrees=75`、确定性 RANSAC 假设数、最小平面内点比例、平面身份法向/偏移公差与陡峭面最小可观测自由度。浅倾角继续使用既有 Z 高度带路径；高度带无效或不代表主体时，`pcaPlane()` 改用 1 mm 采样的确定性 `ransac_pca` 精化。
- 陡峭面以目标平面局部 `u/v/n` 坐标建立重叠框，重叠点计数、ICP 输入、步进保留与结构验收共用该口径；候选变换后从完整 source/target 重建局部重叠。非平面结构和局部 `u/v` 边界点加入 Point-to-Point 辅助残差，主平面仍保留 Point-to-Plane；浅倾角仍使用原有 robot XYZ 路径。
- 倾角超过 75 deg 返回 `plane_tilt_exceeds_limit`；陡峭面在 ICP 前及结构验收时均要求两侧不少于 100 个非平面结构点、`observableDof >= 3` 和双向 3 mm 覆盖率不低于既有阈值，否则返回 `steep_plane_feature_insufficient`，不发布正式结果。
- `IcpDiagnostics` 与 `stitching_report.json` 补充拟合方法、法向/倾角/RMS/内点、局部重叠、特征点与陡峭面拒绝原因。坐标链仍只报警，不自动翻转或补偿。
- 验证：Debug 构建 `multiframe_registration_tests`、`stitching_interface_tests`、`seam_fusion_tests`、`pointcloudstitch` 成功；新增 60/75 deg 特征面通过、76 deg 限制拒绝、60 deg 纯平面 fail-closed 的自动回归。真实陡峭夹具和 `D:\stitch_test\6_50` 尚待重放。

<a id="evidence-0741"></a>

**2026-09-08（低重叠 retry 与粗对齐边界修复）**

<a id="evidence-0742"></a>

- 修复放宽 retry 未恢复缓存拟合平面的问题：retry 不再因 `sourcePlane/targetPlane` 为默认无效值而将结构验收误报为 `0/0`。
- 切向粗对齐和大范围结构候选应用后重新计算实际三维重叠框及两侧点数；候选超出修正预算、破坏重叠或产生空重叠时回退到候选前状态。
- 切向粗对齐候选按 `maximumCorrectionTranslation`/`maximumCorrectionAngleDegrees` 逐级缩放，优先保持现有安全边界，再进入 ICP。
- 验证：Debug 目标构建成功，`multiframe_registration_tests` 通过；真实 `D:\stitch_test\6_50` GUI/现场重放待复验。

<a id="evidence-0743"></a>

**2026-09-07（第二轮性能优化 P0：缓存扩展与 ICP 内循环）**

<a id="evidence-0744"></a>

需求：PointCloudStitch 第二轮性能优化方案 P0/P1（P2 并行化按计时证据门控，本轮未实施）。

<a id="evidence-0745"></a>

- P0 预处理缓存：PairPreprocessCache 增加按层 target 工件缓存（LevelTargetEntry：reference 含法向 + SpatialIndex，键为层入口修正矩阵；矩阵一致 ⇒ overlap box 与结构决策一致，strict/retry 共享 downsample/estimateNormals/buildIndex，命中时源侧 moving 仍按尝试重导出）；原初 try/retry 共享的 crop/平面/初始距离统计保留。
- P0 ICP 内循环：estimateIncrement 接受可复用 correspondence 与 unique 引用 scratch（QVector<CorrespondencePair>/std::unordered_set，按轮次容量保留，消除每轮分配）；reference 恒为 actualOverlap 裁剪后体素化的点集，候选循环移除冗余 target insideBounds；27 邻域改为全局静态偏移表 neighborOffsets27；trial 边界检查改用仅变换 XYZ 的 countInsideBoundsAfterTransformXyz（法向对边界计数无贡献）。
- 计时/诊断：PairStageTimes/IcpDiagnostics/MultiFrameRegistrationResult 增加 targetLevelCacheMs/correspondenceMs/boundaryCheckMs（attempt 级 + 聚合）与 targetLevelCacheHit；IcpOptions::collectDiagnosticFrames（默认 true 兼容；GUI 标准模式 false）按需生成 pre/post 诊断帧并累计 diagnosticFrameMs。
- 验证（手工 cl 构建，/MD，O2 基准）：multiframe_registration_tests、seam_fusion_tests 连续通过；真实 robot_base 3 帧数据（90 万至 246 万点）balanced 与 diagnostic 修正矩阵与上轮完全一致（逐位），新增子计时字段按预期输出；cache 命中路径与重算路径由 seam/registration 单测及真实数据矩阵一致性覆盖。
- 风险/未验证：cmake 全量 Debug/Release CTest 与 GUI 实机待用户环境复验；target 缓存仅放宽重试命中（正常一次成功尝试无二次使用）；结构验收区 2mm 样本跨尝试缓存未实施（依赖最终修正的源侧无法安全共享），仍为后续候选。

<a id="evidence-0746"></a>

**2026-09-07（配准与拼接耗时优化，阶段 1/2/3）**

<a id="evidence-0747"></a>

需求：PointCloudStitch 配准与拼接耗时优化方案（源码级分析基线：粗搜索/ICP 热循环/重复预处理/seam 全量扫描/GUI 输出为热点；本会话无 cmake/ninja 环境，采用手工 cl 链路完成模块级验证）。

<a id="evidence-0748"></a>

- 阶段 1 计时基线：IcpDiagnostics 增加每尝试阶段耗时 preprocessMs/coarseSearchMs/icpLevel1-3Ms/structuralValidationMs/attemptTotalMs/strictAttemptTotalMs/levelIterations；MultiFrameRegistrationResult 增加聚合 readMs/transformMs/preprocessMs/coarseSearchMs/icpLevel1-3Ms/structuralValidationMs/retryMs/registrationTotalMs/registrationAttemptCount。registerPair() 用 RegionTimer 分区计时（预对齐前为 preprocess、粗搜索为 coarse、每 ICP 层独立、结构校验独立），严格档与放宽档均计入聚合。应用 wrapper（pointcloudprocessor.cpp）累计 read/transform 回填结果。
- 阶段 3 分级启用：IcpOptions 默认改为 tangentCoarseAlignmentEnabled=false（新增 tangentCoarseAlignmentAuto=true：平面预对齐后覆盖率/重叠条件未满足时自动触发）、wideStructureDiagnosticEnabled=false（诊断模式显式重开）。relaxed retry 改为按 blockingClass 触发（仅 safety_boundary/prealignment_boundary/structural_validation；no_overlap、取消与输入错误不重试；主平面角度拒绝等 prealignment 类失败现纳入重试范围）。
- 阶段 2 预处理缓存与 ICP 内循环：新增 PairPreprocessCache（bounds/crop/平面拟合/初始距离统计/每层源采样基点），严格档与放宽档共享，retry 只重算依赖修正矩阵的部分；collectInitialPairDiagnostics 拆分为 computePairInitialData+stampPairInitialData；ICP step factor trial 改为复用 scratch buffer 的 countInsideBoundsAfterTransform（变换与计数合并、达阈值提前退出）；每层增加亚微米级无改善终止（连续 8 次增量平移不超过 5um 且旋转不超过 0.0005deg 视为收敛），最终安全与结构验收仍把关质量。
- 验证（本会话手工 cl 构建，/MD，O2 仅用于基准）：multiframe_registration_tests、seam_fusion_tests 连续 8 次通过；真实 robot_base 数据（D:/workpiece/output/pointcloud/1..3_*_robot_base_retained.ply，90 万至 246 万点，stride 8 采样）注册基准：平衡模式与诊断模式结果完全一致（修正矩阵逐位相同），wide 粗搜索由诊断模式约 414ms/对降至平衡模式约 8ms/对（自动触发条件跳过）；阶段聚合耗时、逐对层级迭代与阶段 ms 字段均按预期填充。
- 风险/未验证：cmake/Ninja 全量 Debug/Release CTest 与 GUI 运行需在用户环境复验；stitching_interface_tests 在本沙箱受 QTemporaryDir 目录创建限制无法运行（编译链接通过）；真实夹具多帧/粗细搜索/retry 组合占比仍需用户生产数据验收；GUI 改动仅完成 uic+cl 语法编译，未做 UI 人工冒烟。

<a id="evidence-0749"></a>

**2026-09-06（模块重命名与职责重划为 ROI 点匹配）**

<a id="evidence-0750"></a>

- 目录重命名：`modules/40_pointcloud_matching`，target 改为 `pcv_m40_pointcloud_matching`；旧 `pcv_m40_pointcloudregistration` 保留为兼容 INTERFACE，转发到新 target。
- 职责重划：模块改为平面/ROI 点匹配与 `robot_base` 完整回传。新增正式接口 `pcv::matching::{RoiMatchInput, RoiMatchResult, matchRoiToRobotBase(...)}`（`include/pcv/matching/roi_matching.h`）：校验 ROI 索引、平面来源、点数量与有限性；经 50 坐标转换生成 `robot_base` 点并保留原始点索引；fail-closed（任一 ROI 点无法合法回传则不发布不完整结果）；返回完整性统计、丢失点数、覆盖率、取消状态与失败原因。
- seam fusion 实现与公共类型迁至 30 模块 `include/pcv/stitching/`（`seam_fusion.h/.cpp`、`seam_fusion_kernel.h`、`seam_fusion_types.h`）；本模块 `include/pcv/registration/seam_fusion*.h` 降为转发头。本模块不再负责 seam fusion 编排与最终拼接输出。
- `registerRobotBaseFrames()`/ICP/结构校验等底层能力保留（兼容接口，内部逐步转向 matching API）；`multiframe_registration` 位于本模块 `include/pcv/registration/multiframe_registration.h`。

<a id="evidence-0751"></a>

**2026-09-04（报告元数据真实化）**

<a id="evidence-0752"></a>

- `RobotBaseFrame` 增加 `flangeFromDepth`；`registerRobotBaseFrames()` 生成的 `FrameTransformMetadata` 现复制真实的 `flangeFromDepth`/（既有）`signedTravel`/`dominantTravelAxis`，修复 stitching_report 中 `flange_from_depth=identity`、`signed_travel=0`、`dominant_travel_axis=-1` 的失真（此前 GUI 路径在 `mergePlyCloudsInWorld` 组装 moduleFrames 时未携带这些值）。
- 说明：`stitching_report.json` 新增 `orientation_check`（净旋转 Z 系数、台面高度、台面上下采样分布、`suspected_upside_down`），用于真机核对输出是否上下颠倒。
- 验证：本会话沙箱禁止外部程序，未编译；待 Debug 构建 + `multiframe_registration_tests`/`seam_fusion_tests`/全量 CTest 与真实数据验收。

<a id="evidence-0753"></a>

**2026-09-03**

<a id="evidence-0754"></a>

- 崩溃加固：`IcpOptions` 增加共享 `cancellationToken`，ICP 优先读取稳定的 `std::shared_ptr<std::atomic_bool>`；GUI 任务将同一 token 贯穿配准任务，保留 `std::function<bool()>` 兼容入口。
- retry 内存收敛：`registerPair()` 的 source 保持只读，严格档和放宽档不再提交失败中的临时 sample；覆盖率改善使用已有诊断结果，避免 retry 额外复制整帧点云。
- 验证：Debug 目标构建成功，`multiframe_registration_tests` 连续运行 3 次通过，全量 Debug CTest `17/17` 通过。尚未使用真实夹具回归，崩溃 UAF 仍需 GUI 重放或 ASan/PageHeap 进一步确认。

<a id="evidence-0755"></a>

- 取消令牌生命周期契约：`cancellationToken` 别名移至 `seam_fusion_types.h`，`SeamFusionOptions` 增加同款 token 并统一走 `cancellationRequested()`；GUI `m_cancelRequested` 改为窗口生命周期单令牌（构造函数创建、开始任务仅 `store(false)` 不再替换对象），开始门卫改为 `m_watcher` 非空即拒绝重入，`stitchingFinished` 先 disconnect 再清理，消除“上一任务 finished 未处理时重入/双 finished/令牌 use-after-free”；模块 30 接口路径在入口自建内部 token，并由注册进度回调同步置位，使所有热循环只读稳定原子、不再依赖调用方 std::function 生命周期。取消语义不变：阶段边界生效。
- 验证：Debug 目标构建 + `multiframe_registration_tests`/`seam_fusion_tests`（含 token 取消用例）通过；GUI 启动冒烟通过；真实数据连续/取消/快速重开场景仍待用户数据验收。

<a id="evidence-0756"></a>

- 配准 pair 改为原子提交：严格档和放宽档均从同一原始 sample 副本运行，只有最终 accepted 才提交 sample、full 和全局修正。
- 增加一次性放宽重试：仅针对安全边界、主平面平移或结构验收失败触发；放宽上限按稳健初始误差计算并限制在 `50 mm / 3 deg`，仍要求三层收敛、重叠点数、可观测自由度、修正预算和覆盖率护栏全部通过。
- 结构验收增加低点数分级：`100~499` 点使用双向 `3 mm >= 0.45` 且 `5 mm >= 0.80`，结构点不足时允许 `+-2 mm` 平面带二次验收；严格档默认值保持不变。
- `stitching_report.json` 和 regression report 增加 `attempt_count`、`primary_attempt_reason`、`relaxed_retry_*`、`accepted_by_retry`、稳健初始误差及双向 `5 mm` 覆盖率字段。
- 验证：Debug 全量构建，`multiframe_registration_tests` 通过；全量 CTest `17/17` 通过。尚未接入真实夹具回归，位姿图优化暂未实现。

<a id="evidence-0757"></a>

- M5.4 收尾：诊断工具改为直接消费本模块经 `pcv_m30_pointcloudstitch` 暴露的统一接口，ICP/seam diagnostics 字段保持原输出语义。
- 验证：Debug/Release 全量 CTest 均为 14/14；工具 Debug 构建通过。
- 风险：旧兼容 target `pcv_registration` 仍保留，待弃用周期和生产引用审计完成后单独删除。

<a id="evidence-0758"></a>

- M5.1：`pointcloudstitch` 的生产路径在手眼转换后统一调用 `registerRobotBaseFrames()`，ICP、相邻帧诊断和结果模型由本模块提供；应用层旧实现仅作为待清理兼容代码保留。
- 验证：`pointcloudview`、`pointcloudstitch` Debug 构建成功，完整 Debug CTest `14/14` 通过。
- M5.4：已删除应用层 `registerPair()`、旧 ICP 循环及其诊断辅助函数；`pointcloudstitch` 保留读取/手眼转换后直接转交 `RobotBaseFrame`。模块实现成为唯一生产配准算法来源。
- `registration_diagnostic` 已改为编译 `pointcloudstitch` 兼容处理器并链接 `pcv_m10_pointcloudread`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`；不再直接编译 `pointcloudview` processor。

<a id="evidence-0759"></a>

- 建立模块边界和兼容入口；配准实现暂留 `src/registration`。
- M3：多帧 ICP 与接缝融合源码已迁移到 `modules/40_pointcloudregistration`，旧 `pcv_registration` 保留兼容聚合。
- M5.1：共享 `seam_fusion` 已从固定禁用占位改为真实投影重叠检查、融合带裁剪和候选最近邻插值；应用层入口保持兼容包装。当前仍需补充多帧、无重叠、取消和 bounded-memory 回归。
- M5.1 接口收敛：`pcv_m30_pointcloudstitch` 已允许启用共享 seam fusion；无真实投影重叠按 fail-closed 保留点云，真实重叠但无互相对应点仍失败并阻止正式输出。

<a id="evidence-0760"></a>

**2026-08-31（M5.4 兼容层审计）**

<a id="evidence-0761"></a>

- 生产引用仍包括 `apps/pointcloudstitch/pointcloudprocessor.cpp` 的独立 `registerPair()`/`mergePlyCloudsInWorld()`，以及 `tools/registration_diagnostic` 对该处理器的直接编译。
- `pcv_registration`、`pcv_io`、`pcv_output` 仍被兼容 CMake 聚合 target、旧公共头和测试引用，当前不满足删除条件。
- 下一步必须先统一应用处理器与 `pcv_m40_pointcloudregistration` 的结果类型，再迁移诊断工具和测试，之后才能单独提交兼容层删除。
- seam fusion 增加处理中途取消检查；取消发生在输出提交前，原始点云映射保持不变，不发布部分融合结果。
- 融合带中的未匹配点现在显式保留，并累计到 `SeamFusionDiagnostic::unmatchedPreserved`；只有成功匹配的点生成插值结果。
- 应用 `seamfusion.h/.cpp` 改用 `pcv::SeamFusionOptions/Result/Diagnostic` 类型别名，包装层不再复制结果模型。
- 应用 `pointcloudprocessor.h` 已改为直接复用 `multiframe_registration.h` 的 `IcpOptions`、`IcpDiagnostics`、`FrameTransformMetadata` 和 `MultiFrameRegistrationResult`；仅保留兼容函数入口和 `WorldCloudInput` 别名。

<a id="evidence-0762"></a>

**历史补充：40 规划声明**

<a id="evidence-0763"></a>

正式声明：对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

<a id="evidence-0764"></a>

当前状态：**规划目标，未实现**。本模块当前不实现基准模板持久化、当前扫描与基准扫描比较、移动阈值和 6DoF 偏差判定、机器人执行或坐标修正下发，也不产生新的正式输出闭环。`pcv_m40_pointcloud_matching` 仅聚合 30/60 的兼容能力，旧 registration/matching header 仅转发。正式设计和 UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-21"></a>

## docs/modules/70_roi_template.md

原始来源：`docs/modules/70_roi_template.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0769"></a>

- 建立模块边界和 `pcv_m70_roi_template` 兼容入口；未改变现有 ROI 行为。
- M4：临时工件输入解析、平面/ROI 准备和四件套接口已迁移到模块源码，行为契约保持不变。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-22"></a>

## docs/modules/60_plane_fitting.md

原始来源：`docs/modules/60_plane_fitting.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0772"></a>

**扫描路径规划（2026-09-30 修订）**

<a id="evidence-0773"></a>

`pcv::scan::planScanTrajectory()` 使用 `ScanPlanningOptions` 生成机器人法兰/TCP 参考位姿 `[X,Y,Z,A,B,C]`。扫描轴固定为 WObj1：横向覆盖沿 `X`，行程沿 `Y`；扫描线按 `-X → +X` 排列，产品方向固定为 `+Y → -Y`，不采用蛇形路径。

<a id="evidence-0774"></a>

中心和范围规则：

<a id="evidence-0775"></a>

- `Global` 使用当前拟合平面 `globalPlaneIndices`，`LocalRoi` 使用当前 ROI `localRoiIndices`；两者均拒绝空、越界和非有限点，LocalRoi 规划阶段不重新拟合平面，局部失败不回退全局。
- 有效点投影到 WObj1 XY 后取包围盒中心、X 宽度和 Y 行程长度；中心沿 WObj1 `+Z` 偏移统一输入的 `scanHeightMm`。
- P 点的 `ABC` 直接取 WObj1 的 `poseA/poseB/poseC`；端点固定为 ROI `vMax + travelExtensionMm` 和 `vMin - travelExtensionMm`，结果同时返回原始和最终行程。

<a id="evidence-0776"></a>

扫描带规则使用 `stepTarget = cameraWidthMm - overlapMm`。目标宽度 `<= cameraWidthMm + 0.1 mm` 时只生成 `P0→P1`；超过固定单线容差后按 `ceil((requiredWidthMm-cameraWidthMm)/stepTarget)+1` 计算带数，并在首尾有效覆盖边界之间均匀布置，实际重叠不小于请求值。默认 `cameraWidthMm=100`、`overlapMm=50`、`scanHeightMm=50`、`travelExtensionMm=50`。每条线生成两个连续编号点（`P0→P1`、`P2→P3`），带间移动不属于扫描线段。单线时 `actualOverlapMm` 沿用请求值，不代表存在相邻带；内部 JSON 的 `single_line_width_tolerance_mm` 记录固定容差，原始目标宽度不截断。

<a id="evidence-0777"></a>

输入校验包括有效 WObj1、有限 WObj1 姿态、有效平面/ROI、投影范围、`cameraWidthMm > 0`、`scanHeightMm >= 0` 和取消回调。结果返回结构化错误码、最终中心、自动范围、轴/方向、请求与实际重叠、间距、P 点和独立段；失败或取消不返回成功轨迹。

<a id="evidence-0778"></a>

`scanTrajectoryToJson()` 输出 `scan_trajectory_v1`，使用 `snake_case`，核心字段为 `mode`、`center_pose`、`required_area`、原始/最终行程、扫描带参数、方向、`points` 和 `segments`。`scanTrajectoryToInoRobotJson()` 输出 `sr2026-p-json-mvp-2`：`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`，点号从 10 开始且最多 9990 点，姿态重排为 `[X,Y,Z,RZ,RY,RX]`，XYZ 转为 WObj1 局部坐标，不生成控制器写入调用。`pointcloudview` 第五步“扫描规划”默认局部 ROI，后台调用模块 API、显示内部 `P0=[...]` 与导出 `P[10]...` 的编号区别，并通过模块 10 `ArtifactTransaction` 原子导出项目 JSON 或 `P.json`；画布、平面、WObj1、ROI 或参数变化会取消/清除旧结果。

<a id="evidence-0779"></a>

- 根因：扫描线数量原先使用 `requiredWidth > cameraWidthMm` 的严格比较，ROI/WObj1 投影和浮点误差可能使本应覆盖一条线的区域略超 `cameraWidthMm`，误生成第二条扫描线。
- 修复：模块内固定 `kSingleLineWidthToleranceMm=0.1 mm`；`requiredWidthMm <= cameraWidthMm + 0.1 mm` 时只生成一条线和 `P0/P1`，只有超过该阈值才按实际覆盖规则生成多条线。
- 结果 DTO 和内部 `scan_trajectory_v1` JSON 增加 `single_line_width_tolerance_mm` 诊断字段；InoRobot `P.json` schema、点号和 pose 顺序保持不变。
- 测试覆盖 `100.099 mm`、`100.1 mm` 单线边界和 `100.1001 mm` 多线边界；真实相机有效视野与现场 ROI 边界仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`（含 `scan_planner_tests`）；双配置 `pointcloudview` 自关闭启动冒烟退出码均为 `0`，`git diff --check` 通过。未使用触发问题的真实 ROI 做 GUI 复验；Release 部署仍缺本机 Visual Studio release redistributable。

<a id="evidence-0780"></a>

**2026-09-30（扫描规划验收）**

<a id="evidence-0781"></a>

- `scan_planner_tests` 补齐实际重叠下限、Y 两端固定延长、倾斜 WObj1 的 P.json 局部 XYZ、无效坐标轴、退化投影范围和 `P[10]...P[9999]` 边界断言。
- Debug/Release 全量 CTest 均 `16/16`；扫描定向测试双配置通过；`pointcloudview` Debug/Release 自关闭启动冒烟均退出码 0；`git diff --check` 通过。
- GUI 人工流程未验证：本机 computer-use connector 初始化失败（`unsupported Codex auth method: apikey`）；真实 ROI、相机视野标定、机器人可达性和安全性仍须现场验收。

<a id="evidence-0782"></a>

- 将未发布的旧扫描语义修订为固定 WObj1 X/Y 轴、毫米重叠距离、人工目标尺寸和两种显式 Y 行程方向；删除从全局平面范围推导、自动选轴、固定 50% 重叠和 `marginMm` 语义。
- 全局中心直接采用人工 `XYZABC`；局部中心采用 ROI 三维质心投影与 `+Z` 高度，独立执行并继承界面 `ABC`。
- `pointcloudview` 增加独立扫描规划页、后台取消/revision 门禁、逐行 P 点显示、覆盖确认和事务 JSON 导出。
- 单元测试覆盖单线/多线、均匀间距、编号与方向、倾斜 WObj1、局部中心、非法输入、取消和 JSON；Debug 定向测试已通过，完整 Debug/Release 结果见本轮最终验收记录。
- 风险：未验证真实机器人可达性、相机实际有效宽度、现场安全距离、碰撞和干涉。

<a id="evidence-0783"></a>

**2026-09-21（临时工件自动链失败恢复）**

<a id="evidence-0784"></a>

- 应用层统一管理 `None/Json/Ply` 临时工件会话；参数取消、旧会话替换和画布版本变化不会复用过期 source index 或覆盖策略。
- 自动 WObj1、边缘分割、ROI/Mask 和四件套前置失败统一进入 `Failed`，保留已完成点云/几何结果并允许重试；主动放弃取点保留会话和转换点云，重新取点即可继续。
- 取消错误码按发生阶段保持不变，`cancelled=true` 为取消权威标志：PLY 读取为 `PCV_INPUT_002`，转换为 `PCV_TRANSFORM_001`，调用前或 finalize 取消为 `PCV_CANCELLED`；文档不再声称所有取消均为 `PCV_TRANSFORM_001`。
- 验证：源码树外 `scan-ui-debug-20260922` / `scan-ui-release-20260922` 构建成功，Debug/Release CTest 均为 `16/16` 通过；启动冒烟通过。真实样件与完整 GUI 交互仍待验证。

<a id="evidence-0785"></a>

**2026-09-20（PLY 临时工件线路：内存扫描描述符与共享转换尾段）**

<a id="evidence-0786"></a>

- 需求：普通 PLY 没有扫描 JSON 元数据，但必须走同一套临时工件流程并输出同一套四件套。
- 文件：`modules/60_geometry_workpiece/include/pcv/workpiece/temp_workpiece_interface.h`、`modules/60_geometry_workpiece/src/temp_workpiece_interface.cpp`。
- API（追加字段，向后兼容，未删除或改名）：
  - `TempWorkpieceOptions::useInMemoryScan`、`TempWorkpieceOptions::scanInfo`：PLY 线路用内存扫描描述符（scan_id、created_at、LineProfileXz、camera->robot_base、Start/End、PLY 与 XML 绝对路径），跳过 `temp_scanning_info.json` 读取；该标记为 false 时 JSON 入口行为完全不变。
  - `TempWorkpiecePreparation::inputYMinimum`、`inputYMaximum`、`signedTravel`、`dominantTravelAxis`、`cancelled`：透传 `pointcloud::transformLineScanToRobotBase()` 的诊断，供 UI 展示转换质量。
- 行为：PLY 线路没有 JSON interface 目录，`TempWorkpiecePreparation::interfaceDirectory` 等于输出目录，`scanningInfoPath` 为空；`prepareTempWorkpiece()` 的 PLY 读取、手眼标定加载和坐标转换尾段合并为一个共享实现，JSON 与 PLY 入口使用同一算法、同一阈值和同一错误码。取消时保留发生阶段错误码，由新增的 `cancelled=true` 标记区分，调用方不得把取消发布成成功会话：读取阶段为 `PCV_INPUT_002`，转换阶段为 `PCV_TRANSFORM_001`，调用前或 finalize 为 `PCV_CANCELLED`。PLY 线路要求输出目录为绝对路径，缺少输出目录时报 `PCV_CONTRACT_001`。
- 契约：四件套文件名（`baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`）、binary little-endian PLY、Grayscale8 0/255 PNG、`sr2026-temp-workpiece-info-mvp-2` JSON 字段与精度规则均未变化；正式提交仍由模块 10 的 `ArtifactTransaction` 完成，失败整体回滚。
- 测试：`tests/unit/interface/temp_workpiece_interface_tests.cpp` 增加 PLY 线路用例（合法/非法 XML、非刚体矩阵、Start == End、空 PLY、全部点被拒、非有限位姿、相对路径、取消、四件套格式与 schema、同名拒绝覆盖、取消不产出、写入失败整体失败）；`tests/unit/planefitting/plane_fitting_tests.cpp` 增加第二组三点同面校验用例。
- 验证：历史记录为静态检查；2026-09-21 源码树外 Debug 全量构建成功，CTest `15/15` 通过；GUI 验收仍待执行。
- 风险：PLY 线路端到端四件套内容（WObj1 朝向、ROI 与 Mask）未在真实样件上比对。

<a id="evidence-0787"></a>

- PLY/JSON 自动链失败恢复、取消语义和会话替换规则已完成代码验收；Debug/Release 全量构建成功，CTest 均为 `15/15`。
- WObj1 朝向、ROI/Mask 实际内容和四件套现场结果仍需真实样件与生产夹具确认。

<a id="evidence-0788"></a>

- 目录重命名：`modules/60_plane_fitting`，target 改为 `pcv_m60_plane_fitting`；旧 `pcv_m60_planefitting` 保留为兼容 INTERFACE，转发到新 target。职责（平面拟合与几何诊断）不变。

<a id="evidence-0789"></a>

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
- M5.2 首轮：新增独立 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` API；应用入口暂继续调用完整 legacy 算法以保留连通域和 PCA 诊断，待字段完整迁移后再切换。
- M5.2 第二轮：迁移 `calculateBoundsCenter` 及越界/非有限点校验，应用入口通过兼容包装调用模块实现。
- M5.2 契约补齐：`Options` 和 `Result` 已覆盖连通域、RANSAC/PCA 迭代、随机种子、预览延迟分类及断开区域诊断字段；完整算法仍待从 processor 迁移，当前新增字段不改变旧入口行为。
- M5.2 算法迁移完成：`pcv::planefitting::fit` 实现 PCA 初始模型、RANSAC、PCA 重分类、预览延迟分类和连通域筛选；`extractPlaneFromPoints` 改为兼容 forwarding wrapper。新增模块实现与应用级几何回归均通过。
- M5.4：删除 `extractPlaneFromPointsLegacy()`，应用入口继续保留兼容参数转换；处理器不再包含第二套平面拟合算法。

<a id="evidence-0790"></a>

**2026-10-06（离线亮度图纹理映射）**

<a id="evidence-0791"></a>

- 新增 `pcv::texture::mapPlaneBrightness()`，复用现有拟合平面和 `planeIndices`，并通过单个 PLY 的原始 `sourceIndices` 直接读取同次扫描亮度像素；不改变平面拟合、PLY 或正式输出契约。
- WObj1 存在时使用局部 X/Y；缺少 WObj1 时将拟合平面法向规范化，并从最不平行的世界轴投影生成稳定局部 X/Y。使用全部有效平面点局部包围盒做覆盖范围，宽高比不一致时中心裁剪，最近邻采样。
- 图像统一转换为 `QImage::Format_Grayscale8`，忽略 alpha，不做自动对比度增强；只覆盖现有平面点，越界、无效和无法采样点跳过并计数，取消不发布结果。
- `pointcloudview` 通过后台任务选择亮度图并显示纹理统计；当前只显示每点灰度，不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- 验证：`texture_mapping_tests` 已覆盖 WObj1/固定轴回退、方向、裁剪、无效点、取消和空平面；Debug/Release 全量 CTest 均 `17/17`。
- 风险：真实样本 GUI 视觉方向、中心裁剪和大点云性能仍需人工验收。

<a id="evidence-0792"></a>

**2026-10-06（亮度图严格栅格对应）**

<a id="evidence-0793"></a>

- 亮度纹理不再使用平面局部坐标包围盒投影。单个 PLY 临时工件必须保留原始 `sourceIndices` 和 `declaredPointCount`，图像像素数必须完全匹配原始 PLY 点数。
- 固定采用左上角起始 row-major：`pixelX = sourceIndex % image.width`、`pixelY = sourceIndex / image.width`；仅当前 `planeIndices` 对应点读取亮度，其他点不使用图像像素。
- RGB/JPG/PNG 只转换为 `Format_Grayscale8`，不裁剪、缩放、翻转、配准或增强；任意过滤点、点数变化、缺失/重复/越界 source index 都拒绝整次映射。
- WObj1 不参与亮度采样；平面或点云变化使纹理失效，WObj1 变化不使纹理失效。
- 验证：`texture_mapping_tests`、`pointcloud_canvas_contract_tests` 在 Debug/Release 均通过；源码树外 Debug/Release 全量 CTest 均 `17/17`，两配置 `pointcloudview` 自关闭启动冒烟通过，`git diff --check` 通过。
- 风险：真实 PLY 与同次扫描亮度图的人工 GUI 视觉验收、row-major 点序现场一致性和实时取流接入仍未完成。

<a id="evidence-0794"></a>

**2026-10-08（PLY 原始坐标纹理实验）**

<a id="evidence-0795"></a>

- `pointcloudview` 增加独立“PLY 纹理实验（原始坐标）”来源，直接保留单个 PLY 的 XYZ、原始点序、声明点数和 identity source index；不经过手眼转换，不建立 WObj1，不输出临时工件四件套。
- 实验平面复用两组三点和同面校验，第一组三点使用 `useZAxisResidual=false` 的点到平面垂直距离，支持任意朝向；第二组三点通过后才能选择同次扫描亮度图。
- 亮度图继续使用严格 `source_raster_index`：像素数等于 PLY 声明点数，左上角起始 row-major，只给 `planeIndices` 对应点着色；失败或取消不发布结果。
- `texture_experiment_tests` 覆盖声明点数/source index、垂直平面、同面校验、像素映射、取消和尺寸错误拒绝；真实样本视觉方向和现场点序仍待验收。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0111](history_pre_v0.5.md#evidence-0111)、[evidence-0095](history_pre_v0.5.md#evidence-0095)、[evidence-0722](history_pre_v0.5.md#evidence-0722)、[evidence-0599](history_pre_v0.5.md#evidence-0599)、[evidence-0325](history_pre_v0.5.md#evidence-0325)、[evidence-0113](history_pre_v0.5.md#evidence-0113)、[evidence-0114](history_pre_v0.5.md#evidence-0114)。

<a id="source-23"></a>

## docs/modules/80_plane_output.md

原始来源：`docs/modules/80_plane_output.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0798"></a>

- 目录重命名：`modules/80_plane_output`，target 改为 `pcv_m80_plane_output`；旧 `pcv_m80_planeoutput` 保留为兼容 INTERFACE，转发到新 target。职责（Grayscale8 PNG、binary little-endian PLY、JSON 成套输出与回滚）不变。

<a id="evidence-0799"></a>

- 建立模块边界和兼容入口；输出实现暂留 `src/output`。
- M2：PNG/PLY/JSON 输出实现已迁移到 `modules/80_planeoutput`，旧 `pcv_output` 保留兼容聚合。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0599](history_pre_v0.5.md#evidence-0599)、[evidence-0325](history_pre_v0.5.md#evidence-0325)。

<a id="source-24"></a>

## docs/development/module_convergence_v0.4.md

原始来源：`docs/development/module_convergence_v0.4.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0803"></a>

版本：v0.4。日期：2026-10-09。状态：文档整理已按本方案执行；代码/API 收敛批次尚未实施。本轮只做静态检查，不构建、不运行 CTest 或 GUI。当前事实见 [正式 v0.4 基线](../requirements/pointcloudview_v0.4.md)。

<a id="evidence-0804"></a>

**1. 本轮交付与边界**

<a id="evidence-0805"></a>

全仓 Markdown 逐个登记当前/历史/兼容/实验/未实现状态，修正 namespace、模块归属、失效链接和索引。新增规范 10/60 模块文档，旧 05/10/60/70/80 文档保留正文并加归档入口；40 保留单一一级标题和统一未实现声明。历史测试结论按日期保留，不以历史通过率替代本次验证。

<a id="evidence-0806"></a>

本次仅修改 Markdown。以下批次是后续代码开发顺序，不在本次整理中修改源码、CMake、JSON schema、输出文件或默认值。代码更新必须登记受影响模块文档、协同映射表及 [target.md](../../target.md)，包含日期、owner、文件/API、行为、验证、风险与回滚点。

<a id="evidence-0807"></a>

**2. 批次一：公共边界与兼容映射**

<a id="evidence-0808"></a>

| domain | current_entry | planned_entry |
|---|---|---|
| IO/预处理/平面输出 | `pcv::detail::io/filtering`、`pcv::output` | `pcv::io` 公共门面 |
| 拼接 | `pcv::interface::StitchingRequest/stitch` 与 `pointcloud::` 算法类型 | `pcv::stitching` 门面 |
| 坐标 | `pointcloud::`、`pcv/registration/handeye_transform.h` | `pcv::coordinates` 门面 |
| 平面/几何 | `pcv::planefitting/roi/matching` | `pcv::geometry` 门面 |
| 工件流程 | `pcv::interface`、`pcv/workpiece/*` | `pcv::workpiece` 门面 |
| 扫描/纹理/相机 | `pcv::scan/texture/camera` | 保持独立领域接口 |

<a id="evidence-0809"></a>

采用增量 alias/forwarding 或薄门面，底层类型与算法 namespace 暂保留，不做全部实现改名。新门面不复制算法、不修改结构体布局/默认值/错误码；旧头、target 和 namespace 保留一个兼容周期。可标记的旧类型/函数使用 `[[deprecated]]`，namespace/转发头以说明和迁移记录标注；不能假装 namespace 本身可统一加弃用属性。

<a id="evidence-0810"></a>

30 的分组请求成为主实现，旧 `StitchingOptions/stitchRawLineProfiles` 反向适配。兼容映射逐字段记录，显式保留旧 `allowOverwrite=true` 与新请求 `false` 的不同默认值，并完整透传配准、seam、preview、诊断、进度与取消。2026-10-09 算法已恢复到 `6329426`，后续收敛以该完整配准流程为准，不重新引入已回滚的 profile/2.5D 增量。

<a id="evidence-0811"></a>

IO writer 与事务仅保留 10 实现；30/60 领域结果与提交编排的拆分先保留现有便利入口行为，再逐步提供纯结果/manifest 边界。所有正式产物走同一事务，提交失败不得返回正式成功状态。

<a id="evidence-0812"></a>

**3. 批次二：生产调用迁移与 UI 接入**

<a id="evidence-0813"></a>

先迁 `pointcloudview registrationdialog` 与 `registration_diagnostic` 的旧 stitching 调用，再迁 view processor/MainWindow、CLI 和必要工具的 10/50/60 公共入口。工具 target 改用规范 10/30/50，清理仅为旧 registration 路由而链接的 40 聚合依赖；保留原输入/输出与命令契约。修正 10 CMake 中仍推荐旧 target 的注释。

<a id="evidence-0814"></a>

主线持有 MainWindow、模块依赖和任务生命周期；同事负责 feature 组件、登记的 UI/资源、交互说明与 UI 测试。临时 adapter 限定在 `apps/pointcloudview/adapters/` 并记录替代 API/删除条件。页面控件形态和外部高点/刀路 schema 在同事交接包内冻结，冻结前只保留版本化占位，不实现规划算法。pointcloudstitch 视觉不属于本轮统一。

<a id="evidence-0815"></a>

每个迁移批次记录旧/新 header、target、namespace 和实际调用方。当前代码存在的兼容接口应保留用于回归；不得用全仓无记录字符串替换改变历史记录或公开 JSON 字段。

<a id="evidence-0816"></a>

**4. 批次三：审计、验证与回滚**

<a id="evidence-0817"></a>

静态门禁覆盖：Markdown 本地链接/锚点/围栏/一级标题；规范目录/target/API 与源码一致；共享模块无 apps 依赖；生产旧调用有明确迁移清单；取消/失败发布和事务覆盖政策保持；`git diff --check`。本轮执行这些静态核对，不把“静态通过”写成“编译通过”。

<a id="evidence-0818"></a>

后续动态验证单独安排：源码树外可写 Debug/Release 构建、全量 CTest 与相关定向回归、view 主窗/feature GUI smoke、CLI-only 配置、高倾角基线。场景包含输入/取消/覆盖/回滚、旧新请求选项等价、来源映射、Qt 共享副本下串行与 1/2/4 worker 结果等价、Legacy 与快速模式回退、扫描单线容差与 P.json、严格纹理点序与会话失效。不得因本轮未运行而标记通过。

<a id="evidence-0819"></a>

每批回滚点是本批开始前的文件/API 映射及独立 diff；保留旧接口使调用方能回退。保护已有工作区改动，不使用破坏性 Git 回退。删除兼容层必须在弃用周期结束、引用清零及构建/测试/GUI/现场条件满足后独立评审与授权，不与迁移混合。

<a id="evidence-0820"></a>

**5. 明确范围与待冻结项**

<a id="evidence-0821"></a>

不实现 40 移动判定、90 干涉检查、100 质量报告、外部刀路算法、机器人执行或便携部署；不提交/push/上传。本方案无新增数据 schema。

<a id="evidence-0822"></a>

后续业务接入需要同事冻结页面形态、DTO/schema 版本与外部项目调用方式；真实数据回放需要完整 Start/End 与标定来源。性能任务按自身基准和 GUI 端到端口径验收，不能由文档整理替代。实现人员先按本方案完成公共与调用批次，业务待冻结项不阻塞当前文档交付。

<a id="source-25"></a>

## docs/modules/90_interferenceplane.md

原始来源：`docs/modules/90_interferenceplane.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0823"></a>

- v0.4 仍保持未实现状态，不建立 target、不添加空成功路径；刀路规划和干涉检查不属于本轮实现。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0688](history_pre_v0.5.md#evidence-0688)、[evidence-0325](history_pre_v0.5.md#evidence-0325)、[evidence-0690](history_pre_v0.5.md#evidence-0690)。

<a id="source-26"></a>

## docs/modules/pointcloudcli.md

原始来源：`docs/modules/pointcloudcli.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0826"></a>

**命令契约**

<a id="evidence-0827"></a>

| 调用 | 行为 | 退出码 |
|---|---|---:|
| `pointcloudcli --help` | 输出用法和支持选项 | `0` |
| `pointcloudcli --version` | 输出 `pointcloudcli <PROJECT_VERSION>` | `0` |
| `pointcloudcli` | 输出缺少命令行选项错误和用法 | `2` |
| `pointcloudcli --unknown` | 输出未知选项错误和用法 | `2` |
| `pointcloudcli inspect ply <file.ply> [--json]` | 检查 PLY Header | `0` 或 `10` |
| `pointcloudcli read ply <file.ply> [--json]` | 读取 PLY 并输出摘要 | `0` 或 `10` |
| `pointcloudcli temp-workpiece run <job.json> [--data-dir <path>] [--allow-overwrite]` | 执行临时工件四件套 | `0` 或阶段错误码 |

<a id="evidence-0828"></a>

业务流程为扫描 JSON/PLY/XML 校验、camera 到 robot_base 转换、第一组三点平面拟合、第二组三点一致性验证、自动 WObj1、ROI/Mask 和四件套原子输出。Job JSON 所有相对路径相对于 Job 文件目录，索引空间为转换后的 `robot_base` 点云。

<a id="evidence-0829"></a>

`inspect ply` 只读取 Header；`read ply` 调用 `pcv::detail::io::readPly`，输出 ASCII、binary little-endian/big-endian 格式、声明/有效点数、非有限 XYZ 数、Bounds 和 intensity 摘要，且不写出新文件。带 `--json` 时成功摘要写入 stdout，PLY/业务阶段失败通过 JSON 写入 stderr；基础用法错误仍输出错误文本和用法。版本号由 CMake `PROJECT_VERSION` 在编译时注入。当前不实现实时相机采集、CLI 拼接、PLY 转换写出、干涉平面、质量报告、安装程序或签名发布；`registration_diagnostic` 仍为独立拼接诊断工具。

<a id="evidence-0830"></a>

`temp-workpiece run` 的运行数据目录按 `--data-dir`、环境变量 `PCV_DATA_DIR`、exe 同级 `data`、`%LOCALAPPDATA%\PointCloudSuite\data` 顺序选择首个可写目录，并通过 `pcv::runtime::configureDataDirectory()` 配置缓存位置。`--data-dir` 对 `inspect ply` 或 `read ply` 属于契约错误（退出码 `2`）。

<a id="evidence-0831"></a>

Release 便携目录可由 `scripts\build_windows.ps1 -CliOnly -Config Release -Deploy` 生成；部署目录包含 `pointcloudcli.exe`、Qt/VC 运行库、`data` 和 `README.txt`，并通过清空 PATH 的子进程回归 15 项 CLI 契约。目标目录必须在源码树外且不能是盘符根目录；已有目录会在安全检查后清空重建。`PCV_BUILD_CLI` 默认仍为 `OFF`。

<a id="evidence-0832"></a>

**测试**

<a id="evidence-0833"></a>

`PCV_BUILD_CLI=ON` 且 `PCV_BUILD_TESTS=ON` 时注册 15 项参数、读取和业务测试。读取测试覆盖 ASCII、binary little-endian/big-endian Header、缺少文件、无效 Header、完整读取摘要、Bounds 和 intensity；业务测试覆盖四件套生成、ROI 模式、覆盖保护和路径穿越拒绝。PLY/业务阶段失败通过稳定 JSON 和退出码契约返回，基础参数错误按用法契约输出。

<a id="evidence-0834"></a>

- 新增独立 `pointcloudcli` target 和 `PCV_BUILD_CLI` 开关；默认关闭，GUI target 和 UI 源码未修改。
- 建立 `--help`、`--version`、空参数和未知参数的稳定命令行契约及 CTest 覆盖。
- 增加 Job JSON v1、临时工件业务编排、公共 WObj1/ROI/Mask 几何接口和取消处理。

<a id="evidence-0835"></a>

**2026-09-13**

<a id="evidence-0836"></a>

- 新增 `inspect ply`、`read ply`、`--data-dir` 可写目录回退和 15 项 CLI CTest；补充 `-CliOnly` Release 便携部署。

已合并的重复证据：[evidence-0587](history_pre_v0.5.md#evidence-0587)、[evidence-0753](history_pre_v0.5.md#evidence-0753)。

<a id="source-27"></a>

## docs/pointcloudview_ply_line_handoff_2026-09-20.txt

原始来源：`docs/pointcloudview_ply_line_handoff_2026-09-20.txt`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0839"></a>

PointCloudView PLY 线路交接文档
================================================================

<a id="evidence-0840"></a>

文档性质：纯文本交接记录（无 Markdown 语法依赖，可直接用记事本查看）
日期：2026-09-20
主模块：60_geometry_workpiece
协作模块：50_coordinate_conversion、10_pointcloud_io
UI 编排：apps/pointcloudview
本轮状态：代码级与测试代码级实现完成；仅执行静态检查
重要：本轮未执行任何构建、CTest 或 GUI 启动；未执行任何 Git 命令

<a id="evidence-0841"></a>

1. 任务与范围
----------------------------------------------------------------

<a id="evidence-0842"></a>

目标：优化 pointcloudview 的 PLY 处理线路，使其遵循现有“扫描 JSON 临时工件”
线路的处理方式，并把两组三点之后的所有步骤自动化。

<a id="evidence-0843"></a>

已完成的产品决策（按要求固定，不再变更）：
  - 直接改造现有“打开 PLY”入口，不新增独立 PLY 一键入口。
  - 打开 PLY 后必须进入手眼转换与临时工件流程。
  - Start/End 位姿通过界面手工录入。
  - 位姿字段顺序 [X, Y, Z, A, B, C]，A = Rx、B = Ry、C = Rz，
    旋转顺序 Rz(C) * Ry(B) * Rx(A)。
  - 输入 PLY 数据布局固定为 LineProfileXz，不提供 FullXyz 切换。
  - 每次打开 PLY 都要求用户选择手眼标定 XML。
  - 输出目录由用户选择，选择器初始目录定位到输入 PLY 所在目录。
  - 默认禁止覆盖已有四件套。
  - 保留第二组三点同面校验；通过后自动完成全部后续步骤。
  - 现有 JSON 入口、公共 JSON schema、四件套文件名与格式保持兼容。
  - 不实现第二套手眼转换、平面拟合、ROI 或输出算法。

<a id="evidence-0844"></a>

2. 交付结果（PLY 线路流程）
----------------------------------------------------------------

<a id="evidence-0845"></a>

用户可见流程：
  选择 PLY
    -> “PLY 线路参数”对话框（手眼 XML、Start/End 各 6 个数值、输出目录、
       只读 LineProfileXz、坐标方向 camera -> robot_base、手眼矩阵校验状态）
    -> 校验通过后读取 PLY 并执行手眼转换到 robot_base
    -> 发布 robot_base 点云到画布，自动切到平面提取页并进入第一组三点选点
    -> 人工选择 P1、P2、P3 后自动执行第一组平面拟合
    -> 自动进入第二组三点选点（同一平面）
    -> 第二组同面校验通过后自动完成：建立 WObj1 -> 生成/确认工件坐标系 ->
       提取最终平面与辅助点 -> 边缘分割 -> 生成 ROI indices -> 生成 plane mask
       -> 构造 TempWorkpieceFinalizeOptions -> finalizeTempWorkpiece()
       -> ArtifactTransaction 一次性提交四件套 -> 发布成功状态与四个输出路径
    -> 显示处理诊断（输入点数/转换点数/rejectedInvalid/rejectedRange/
       inputYMinimum/inputYMaximum/signedTravel/dominantTravelAxis）与失败阶段

<a id="evidence-0846"></a>

唯一人工步骤：两组三点（每组 P1、P2、P3）。原有的“确定平面/边缘分割/输出”
按钮保留，作为失败后的人工兜底与 JSON 线路的原有入口，不再要求逐步点击。

<a id="evidence-0847"></a>

“扫描文件夹 PLY”分支保持只读预览，行为未变。

<a id="evidence-0848"></a>

3. 修改文件清单（共 13 个，全部为修改，无新增、无删除）
----------------------------------------------------------------

<a id="evidence-0849"></a>

模块 60（主模块）
  1) modules/60_geometry_workpiece/include/pcv/workpiece/temp_workpiece_interface.h
  2) modules/60_geometry_workpiece/src/temp_workpiece_interface.cpp

<a id="evidence-0850"></a>

应用层
  3) apps/pointcloudview/mainwindow.h
  4) apps/pointcloudview/mainwindow.cpp

<a id="evidence-0851"></a>

测试代码
  5) tests/unit/interface/temp_workpiece_interface_tests.cpp
  6) tests/unit/registration/handeye_transform_tests.cpp
  7) tests/unit/planefitting/plane_fitting_tests.cpp

<a id="evidence-0852"></a>

文档
  8) target.md
  9) AGENTS.md（仅末尾追加“10. 变更记录”，未改写既有规则）
 10) docs/architecture/module_ui_integration_v0.4.md（新增第 9 节）
 11) docs/requirements/pointcloudview_v0.3.md（变更记录新增 2026-09-20 条）
 12) docs/modules/50_coordinate_conversion.md（新增 2026-09-20 变更记录）
 13) docs/modules/60_plane_fitting.md（新增 2026-09-20 变更记录）

<a id="evidence-0853"></a>

未改动（明确说明）：
  - modules/30_pointcloud_stitch 及其他模块 30 相关实现：未改动。
  - 所有 CMakeLists.txt：未改动（未新增 target、未新增源文件）。
  - apps/pointcloudview/ui/mainwindow.ui：未改动；未生成或提交 ui_*.h。
  - 兼容 target 与 forwarding header：未改动、未清理。
  - 四件套文件名、格式与错误码：未改动。

<a id="evidence-0854"></a>

4. 关键行为与 API 变更
----------------------------------------------------------------

<a id="evidence-0855"></a>

4.1 模块 60 公共头（仅追加字段，向后兼容，未删除或改名）
  - TempWorkpieceOptions::useInMemoryScan
      为 true 时不读取 temp_scanning_info.json，改用内存扫描描述符。
  - TempWorkpieceOptions::scanInfo（TempScanningInfo）
      PLY 线路的扫描描述符：scan_id、created_at、LineProfileXz、
      coordinate_frame = camera、calibration camera -> robot_base、
      Start/End 位姿、PLY 与 XML 绝对路径。
  - TempWorkpiecePreparation::inputYMinimum、inputYMaximum、signedTravel、
    dominantTravelAxis
      透传 pointcloud::transformLineScanToRobotBase() 的行程诊断。
  - TempWorkpiecePreparation::cancelled
      标记 worker 因取消回调停止；错误码保持兼容不变，调用方不得把取消
      发布为成功会话。

<a id="evidence-0856"></a>

4.2 模块 60 行为
  - prepareTempWorkpiece() 的“读 PLY -> 加载手眼标定 -> 坐标转换”尾段合并为
    一个共享实现，JSON 与 PLY 入口使用同一算法、同一阈值、同一错误码。
  - 转换选项显式固定：layout = LineProfileXz、interpolateRotation = true、
    sampleStride = 1（其余沿用 JSON 线路默认值）。
  - 取消回调同时透传给 readPlyCached() 的 PlyReadOptions::isCancelled。
  - PLY 线路要求输出目录为非空绝对路径，缺失时报 PCV_CONTRACT_001；
    输入路径必须是绝对路径（无 JSON 目录可解析相对路径）。
  - PLY 线路的 interfaceDirectory 等于输出目录，scanningInfoPath 为空。
  - useInMemoryScan = false 时，JSON 入口行为与改造前完全一致。

<a id="evidence-0857"></a>

4.3 应用层行为
  - 新增工作流状态语义（私有枚举，不对外暴露）：
    Idle、ReadingPly、WaitingForParameters、Transforming、
    SelectingPrimaryPlanePoints、ValidatingPrimaryPlane、
    SelectingSecondaryPlanePoints、ValidatingSecondaryPlane、
    FinalizingArtifacts、Completed、Failed、Cancelled。
    （准备 worker 一个阶段内完成 ReadingPly 与 Transforming。）
  - 新增重入门禁：worker 运行期间以及 WaitingForParameters、ValidatingPrimaryPlane、
    ValidatingSecondaryPlane、FinalizingArtifacts 等自动阶段，禁止重复打开
    PLY 或 JSON；交互选点阶段允许重开数据源（旧会话随之失效）。
  - 新增 pointcloud::CancellationToken 取消回调：closeEvent 与析构置位，
    取消结果不发布成功，也不产生任何输出。
  - 画布 revision 变化（重新加载、去噪、恢复原始点云等）即清除 PLY 会话与
    临时工件会话；磁盘上既有正式输出不会被删除或改动。
  - 参数对话框内置校验（OK 按钮仅在全部满足时可用）：PLY 存在、XML 存在且可解析、
    矩阵为有效刚体变换、12 个位姿有限、输出目录非空、非盘符根目录、
    非构建/缓存/源码树目录、无四件套同名冲突。
  - 每次 XML 选择或变更立即执行 loadHandEyeCalibration() 与
    validateRigidTransform()，界面显示标定绝对路径、有效性、方向与无效原因。
  - 取消参数对话框不改变当前画布、当前点云与已有临时工件会话。
  - PLY 线路固定 allowOverwrite = false，且不在自动执行中途询问覆盖；
    JSON 入口保留原有覆盖确认对话框。
  - 失败诊断按阶段显示：PLY 读取与手眼转换、边缘分割、ROI/Mask 生成、
    四件套事务提交；同时显示模块返回的错误码与诊断。
  - “打开 PLY”右侧数值控件范围：X/Y/Z 约 ±1.0e7 mm（4 位小数），
    A/B/C 约 ±3.6e4 deg（6 位小数），满足生产位姿小数范围。

<a id="evidence-0858"></a>

4.4 复用的既有接口（未新增算法）
  - 坐标转换：pointcloud::loadHandEyeCalibration()、validateRigidTransform()、
    transformLineScanToRobotBase()（模块 50 为唯一数学实现）。
  - 几何与临时工件：pcv::planefitting::fit() / validateConsistency()、
    selectAutomaticWorkpieceAxisPoints()、
    buildWorkpieceCoordinateSystemFromThreePoints()、segmentPlaneEdges()、
    rasterizePlaneEdgeMask()、finalizeTempWorkpiece()（模块 60）。
  - 正式输出：pcv::io::ArtifactTransaction（模块 10）。
  - PLY 读取：pcv::detail::io::readPlyCached()（ASCII、binary little-endian、
    binary big-endian、按属性名读取、点序与 source index 语义均保留）。

<a id="evidence-0859"></a>

5. 公共契约状态
----------------------------------------------------------------

<a id="evidence-0860"></a>

  - JSON 输入 schema：未变化。parseTempScanningInfo() 与
    sr2026-temp-scanning-info-mvp-2 读取逻辑未改动。
  - JSON 输出 schema：未变化。仍为 sr2026-temp-workpiece-info-mvp-2，
    字段名（schema_version、kind、created_at、plane、image、roi、outputs）、
    精度规则（WObj1 六元组保留 3 位小数）与内容结构未变化。
  - 未创建任何 PLY 专用 JSON schema。
  - 四件套文件名：未变化
      baseline_robot_base.ply
      roi_template_robot_base.ply
      plane_mask.png
      temp_workpiece_info.json
  - 四件套格式：未变化
      PLY 为 binary little-endian；
      PNG 为 QImage::Format_Grayscale8，像素只取 0 或 255；
      四个文件通过同一个 ArtifactTransaction 成套提交，任一失败整体回滚。
  - 错误码：未新增、未修改（PCV_INPUT_001、PCV_INPUT_002、PCV_TRANSFORM_001、
    PCV_TRANSFORM_002、PCV_PLANE_001、PCV_FRAME_001、PCV_IMAGE_001、
    PCV_OUTPUT_001、PCV_OUTPUT_002、PCV_CONTRACT_001、PCV_CANCELLED）。
  - 坐标约定：未变化（robot_base、[X,Y,Z,A,B,C]、A=Rx、B=Ry、C=Rz、
    Rz(C)*Ry(B)*Rx(A)、长度 mm、角度 deg）。

<a id="evidence-0861"></a>

6. 静态验证记录（本轮实际执行）
----------------------------------------------------------------

<a id="evidence-0862"></a>

本轮仅执行不触发项目构建的静态检查，结果如下：

<a id="evidence-0863"></a>

  (1) 阅读 AGENTS.md、docs/architecture/module_ui_integration_v0.4.md、
      docs/requirements/pointcloudview_v0.3.md、
      docs/modules/50_coordinate_conversion.md、
      docs/modules/60_plane_fitting.md、target.md。
      结果：完成，作为实现约束依据。

<a id="evidence-0864"></a>

  (2) rg 检索 transformLineScanToRobotBase / loadHandEyeCalibration /
      validateRigidTransform 的全部调用点。
      结果：坐标转换仅模块 50 一处实现；调用方为模块 30、模块 60
      （roi_matching、temp_workpiece_interface）、应用参数对话框与测试，
      无第二套实现。

<a id="evidence-0865"></a>

  (3) rg 检索四件套文件名与 schema 常量。
      结果：文件名、kTempWorkpieceSchema、ArtifactTransaction 提交点均未变化。

<a id="evidence-0866"></a>

  (4) 检查 JSON 入口是否仍走共享流程。
      结果：openTempScanningInfo() 仍以 scanningInfoPath 调用
      prepareTempWorkpiece()；finalizeTempWorkpiece() 仍为唯一提交入口。

<a id="evidence-0867"></a>

  (5) 新增符号一致性检索（workflowStateName、workflowBlocksNewSource、
      setWorkflowState、startPlyLinePreparation、reportPlyLinePreparation、
      appendTempWorkpieceOutput、collectPlyLineParameters、
      plyLineOutputDirectoryIssue、existingWorkpieceArtifacts、
      workpieceArtifactNames、normalizedAbsolutePath、poseIsFinite、
      PlyLineParameters）。
      结果：声明、定义与调用一致，全部落在 mainwindow.h/.cpp 与模块 60。

<a id="evidence-0868"></a>

  (6) 语法层面的括号与引号平衡扫描（5 个 C++ 文件）。
      结果：花括号/圆括号计数全部平衡；双引号奇偶检查命中项均为
      原始字符串字面量与转义引号，属正常用法。

<a id="evidence-0869"></a>

  (7) include 完整性人工核对。
      结果：已补 <QDateTime>、<QGridLayout>、<atomic>、
      <pcv/core/cancellation.h>、<pcv/registration/handeye_transform.h>；
      测试补 <limits>。

<a id="evidence-0870"></a>

  (8) 工作线程约束检查：检查 9 处 QtConcurrent::run 的 lambda 体。
      结果：worker 只调用模块函数、按值捕获，内部无 QWidget/OpenGL/this 访问，
      QWidget 与 OpenGL 更新仅发生在 GUI 线程槽函数中。

<a id="evidence-0871"></a>

  (9) 源码树污染检查：列出本次会话修改的文件（排除 build/ 与 .git/）。
      结果：仅第 3 节列出的 13 个文件，无新增文件，无缓存、日志或输出产物
      写入源码树。

<a id="evidence-0872"></a>

 (10) ui_*.h 检查。
      结果：未生成新的 ui_*.h（build/ 目录内既有文件为历史构建产物）；
      未改动任何 .ui 文件。

<a id="evidence-0873"></a>

 (11) 文档同步检查。
      结果：target.md（新增第九节与状态表行）、AGENTS.md（末尾追加变更记录）、
      docs/architecture/module_ui_integration_v0.4.md（新增第 9 节）、
      docs/requirements/pointcloudview_v0.3.md（新增 2026-09-20 变更记录）、
      docs/modules/50_coordinate_conversion.md 与
      docs/modules/60_plane_fitting.md（各新增 2026-09-20 变更记录）均已更新。

<a id="evidence-0874"></a>

本轮完成的测试代码修改（仅编写，未运行）：
  - tests/unit/interface/temp_workpiece_interface_tests.cpp
      PLY 线路：合法内存描述符；缺失/非法 XML；非刚体矩阵；Start == End；
      空 PLY；全部点被拒；非有限位姿；相对路径；取消（不产生会话与输出）；
      四件套文件名、binary little-endian 编码、Grayscale8 且仅 0/255、
      JSON schema 与 created_at；已存在文件拒绝覆盖（allowOverwrite=false）；
      取消 finalize 无正式输出；输出目录为文件时整体失败；
      成功目录内无 staging 残留。
  - tests/unit/registration/handeye_transform_tests.cpp
      XML 不完整、非刚体矩阵、Start == End、非有限位姿、全部点被拒绝、
      空点云、取消回调、行程诊断（signedTravel、dominantTravelAxis、
      inputYMinimum、inputYMaximum）。
  - tests/unit/planefitting/plane_fitting_tests.cpp
      第二组三点同面通过、复用点、共线、越界、非有限、距离超阈值、
      夹角超阈值、取消不通过；均使用既有 validateConsistency 与既有阈值。

<a id="evidence-0875"></a>

7. 未执行的验证（明确列出，不得视为已通过）
----------------------------------------------------------------

<a id="evidence-0876"></a>

本轮未执行以下命令与动作：

<a id="evidence-0877"></a>

  - 任何 Git 命令：git status、git diff、git add、git commit、git checkout、
    git reset 等全部未执行（按任务限制）。
  - 任何构建：
      .\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir>
          -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
      .\scripts\build_windows.ps1 -Config Release -BuildTests -QtDir <QtDir>
          -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
      cmake -S . -B <BuildDir> ...
      cmake --build <BuildDir> --config Debug
  - 任何测试：
      ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
      ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
      以及 handeye_transform_tests、plane_fitting_tests、
      temp_workpiece_interface_tests、plane_output_tests、
      pointcloud_canvas_contract_tests、roi_matching_tests。
  - 任何 GUI 启动与人工操作（未运行 pointcloudview.exe，未打开任何对话框）。
  - 真实数据验收：未使用真实线扫 PLY、真实手眼标定 XML 或生产夹具。

<a id="evidence-0878"></a>

因此：本轮不得声称任何测试通过；文档中所有“已实现”均为代码级结论。

<a id="evidence-0879"></a>

8. 风险与未验证项
----------------------------------------------------------------

<a id="evidence-0880"></a>

  - Debug/Release 构建未执行，可能存在编译错误（例如新增 Qt 头依赖或
    Qt 版本差异导致的 connect 重载不匹配），首次构建需修正。
  - 全量 CTest 未执行，回归结论未知。
  - GUI 人工流程未验证：对话框布局与可用性、两组三点交互、诊断与状态文案、
    四件套路径展示均未在真实界面确认。
  - 真实线扫 PLY、手眼 XML 与生产夹具未验证；Start/End 插值方向、标定方向
    （camera -> robot_base）与 WObj1 朝向仍需真实样件验收。
  - 新增测试断言中，个别用例依赖既有算法细节（例如空 PLY 归为
    PCV_INPUT_002、严格平面夹具的 fit 成功），首次运行时可能需要按实际行为微调。
  - 行为收紧点（已在文档记录）：
      * 自动阶段与 worker 运行期间禁止重开数据源；
      * PLY 线路固定禁止覆盖已有四件套（不再弹出覆盖确认）；
      * 输出目录新增启发式拒绝：路径分量命中 build*、cmake-build*、.pcv*、
        cache、.cache、CMakeFiles、runtime_data，或目录含 CMakeCache.txt，
        或目录同时含 CMakeLists.txt 与 modules/apps 子目录。
        该启发式可能拒绝名为 build* 的正常业务目录，此时改选其他目录即可。
  - 工作区原有未提交修改未被覆盖、清理或回退；本次只改动第 3 节的 13 个文件，
    全部为逐块最小编辑。由于禁止 Git 操作，未做 git status/git diff 级确认。

<a id="evidence-0881"></a>

9. 接手建议（建议执行顺序）
----------------------------------------------------------------

<a id="evidence-0882"></a>

  1) 先跑一次 Debug 构建（源码树外构建目录），修正可能的编译问题：
       .\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir>
           -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
  2) 跑目标模块测试：
       ctest --test-dir <DebugBuildDir> -C Debug -R "handeye_transform_tests|
           plane_fitting_tests|temp_workpiece_interface_tests" --output-on-failure
  3) 跑全量回归：
       ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
  4) GUI 人工验收（重点）：
       - 打开单个 PLY -> 参数对话框：验证 XML 校验状态文案、12 个位姿输入、
         输出目录初始位置（输入 PLY 所在目录）、同名冲突阻止、取消不改画布；
       - 第一组三点 -> 是否自动拟合；失败后是否能撤销并重选；
       - 第二组三点 -> 同面通过后是否不再需要点击任何按钮即生成四件套；
       - 用同一目录重复执行 -> 是否在参数阶段被拒绝；
       - 用真实 JSON 入口回归 -> 原有行为是否不变。
  5) 用真实样件比对四件套内容（WObj1 六元组、ROI 点数、Mask 与图像参数），
     确认 Start/End 插值方向与手眼方向正确后再标记验收。

<a id="evidence-0883"></a>

10. 回滚说明
----------------------------------------------------------------

<a id="evidence-0884"></a>

  - 本轮所有改动集中在第 3 节列出的 13 个文件，未使用破坏性命令，可按文件回退。
  - 模块 60 的新增字段为纯追加；即使应用层代码回退，模块 60 也可单独保留
    （JSON 入口行为不受影响）。
  - 未生成任何构建产物、缓存或运行数据，回滚不涉及清理目录。
  - 未改动兼容 target、forwarding header、CMake 与 .ui 文件。

<a id="evidence-0885"></a>

11. 相关文档索引
----------------------------------------------------------------

<a id="evidence-0886"></a>

  - 顶层目标与本次迭代状态：target.md（第七节状态表、第九节 PLY 线路迭代记录）
  - 模块与 UI 协同真相源：docs/architecture/module_ui_integration_v0.4.md
    （第 9 节 2026-09-20 PLY 线路接入记录）
  - 数据契约基线：docs/requirements/pointcloudview_v0.3.md（第 8 节变更记录）
  - 坐标转换模块：docs/modules/50_coordinate_conversion.md
  - 工件几何与临时工件模块：docs/modules/60_plane_fitting.md
  - Agent 工作手册与变更记录：AGENTS.md（第 10 节）
  - 本文件：docs/pointcloudview_ply_line_handoff_2026-09-20.txt

<a id="evidence-0887"></a>

（文档结束）

<a id="source-28"></a>

## docs/development/build-windows.md

原始来源：`docs/development/build-windows.md`（整理前工作区文本）；正文描述当次状态，当前细节可能已替代。

<a id="evidence-0888"></a>

**环境要求**

<a id="evidence-0889"></a>

- Windows 10/11；
- MSVC x64，支持 C++17；
- CMake 3.19+；
- Qt 6.5+，包含 Core、Gui、Widgets、Concurrent、OpenGL 和 OpenGLWidgets；
- 源码、构建、Qt、输入和输出路径支持 Unicode；推荐用含中文目录的构建目录执行一次验证。

<a id="evidence-0890"></a>

**推荐命令**

<a id="evidence-0891"></a>

在 Developer PowerShell 中执行：

<a id="evidence-0892"></a>

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

<a id="evidence-0893"></a>

也可以使用：

<a id="evidence-0894"></a>

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

<a id="evidence-0895"></a>

**Qt Creator 直接运行 View 和 Stitch**

<a id="evidence-0896"></a>

根 `CMakeLists.txt` 固定接入 `pointcloudview` 与 `pointcloudstitch`，因此
Qt Creator 完成一次 CMake 重新配置后，项目树会显示 `apps/` 和两个可执行
target；可在项目栏右键构建/运行，或从右上角运行目标列表选择它们。

<a id="evidence-0897"></a>

旧的 `build\\Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 配置曾把两个 target
缓存为 `OFF`，不能仅靠重新编译恢复。请在 Qt Creator 中关闭项目、移除该旧
构建配置或创建新的源码树外构建目录，再打开根 `CMakeLists.txt`，选择
`Desktop Qt 6.8.3 MSVC2022 64bit` Kit 并执行 **CMake Reconfigure**。构建时
选择 `NMake Makefiles`，不要使用 `NMake Makefiles JOM`。

<a id="evidence-0898"></a>

启动自测可使用：

<a id="evidence-0899"></a>

```powershell

# pointcloudstitch 的运行参数
--selftest-close


# pointcloudview 的运行环境变量
POINTCLOUDVIEW_SELFTEST_CLOSE=1
```

<a id="evidence-0900"></a>

**CLI 骨架构建**

<a id="evidence-0901"></a>

`pointcloudcli` 默认关闭，打开 `PCV_BUILD_CLI` 后可与两个桌面应用在同一构建目录中编译。CLI target 仅依赖 Core/Gui/Concurrent 和业务模块，不链接 Widgets、OpenGL、PointCloudCanvas 或 GUI application target。当前入口包括 `inspect ply`、`read ply` 和 `temp-workpiece run <job.json>`。

<a id="evidence-0902"></a>

```powershell
cmake -S . -B <CliBuildDir> `
  -DPCV_BUILD_CLI=ON `
  -DPCV_BUILD_TESTS=ON
cmake --build <CliBuildDir> --config Debug --target pointcloudcli
ctest --test-dir <CliBuildDir> -C Debug -R "cli_" --output-on-failure
```

<a id="evidence-0903"></a>

保持默认 `PCV_BUILD_CLI=OFF` 时，Qt Creator 的 View/Stitch 运行目标不受影响。

<a id="evidence-0904"></a>

CLI-only 配置不会查找 Qt Widgets/OpenGL。`inspect ply` 只检查 Header，`read ply` 读取完整点云并输出摘要；两个命令默认不写文件。若使用 Job JSON 执行业务命令，成功摘要写入 stdout，阶段进度和错误 JSON 写入 stderr。退出码 `0/2/10/20/30/40/130` 分别表示成功、契约、输入、坐标转换、平面/WObj1/ROI、输出和取消。

<a id="evidence-0905"></a>

脚本会检查 Qt、CMake 和 MSVC 环境。构建输出、缓存、日志和导出物应放在源码树之外。

<a id="evidence-0906"></a>

**一键 CLI 构建与便携部署**

<a id="evidence-0907"></a>

`scripts/build_windows.ps1` 在 `-CliOnly` 下会自动探测 CMake、Visual Studio/MSVC 和 Qt；仍可通过 `-CMakePath`、`-QtDir` 覆盖探测结果。`-CliOnly` 与 `-ExperimentalCamera` 互斥，`-Deploy` 必须配合 `-CliOnly` 且只支持 Release：

<a id="evidence-0908"></a>

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

<a id="evidence-0909"></a>

部署步骤会调用同一 Qt 版本的 `windeployqt --release --compiler-runtime`，补齐可用的 x64 VC Runtime，检查 `pointcloudcli.exe` 及 Qt DLL 的导入依赖，创建 `data` 目录并运行清空 PATH 的 15 项 CLI 契约测试。目标目录必须位于源码树外且不能是盘符根目录；现有目录会在安全检查通过后清空重建。也可运行 `scripts\build_pointcloudcli_release.bat` 使用默认 Release 部署目录。

<a id="evidence-0910"></a>

CLI 运行数据目录按 `--data-dir`、`PCV_DATA_DIR`、exe 同级 `data`、`%LOCALAPPDATA%\PointCloudSuite\data` 顺序选择可写目录。只有 `temp-workpiece run` 接受 `--data-dir`；`inspect ply` 和 `read ply` 携带该选项会返回 `PCV_CONTRACT_001` 和退出码 `2`。

<a id="evidence-0911"></a>

**生成器与头文件依赖跟踪**

<a id="evidence-0912"></a>

推荐使用 `NMake Makefiles`，因为项目通过 CMake 自身扫描器记录 MSVC 头文件依赖。`Ninja` 依赖 MSVC `/showIncludes` 前缀，在本地化 MSVC 环境可能生成无头文件依赖的数据库；`NMake Makefiles JOM` 会生成空的 `.obj.d` 文件，脚本会拒绝该生成器。

<a id="evidence-0913"></a>

- `NMake Makefiles` 使用 CMake 扫描器；`Ninja` 仍通过 `/showIncludes` 生成依赖，脚本会在构建后检查依赖数据库；
- 在中文 MSVC 上该前缀被以错误编码保存，可在 `<BuildDir>/CMakeFiles/<ver>/CMakeCXXCompiler.cmake` 中看到乱码值（例如 `CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢:  "`），过滤永远不命中：所有 `*.obj.d` 为 **0 字节**，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`；
- 后果：**修改公共头不会触发依赖方重新编译**。按不同结构体布局编译出的新旧目标文件被链接进同一个库，运行期表现为栈破坏、崩溃或随机行为；而单元测试可能因为可执行文件是在头文件变更之后新建的、自身布局自洽而全部通过。

<a id="evidence-0914"></a>

因此：

<a id="evidence-0915"></a>

- 项目不再设置 `CMAKE_DEPENDS_USE_COMPILER OFF`；配置阶段通过 `VSLANG=1033` 固定 MSVC `/showIncludes` 语言，构建阶段由 `CMAKE_CXX_COMPILER_LAUNCHER` 和 `scripts/build_windows.ps1` 继续保持一致；
- `scripts/build_windows.ps1` 构建完成后会检查依赖数据库中是否真的记录了头文件，未记录则以非零码退出；
- MSVC 配置若 ABI 探测失败或指针宽度不是 8 字节会直接终止；脚本会拒绝 `NMake Makefiles JOM` 和依赖数据库失效的生成器；
- 根 CMake 的 `PCV_BUILD_EXPERIMENTAL_CAMERA` 默认值为 `ON`；`scripts/build_windows.ps1` 默认传入 `OFF`，需要实验相机时显式使用 `-ExperimentalCamera`。该模块不属于 v0.3 发布基线；
- 命令行强制全量重建：

<a id="evidence-0916"></a>

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Clean -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
```

<a id="evidence-0917"></a>

**修改公共头（尤其是新增结构体字段）后，必须使用能跟踪依赖的构建树，或执行 `-Clean` / Qt Creator 的“重新构建项目”。旧构建目录需要重新配置一次以应用新的 `VSLANG` 和依赖护栏。**

<a id="evidence-0918"></a>

**v0.4 模块构建边界**

<a id="evidence-0919"></a>

规范 target 为 `pcv_m10_pointcloud_io`、`pcv_m20_pointcloud_render`、`pcv_m30_pointcloud_stitch`、`pcv_m40_pointcloud_matching`（兼容 `INTERFACE`）、`pcv_m50_coordinate_conversion` 和 `pcv_m60_geometry_workpiece`。旧 target 只做转发。构建目录必须位于源码树外且可写；UI 同事只构建其 `features/**`、UI 资源和 adapter 工作区，不直接链接模块 `src/`。

<a id="evidence-0920"></a>

本轮文档同步按用户要求暂不运行 CTest。下列命令仍是后续验收命令，不代表本轮已执行：

<a id="evidence-0921"></a>

```powershell
cmake --build <DebugBuildDir> --config Debug
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
```

<a id="evidence-0922"></a>

**本次文档整理的验证范围（2026-10-09）**

<a id="evidence-0923"></a>

本轮按用户要求只做静态检查，不执行本文构建/CTest/GUI 命令。本文保留命令供后续独立验收使用；历史测试计数不能代表当前所有配置。近期新增的 pose-overlap-fine、平面等价提速和纹理实验测试是否启用以 tests/CMakeLists.txt 与构建选项为准，不固定当前总测试数。

<a id="evidence-0924"></a>

后续开发批次和回滚点见 [module_convergence_v0.4.md](../development/development_plan_v0.5.md)，实际代码变更和验证需同步 [target.md](../../target.md)。

<a id="evidence-0925"></a>

**验收原则**

<a id="evidence-0926"></a>

修改公共头、CMake、UI、processor 或输出契约后，必须重新配置并构建受影响应用，必要时执行 Debug/Release 全量 CTest。GUI 人工验收和真实生产夹具验收必须单独记录。
