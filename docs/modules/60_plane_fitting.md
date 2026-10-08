# 60_geometry_workpiece：工件几何

状态：已实现，兼容入口保留。规范实现位于 `modules/60_geometry_workpiece/{include,src}`，模块 target 为 `pcv_m60_geometry_workpiece`；`pcv_m60_plane_fitting`、`pcv_m60_planefitting` 和 `pcv_m70_roi_template` 仅转发。职责包括平面拟合与诊断、平面/边缘几何、ROI、WObj、临时工件流程和扫描路径规划；正式文件通过 10 的事务 API 提交。应用入口仅负责参数转换、生命周期和结果映射。本文件文件名保留旧名称以避免历史链接失效。

接口/测试：`pcv::geometry`、`pcv::workpiece`、`pcv::scan`，以及兼容的 `pcv::planefitting::fit`；`plane_fitting_tests`、`roi_matching_tests`、`scan_planner_tests`、`temp_workpiece_interface_tests` 和几何回归测试。

最后核对日期：2026-10-08。纯算法实现不得依赖 `apps/` 或 Qt Widgets。本文件历史正文中的旧名称与旧职责为当时状态，仅用于追溯。

## 扫描路径规划（2026-09-30 修订）

`pcv::scan::planScanTrajectory()` 使用 `ScanPlanningOptions` 生成机器人法兰/TCP 参考位姿 `[X,Y,Z,A,B,C]`。扫描轴固定为 WObj1：横向覆盖沿 `X`，行程沿 `Y`；扫描线按 `-X → +X` 排列，产品方向固定为 `+Y → -Y`，不采用蛇形路径。

中心和范围规则：

- `Global` 使用当前拟合平面 `globalPlaneIndices`，`LocalRoi` 使用当前 ROI `localRoiIndices`；两者均拒绝空、越界和非有限点，LocalRoi 规划阶段不重新拟合平面，局部失败不回退全局。
- 有效点投影到 WObj1 XY 后取包围盒中心、X 宽度和 Y 行程长度；中心沿 WObj1 `+Z` 偏移统一输入的 `scanHeightMm`。
- P 点的 `ABC` 直接取 WObj1 的 `poseA/poseB/poseC`；端点固定为 ROI `vMax + travelExtensionMm` 和 `vMin - travelExtensionMm`，结果同时返回原始和最终行程。

扫描带规则使用 `stepTarget = cameraWidthMm - overlapMm`。目标宽度 `<= cameraWidthMm + 0.1 mm` 时只生成 `P0→P1`；超过固定单线容差后按 `ceil((requiredWidthMm-cameraWidthMm)/stepTarget)+1` 计算带数，并在首尾有效覆盖边界之间均匀布置，实际重叠不小于请求值。默认 `cameraWidthMm=100`、`overlapMm=50`、`scanHeightMm=50`、`travelExtensionMm=50`。每条线生成两个连续编号点（`P0→P1`、`P2→P3`），带间移动不属于扫描线段。单线时 `actualOverlapMm` 沿用请求值，不代表存在相邻带；内部 JSON 的 `single_line_width_tolerance_mm` 记录固定容差，原始目标宽度不截断。

输入校验包括有效 WObj1、有限 WObj1 姿态、有效平面/ROI、投影范围、`cameraWidthMm > 0`、`scanHeightMm >= 0` 和取消回调。结果返回结构化错误码、最终中心、自动范围、轴/方向、请求与实际重叠、间距、P 点和独立段；失败或取消不返回成功轨迹。

`scanTrajectoryToJson()` 输出 `scan_trajectory_v1`，使用 `snake_case`，核心字段为 `mode`、`center_pose`、`required_area`、原始/最终行程、扫描带参数、方向、`points` 和 `segments`。`scanTrajectoryToInoRobotJson()` 输出 `sr2026-p-json-mvp-2`：`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`，点号从 10 开始且最多 9990 点，姿态重排为 `[X,Y,Z,RZ,RY,RX]`，XYZ 转为 WObj1 局部坐标，不生成控制器写入调用。`pointcloudview` 第五步“扫描规划”默认局部 ROI，后台调用模块 API、显示内部 `P0=[...]` 与导出 `P[10]...` 的编号区别，并通过模块 10 `ArtifactTransaction` 原子导出项目 JSON 或 `P.json`；画布、平面、WObj1、ROI 或参数变化会取消/清除旧结果。

## 变更记录

### 2026-10-08（扫描规划单线容差修复）

- 根因：扫描线数量原先使用 `requiredWidth > cameraWidthMm` 的严格比较，ROI/WObj1 投影和浮点误差可能使本应覆盖一条线的区域略超 `cameraWidthMm`，误生成第二条扫描线。
- 修复：模块内固定 `kSingleLineWidthToleranceMm=0.1 mm`；`requiredWidthMm <= cameraWidthMm + 0.1 mm` 时只生成一条线和 `P0/P1`，只有超过该阈值才按实际覆盖规则生成多条线。
- 结果 DTO 和内部 `scan_trajectory_v1` JSON 增加 `single_line_width_tolerance_mm` 诊断字段；InoRobot `P.json` schema、点号和 pose 顺序保持不变。
- 测试覆盖 `100.099 mm`、`100.1 mm` 单线边界和 `100.1001 mm` 多线边界；真实相机有效视野与现场 ROI 边界仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`（含 `scan_planner_tests`）；双配置 `pointcloudview` 自关闭启动冒烟退出码均为 `0`，`git diff --check` 通过。未使用触发问题的真实 ROI 做 GUI 复验；Release 部署仍缺本机 Visual Studio release redistributable。

### 2026-09-30（扫描规划验收）

- `scan_planner_tests` 补齐实际重叠下限、Y 两端固定延长、倾斜 WObj1 的 P.json 局部 XYZ、无效坐标轴、退化投影范围和 `P[10]...P[9999]` 边界断言。
- Debug/Release 全量 CTest 均 `16/16`；扫描定向测试双配置通过；`pointcloudview` Debug/Release 自关闭启动冒烟均退出码 0；`git diff --check` 通过。
- GUI 人工流程未验证：本机 computer-use connector 初始化失败（`unsupported Codex auth method: apikey`）；真实 ROI、相机视野标定、机器人可达性和安全性仍须现场验收。

### 2026-09-22（扫描路径规划修订与 UI 接入）

- 将未发布的旧扫描语义修订为固定 WObj1 X/Y 轴、毫米重叠距离、人工目标尺寸和两种显式 Y 行程方向；删除从全局平面范围推导、自动选轴、固定 50% 重叠和 `marginMm` 语义。
- 全局中心直接采用人工 `XYZABC`；局部中心采用 ROI 三维质心投影与 `+Z` 高度，独立执行并继承界面 `ABC`。
- `pointcloudview` 增加独立扫描规划页、后台取消/revision 门禁、逐行 P 点显示、覆盖确认和事务 JSON 导出。
- 单元测试覆盖单线/多线、均匀间距、编号与方向、倾斜 WObj1、局部中心、非法输入、取消和 JSON；Debug 定向测试已通过，完整 Debug/Release 结果见本轮最终验收记录。
- 风险：未验证真实机器人可达性、相机实际有效宽度、现场安全距离、碰撞和干涉。

### 2026-09-21（临时工件自动链失败恢复）

- 应用层统一管理 `None/Json/Ply` 临时工件会话；参数取消、旧会话替换和画布版本变化不会复用过期 source index 或覆盖策略。
- 自动 WObj1、边缘分割、ROI/Mask 和四件套前置失败统一进入 `Failed`，保留已完成点云/几何结果并允许重试；主动放弃取点保留会话和转换点云，重新取点即可继续。
- 取消错误码按发生阶段保持不变，`cancelled=true` 为取消权威标志：PLY 读取为 `PCV_INPUT_002`，转换为 `PCV_TRANSFORM_001`，调用前或 finalize 取消为 `PCV_CANCELLED`；文档不再声称所有取消均为 `PCV_TRANSFORM_001`。
- 验证：源码树外 `scan-ui-debug-20260922` / `scan-ui-release-20260922` 构建成功，Debug/Release CTest 均为 `16/16` 通过；启动冒烟通过。真实样件与完整 GUI 交互仍待验证。

### 2026-09-20（PLY 临时工件线路：内存扫描描述符与共享转换尾段）

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

### 2026-09-21（代码验收记录）

- PLY/JSON 自动链失败恢复、取消语义和会话替换规则已完成代码验收；Debug/Release 全量构建成功，CTest 均为 `15/15`。
- WObj1 朝向、ROI/Mask 实际内容和四件套现场结果仍需真实样件与生产夹具确认。

### 2026-09-06（模块重命名）

- 目录重命名：`modules/60_plane_fitting`，target 改为 `pcv_m60_plane_fitting`；旧 `pcv_m60_planefitting` 保留为兼容 INTERFACE，转发到新 target。职责（平面拟合与几何诊断）不变。

### 2026-08-31

- 建立模块边界和 `pcv_m60_planefitting` 兼容入口；保留处理器实现。
- M5.2 首轮：新增独立 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` API；应用入口暂继续调用完整 legacy 算法以保留连通域和 PCA 诊断，待字段完整迁移后再切换。
- M5.2 第二轮：迁移 `calculateBoundsCenter` 及越界/非有限点校验，应用入口通过兼容包装调用模块实现。
- M5.2 契约补齐：`Options` 和 `Result` 已覆盖连通域、RANSAC/PCA 迭代、随机种子、预览延迟分类及断开区域诊断字段；完整算法仍待从 processor 迁移，当前新增字段不改变旧入口行为。
- M5.2 算法迁移完成：`pcv::planefitting::fit` 实现 PCA 初始模型、RANSAC、PCA 重分类、预览延迟分类和连通域筛选；`extractPlaneFromPoints` 改为兼容 forwarding wrapper。新增模块实现与应用级几何回归均通过。
- M5.4：删除 `extractPlaneFromPointsLegacy()`，应用入口继续保留兼容参数转换；处理器不再包含第二套平面拟合算法。

### 2026-10-06（离线亮度图纹理映射）

- 新增 `pcv::texture::mapPlaneBrightness()`，复用现有拟合平面和 `planeIndices`，并通过单个 PLY 的原始 `sourceIndices` 直接读取同次扫描亮度像素；不改变平面拟合、PLY 或正式输出契约。
- WObj1 存在时使用局部 X/Y；缺少 WObj1 时将拟合平面法向规范化，并从最不平行的世界轴投影生成稳定局部 X/Y。使用全部有效平面点局部包围盒做覆盖范围，宽高比不一致时中心裁剪，最近邻采样。
- 图像统一转换为 `QImage::Format_Grayscale8`，忽略 alpha，不做自动对比度增强；只覆盖现有平面点，越界、无效和无法采样点跳过并计数，取消不发布结果。
- `pointcloudview` 通过后台任务选择亮度图并显示纹理统计；当前只显示每点灰度，不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- 验证：`texture_mapping_tests` 已覆盖 WObj1/固定轴回退、方向、裁剪、无效点、取消和空平面；Debug/Release 全量 CTest 均 `17/17`。
- 风险：真实样本 GUI 视觉方向、中心裁剪和大点云性能仍需人工验收。

### 2026-10-06（亮度图严格栅格对应）

- 亮度纹理不再使用平面局部坐标包围盒投影。单个 PLY 临时工件必须保留原始 `sourceIndices` 和 `declaredPointCount`，图像像素数必须完全匹配原始 PLY 点数。
- 固定采用左上角起始 row-major：`pixelX = sourceIndex % image.width`、`pixelY = sourceIndex / image.width`；仅当前 `planeIndices` 对应点读取亮度，其他点不使用图像像素。
- RGB/JPG/PNG 只转换为 `Format_Grayscale8`，不裁剪、缩放、翻转、配准或增强；任意过滤点、点数变化、缺失/重复/越界 source index 都拒绝整次映射。
- WObj1 不参与亮度采样；平面或点云变化使纹理失效，WObj1 变化不使纹理失效。
- 验证：`texture_mapping_tests`、`pointcloud_canvas_contract_tests` 在 Debug/Release 均通过；源码树外 Debug/Release 全量 CTest 均 `17/17`，两配置 `pointcloudview` 自关闭启动冒烟通过，`git diff --check` 通过。
- 风险：真实 PLY 与同次扫描亮度图的人工 GUI 视觉验收、row-major 点序现场一致性和实时取流接入仍未完成。

### 2026-10-08（PLY 原始坐标纹理实验）

- `pointcloudview` 增加独立“PLY 纹理实验（原始坐标）”来源，直接保留单个 PLY 的 XYZ、原始点序、声明点数和 identity source index；不经过手眼转换，不建立 WObj1，不输出临时工件四件套。
- 实验平面复用两组三点和同面校验，第一组三点使用 `useZAxisResidual=false` 的点到平面垂直距离，支持任意朝向；第二组三点通过后才能选择同次扫描亮度图。
- 亮度图继续使用严格 `source_raster_index`：像素数等于 PLY 声明点数，左上角起始 row-major，只给 `planeIndices` 对应点着色；失败或取消不发布结果。
- `texture_experiment_tests` 覆盖声明点数/source index、垂直平面、同面校验、像素映射、取消和尺寸错误拒绝；真实样本视觉方向和现场点序仍待验收。

### 2026-10-08（三点平面拟合等价提速 v2）

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。
