# PointCloudView 顶层目标

> 文档状态：目标与追加式变更记录。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 2026-10-08：PointCloudStitch 位姿＋重叠精配准迭代

- 用户确认优先观察全量拼接结果与提速：位姿粗配准、重叠区两层小幅 6DoF 精配准、质量仅警告、失败回退位姿继续正式输出；每对 10 mm/1°，累计不限，seam 默认关闭，无预览。
- 实现位于模块 30，UI 默认新模式，公共 API 默认 Legacy；复用 50 和 10，不修改手眼或原始点云，不提交/推送。矩阵按原相邻样本求解、帧序累计、失败锚点重置；Qt worker const 访问和来源映射保持。
- 报告区分 `fine_applied/pose_fallback` 与严格质量状态，旧验收未执行不写成通过。正式输出仍用 stitched 文件名，GUI 覆盖前确认，事务成套提交。
- 测试登记：倾斜纯平面/小旋转、无重叠和超预算、链重置、非有限、取消、串并行1/2/4等价、无预览、事务覆盖保护。旧 coarse 夹具要求恢复4 mm却继承2 mm默认预算，本轮使其平移预算显式化，不修改算法质量阈值。
- 验证与风险以本轮最终记录为准；真实 PLY/XML 存在但 Start/End 位姿和历史报告缺失，不能宣称现场速度/双平面问题已验收。

## 当前实现与目标区分（2026-10-09）

全剖/点剖闭环是共同目标，高点标注/外部刀路/结果贴合接入仍待 schema 与调用方式冻结。仓库已有的是点云/几何/临时工件、相机扫描轨迹和严格原始栅格灰度显示，不能把流程图全部步骤写为完成。

当前扫描保留 Global/LocalRoi（UI 默认 LocalRoi），50 是统一手眼数学来源，30 API 默认 Legacy/UI 默认 PoseOverlapFine，40 移动判定仍未实现。下方“删除全局扫描”等带日期记录是历史过程，后续规则已替代；保留正文供追溯。

正式状态：[v0.4 基线](docs/requirements/pointcloudview_v0.4.md)；所有权：[协同文档](docs/architecture/module_ui_integration_v0.4.md)；后续代码批次：[开发方案](docs/development/module_convergence_v0.4.md)。所有代码更新都要在本文件追加日期、owner、文件/API、行为、实际验证、未验证项与回滚点。本次仅文档整理，不改变代码或测试结论。


版本：v1.0  
状态：目标基线  
责任人：陈振佳、于海军  
最后更新：2026-09-18

## 一、北极星目标

将 `pointcloudview` 建设为点云处理、局部标注、刀路规划结果接入和用户展示的统一入口，形成从输入数据到可视化业务结果的完整软件闭环。

本目标以 `pointcloudview` 主流程为中心，覆盖必要的点云读取、渲染、点选、几何处理、坐标转换、结果输出和外部项目接入能力。陈振佳与于海军分别负责后端和前端/外部项目，并共同完成跨边界联调和端到端验收。

## 二、目标业务闭环

### 1. 全剖流程

```text
.ply 文件
  → 点云处理程序
  → png 文件
  → 全剖刀路规划程序
  → 全剖路径 PNG
  → 贴合到纹理点云并由 pointcloudview 展示
```

### 2. 点剖流程

```text
.jpg 文件
  → 高点标注程序
  → high_point_overlay.png
  → 点剖刀路规划程序
  → 点剖路径 PNG
  → 贴合到纹理点云并由 pointcloudview 展示
```

两条流程均应输出坐标 JSON，用于描述结果位置、坐标关系和后续接入所需的元数据。`high_point_overlay.png` 是点剖流程中的中间输入和可视化产物；路径 PNG 是最终贴合到对应纹理点云的业务结果。

最终用户应能够在 `pointcloudview` 中查看点云、纹理、overlay、路径和相关坐标结果，并能够识别处理成功、取消、失败和输入不合规等状态。

## 三、共同交付边界

### 陈振佳：后端负责人

- 负责点云处理算法、共享模块及其稳定公共接口。
- 负责版本化 DTO/API、PLY/PNG/JSON 数据契约和 `ArtifactTransaction` 事务输出。
- 负责错误码、诊断信息、取消语义、失败回滚和结果发布边界。
- 负责构建配置、自动化测试、后端回归和对前端接入所需的稳定能力支持。
- 不直接承担前端页面和用户交互实现。

### 于海军：前端与外部项目负责人

- 负责 `pointcloudview` 前端页面、交互流程和用户展示体验。
- 负责高点标注程序、全剖/点剖刀路规划项目，以及这些项目与 `pointcloudview` 的接入实现。
- 负责将后端输出转换为清晰可操作的界面状态、结果预览和用户提示。

### 双方共同负责

- 确认跨项目输入、输出和版本兼容关系。
- 完成接口联调、固定样例和契约内现场数据回归。
- 共同处理跨边界缺陷、变更影响和最终端到端演示验收。

## 四、接口与数据原则

- 继续沿用现有 PointCloudView 数据契约：binary little-endian PLY、Grayscale8 PNG、`snake_case` JSON、`robot_base` 坐标约定和 `ArtifactTransaction` 成套提交语义。
- 新增字段、产物或外部项目接口必须版本化，不破坏既有契约；具体 schema 以对应模块公共头、模块文档和数据契约为准。
- 后端通过稳定 DTO/API、结果状态、错误码、取消标记和诊断信息向前端及外部项目提供能力；前端不得直接依赖共享模块内部实现或 `modules/*/src`。
- 外部项目的最终接入形态暂不冻结。CLI、库、进程或其他方式作为后续共同决策项；在形态确定前，先保持版本化文件契约和 adapter 边界。
- 取消、失败或输入不合规时，不得发布部分结果、复用旧正式输出或伪造成功状态。

## 五、完成定义与验收门槛

本目标完成的最低条件是：

- 满足已发布输入、标定、质量和规模约束的现场数据，能够完成全剖或点剖对应流程。
- 点云处理、高点标注、刀路规划、坐标 JSON、路径 PNG、纹理点云和统一展示之间的调用链能够端到端运行。
- 在相同输入、参数和契约条件下，结果可重复，关键产物和诊断可追溯。
- 输入校验、取消、处理失败、外部项目失败和事务回滚均有可见、可定位的状态反馈。
- 不满足契约的数据必须被明确拒绝并给出诊断，不以“成功但结果不可信”的方式继续发布。
- 双方完成接口联调、回归验证和一次可复现的端到端业务演示。

真实设备、机器人、现场生产部署、长期稳定性和性能扩展属于后续验收门槛；本目标首先保证契约内现场数据的软件闭环。

## 六、范围边界

本目标默认不新增以下独立交付范围：

- `pointcloudstitch`、`pointcloudcli` 和实验相机能力，除非后续确认它们是本闭环的必要依赖。
- `90_interferenceplane` 和 `100_qualityreport`，当前仍按未实现模块处理。
- 未经双方确认的刀路 JSON schema、外部项目部署方式或机器人执行/坐标修正下发能力。

现有 `AGENTS.md`、`docs/architecture/module_ui_integration_v0.4.md`、`docs/requirements/pointcloudview_v0.3.md`、源码、CMake 和测试是实现约束与事实来源；本文件不替代这些文档，也不把流程图中的目标步骤表述为已经完成的功能。

## 七、轻量状态记录

| 日期 | 状态 | 说明 |
|---|---|---|
| 2026-09-18 | 目标基线已建立 | 已确认两条业务流程、双方职责、统一展示入口、契约内现场数据验收口径；外部项目接入形态待共同决策。 |
| 2026-09-20 | PLY 线路已实现（代码级） | 现有“打开 PLY”入口改造为手眼转换 + 临时工件线路，两组三点后自动生成四件套；未执行构建、CTest 与 GUI 验收，详见第九节。 |

### 2026-09-22（删除全局扫描，仅保留局部 ROI）

- 主模块 `60_geometry_workpiece`，UI 编排 `apps/pointcloudview`；删除 `ScanPlanningMode`、全局中心和人工 ABC 输入，扫描 API 始终使用当前 ROI。
- ROI 三维质心投影到 WObj1 XY 后沿 `+Z` 偏移生成中心 XYZ，P 点 ABC 直接取 WObj1 `poseA/poseB/poseC`；JSON 固定 `mode=local_roi`，不再提供全局分支或全局回退。
- 更新扫描单元测试、模块/架构/需求文档和 UI 参数转换；保留后台取消、revision 门禁、P 点显示和 `ArtifactTransaction` 导出。
- 本轮验证待执行；风险仍包括真实机器人可达性、相机有效宽度、安全距离、碰撞/干涉和真实 ROI GUI 流程。

## 八、2.5D 高程图配准迭代记录

### 2026-09-22（60 扫描路径规划修订与 UI 接入）

- 主模块 `60_geometry_workpiece`，UI 编排 `apps/pointcloudview`，事务输出复用 `10_pointcloud_io`；修订 `pcv::scan::planScanTrajectory()`、`scanTrajectoryToJson()` 及尚未发布的 `scan_trajectory_v1`。
- WObj1 X/Y 固定为横向/行程轴；全局采用人工中心 `XYZABC`，局部采用 ROI 三维质心投影与 `+Z` 高度并继承 ABC；输入人工目标尺寸、相机宽度和毫米重叠距离。扫描线按 `-X → +X` 编号，全部使用选定的同一 Y 方向，输出 `P0→P1`、`P2→P3` 独立线段。
- `pointcloudview` 新增第五步“扫描规划”，后台执行、逐行显示 P 点、revision 变化自动失效，默认导出 `scan_trajectory.json`；覆盖前确认，写入由 `ArtifactTransaction` 原子提交。
- 文件：`modules/60_geometry_workpiece/include/pcv/scan/scan_planner.h`、`modules/60_geometry_workpiece/src/scan_planner.cpp`、`modules/60_geometry_workpiece/CMakeLists.txt`、`apps/pointcloudview/mainwindow.h/.cpp`、`tests/unit/scan/scan_planner_tests.cpp`、`tests/CMakeLists.txt`、模块/架构/需求文档、`AGENTS.md`。
- 验证：源码树外 `scan-ui-debug-20260922` / `scan-ui-release-20260922` 构建成功；Debug/Release 全量 CTest 均 `16/16`，扫描/平面/ROI/事务定向测试通过；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` Debug/Release 启动冒烟通过；`git diff --check` 通过。Release `windeployqt` 提示本机缺少 Visual Studio release redistributable，便携部署未验收。
- 风险：真实相机有效宽度、机器人可达性、现场安全距离、碰撞和干涉未验证；未用真实点云/ROI 完成人工 GUI 全流程（全局/局部/导出/旧结果失效）冒烟。

### 2026-09-30（扫描规划参数与 P.json 契约修订）

- 主模块：`60_geometry_workpiece`；UI：`apps/pointcloudview`；事务输出：`10_pointcloud_io::ArtifactTransaction`。
- 保留 `Global`/`LocalRoi`，UI 默认 LocalRoi；LocalRoi 直接使用当前 ROI 索引，不重新拟合。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、两端 `travelExtensionMm=50`，固定 `-X→+X` 线序和 `+Y→-Y` 行程，首尾覆盖贴合 ROI X 边界。
- 增加原始/最终 Y 行程、延长距离、请求/实际重叠、实际间距、线数结果字段；限制 P.json 最多 9990 个点，来源字段为 `temp_workpiece_info.json`，点号从 `P[10]` 连续递增，姿态为 `[X,Y,Z,RZ,RY,RX]`，不调用控制器 API。
- UI 删除方向下拉框，增加重叠输入和固定延长提示，显示内部 P0/P1 与 P.json P[10]/P[11] 区别；点云、WObj1、ROI 或参数 revision 变化立即取消并清空旧结果，导出继续使用 ArtifactTransaction 和覆盖确认。
- 文件：`modules/60_geometry_workpiece/include/pcv/scan/scan_planner.h`、`modules/60_geometry_workpiece/src/scan_planner.cpp`、`apps/pointcloudview/mainwindow.h`、`apps/pointcloudview/mainwindow.cpp`、`tests/unit/scan/scan_planner_tests.cpp`、相关模块/架构/需求文档与 `AGENTS.md`。
- 验证：本轮 `scan_planner_tests` 通过；完整构建、全量 CTest、启动冒烟和 GUI 手工验证以最终汇报的实际结果为准。机器人可达性、碰撞/干涉、控制器导入和真实工件 GUI 全流程仍需现场验证。

### 2026-09-30（扫描规划最终验证）

- 补充 `scan_planner_tests`：固定 Y 延长和最终行程、实际重叠下限、倾斜 WObj1 P.json 局部坐标、无效 WObj1/退化投影、P[10] 到 P[9999] 边界及无 `wobj` 字段。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`；双配置定向扫描测试通过；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟双配置退出码均为 `0`；`git diff --check` 通过。
- GUI 人工验证未完成：computer-use connector 报 `unsupported Codex auth method: apikey`；真实 ROI、相机视野、机器人可达性、安全距离、碰撞/干涉和控制器导入仍需现场验证。

### 2026-09-22（全局与局部扫描路径规划统一）

- 主模块：`60_geometry_workpiece`；UI 编排：`apps/pointcloudview`；输出复用 `10_pointcloud_io::ArtifactTransaction`。
- 修改：恢复 `Global`/`LocalRoi` 双模式；两种模式均从平面或 ROI 有效点的 WObj1 XY 投影包围盒计算中心、X 宽度和 Y 行程，沿 WObj1 `+Z` 使用统一 `scanHeightMm`，固定 50% 带间重叠并输出同向 `P0→P1...`。
- 接口/文件：修订 `scan_planner.h/.cpp`、双 JSON 序列化（项目 `scan_trajectory_v1` 与 InoRobot `sr2026-p-json-mvp-2`）、`mainwindow.h/.cpp` 双模式 UI 与两个原子导出按钮；扫描测试改为覆盖全局/局部、倾斜 WObj1、方向、编号、错误和 JSON 契约。
- 验证：源码树外 Debug 扫描测试构建与 `scan_planner_tests` 通过；`pointcloudview` Debug 目标编译通过；`git diff --check`、Release 全量 CTest、GUI 全流程本轮尚未完成。
- 风险：未验证真实相机有效宽度、机器人可达性、控制器写入、现场安全距离、碰撞和干涉；现有无关 `30_pointcloud_stitch` 修改保留。

补充验证：完成源码树外 Debug/Release `pointcloudview` 目标构建、双配置全量 CTest（均 `16/16`）、双配置扫描规划定向测试和 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟；扫描测试另补充 WObj1 ABC 继承、空 ROI 拒绝和 InoRobot 局部 XYZ 转换断言。真实点云 GUI 全流程及机器人现场验收仍未完成。

| 日期 | 主模块 | 变更与验证 | 风险/未完成 |
|---|---|---|---|
| 2026-09-18 | `30_pointcloud_stitch`（同步 `40_pointcloud_matching`） | 新增默认关闭的 2.5D 高程图路径、受限 coarse-to-fine 搜索、有效掩膜/去趋势/网格诊断、legacy 回退与 `visibility_hard_rejected`；应用 JSON/GUI 状态透传。Debug 全量 CTest 15/15，Debug/Release 定向测试各 2/2。源码树外真实回放：`1/test_1` 2.5D 退化后回退 `legacy_icp` 并生成正式 PLY；`5_40/test` 可见比例 `0.2246`，来源 `none`，硬拒绝且不生成正式合并 PLY。 | 未实现 pose graph、硬件时间戳、`Δt`、40 的工件移动判定；GUI 人工冒烟、PageHeap、现场夹具和便携 Release 部署未完成。Release 环境未找到 Visual Studio redist 子目录。 |

重大目标、职责、数据契约或验收口径发生变化时，应在本节追加日期和简短说明，并同步更新受影响的架构、模块或接口文档。

## 九、PLY 临时工件线路迭代记录

主模块：`60_geometry_workpiece`；协作模块：`50_coordinate_conversion`、`10_pointcloud_io`；UI 编排：`apps/pointcloudview`。

| 日期 | 主模块 | 变更与验证 | 风险/未完成 |
|---|---|---|---|
| 2026-09-20 | `60_geometry_workpiece`（协作 `50_coordinate_conversion`、`10_pointcloud_io`、`apps/pointcloudview`） | 现有“打开 PLY”入口改造为 PLY 临时工件线路：PLY 线路参数对话框（手眼 XML、Start/End 各 6 个位姿、输出目录、只读 LineProfileXz、手眼矩阵校验状态）→ 按 LineProfileXz 转换到 `robot_base` → 人工第一组三点后自动平面拟合 → 人工第二组三点同面校验 → 自动建立 WObj1、边缘分割、ROI、Mask → 由 `ArtifactTransaction` 一次性提交四件套。模块 60 新增内存扫描描述符路径并合并共享转换尾段，坐标数学完全复用模块 50，正式输出仍只经模块 10。 | 未执行 Debug/Release 构建、全量 CTest 与 GUI 人工验收；真实线扫 PLY、手眼 XML 和生产夹具未验证；Start/End 插值方向、标定方向与 WObj1 朝向仍需真实样件确认。 |
| 2026-09-21 | `apps/pointcloudview`（PLY/JSON 临时工件状态机） | 修复参数对话框取消后状态锁死、自动链同步失败悬空、旧会话串线和 JSON 位姿摘要顺序；新增 `None/Json/Ply` 单一会话来源、替换确认、统一会话失效和自动失败出口；文件夹扫描、多帧配准等新画布入口也遵循确认后失效旧临时工件会话的规则。源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过。 | GUI smoke、真实线扫 PLY/XML 与生产夹具仍待执行。 |

新增行为要点：

- “打开单个 PLY”进入 PLY 线路；文件夹扫描 PLY 仍是只读预览入口，行为不变。
- 每次打开 PLY 都要求重新选择手眼标定 XML，并在选择/变更时立即执行 `loadHandEyeCalibration()` 与 `validateRigidTransform()`。
- 输出目录在流程开始前校验：非空、非盘符根目录、非构建/缓存/源码树目录、且不存在四件套同名冲突；PLY 线路固定 `allowOverwrite = false`，不在自动执行中途询问覆盖。
- 两组三点是 PLY 线路唯一的人工步骤；第一组三点选满后自动拟合平面并自动进入第二组，第二组校验通过后自动完成剩余步骤。
- 现有扫描 JSON 入口、公共 JSON schema、四件套文件名与输出格式保持兼容。

本轮交接记录（纯文本、含未验证项与接手建议）：`docs/pointcloudview_ply_line_handoff_2026-09-20.txt`。

### 2026-09-21（代码验收与发布准备）

- 验收范围：PLY/JSON 会话状态机、手眼转换成功/取消语义、PLY 读取取消透传、两组三点及自动 WObj1/边缘/ROI/Mask/四件套失败出口、JSON 位姿摘要顺序。
- 自动化结果：源码树外 Debug/Release 构建成功；Debug 和 Release 全量 CTest 均为 `15/15`；`git diff --check` 通过。
- 发布边界：本次仅提交上述 PLY/JSON 排错修复、相关测试和 Markdown 文档；工作区既有 `30_pointcloud_stitch`、PointCloudStitch、临时脚本和其他未提交修改不纳入本次提交。
- 未完成验收：GUI smoke、真实线扫 PLY/手眼 XML、机器人运动方向、WObj1 朝向和生产夹具现场数据仍需人工确认。

## 十、手眼标定统一与点云倒转修复迭代记录（2026-09-24）

主模块：`50_coordinate_conversion`；调用方清理：`apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`；复用入口核对：`apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`、`apps/pointcloudstitch` UI 与回归。

- **黄金基准**：扫描 JSON 路径（`prepareTempWorkpiece()` → `transformLineScanToRobotBase()`）是手眼标定黄金基准；离线 JSON、PLY、多帧和拼接路径必须与它逐点一致。
- **根因**：点云上下/法向反转风险来自“重复实现 + LineProfileXz 语义不一致”，不是矩阵方向问题：回归工具自带 XML 解析与 `poseMatrix()`，view 应用自带刚体校验、SLERP 插值、进度轴/`EmbeddedMotion` 启发式和内联 `baseFromFlange * flangeFromDepth`，旧合并路径还把输入 Y 当相机坐标保留。
- **修复**：模块 50 成为唯一数学实现（`composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus` 状态码）；两个调用方的重复解析、重复插值和内联矩阵乘法删除并改调模块 50。未使用矩阵求逆、Z 翻转或镜像作为修复手段。
- **契约**：`RTmatDepth2robot` = `T_flange_depth`；坐标链 `p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`；位姿 `[X,Y,Z,A,B,C]`、`A=Rx/B=Ry/C=Rz`、`Rz(C)*Ry(B)*Rx(A)`、mm/deg、PLY/PNG/JSON 格式与既有错误码均未变化。
- **测试**：`handeye_transform_tests` 覆盖 12 项契约（乘法方向、黄金链路、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、正负行程与反向扫描、FullXyz、法向同旋转不平移、位姿往返、非法矩阵拒绝、无行程/空点云/全无效/超范围/取消、状态码）；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口的 robot_base 逐点一致性断言。
- **验证**：源码树外 Debug/Release 构建成功；Debug/Release 全量 CTest 均 `16/16`；`handeye_transform_tests`、`temp_workpiece_interface_tests`、`stitching_interface_tests`、`multiframe_registration_tests` 定向运行 `4/4`；`pointcloudview` Debug/Release 自关闭启动冒烟均 `ExitCode=0`。真实样本共声明 `6,300,000` 点、有效转换 `4,389,670` 点、无进度越界点；JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因本机缺少 Visual Studio release redistributable 发出警告，未完成便携部署验收。
- **风险/未验证**：真实手眼 XML 的物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一、真实机器人安全/可达性/碰撞与干涉均未验证；本轮所有结论只来自源码树外构建、单元/接口回归和启动冒烟。

### 2026-10-06（离线 PLY + 亮度图纹理点云）

- 主模块：`60_geometry_workpiece`；渲染模块：`20_pointcloud_render`；UI 编排：`apps/pointcloudview`。
- 新增 `pcv::texture::mapPlaneBrightness()`：读取同次扫描 PNG/JPG/BMP，转换 `QImage::Format_Grayscale8`，按原始 PLY `sourceIndex` 的左上角 row-major 像素直接采样；只为现有 `planeIndices` 点生成灰度/有效标记，严格校验原始点数、source index 和过滤统计，不执行局部坐标投影或裁剪。
- `PointCloudCanvas` 新增 per-point 颜色覆盖与“纹理灰度”模式；有效纹理点使用灰度，非平面点/无效点回退当前颜色模式；点云、平面和坐标系更新会清理旧纹理缓存，切换其他颜色模式后可复用缓存。
- `pointcloudview` 增加“选择亮度图并贴到平面”入口，后台映射、取消/revision 门禁、替换保护和状态栏统计；不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- 文件：`modules/60_geometry_workpiece/include/pcv/texture/texture_mapping.h`、`modules/60_geometry_workpiece/src/texture_mapping.cpp`、`tests/unit/texture/texture_mapping_tests.cpp`、渲染契约/画布、`apps/pointcloudview/mainwindow.*`、对应 CMake/UI 和文档。
- 验证：源码树外 `C:\pcv-build\texture-full-debug` Debug 构建成功，CTest `17/17` 通过；`C:\pcv-build\texture-full-release` Release 构建成功，CTest `17/17` 通过；纹理映射和渲染契约定向测试均通过。
- 风险/未完成：真实 PLY + 亮度图 GUI 方向、中心裁剪视觉效果、无 WObj1 回退轴和大点云性能未人工验收；Release `windeployqt` 提示缺少本机 Visual Studio release redistributable；未接入实时相机取流。

### 2026-10-06（严格 PLY 栅格索引亮度纹理修正）

- 将亮度纹理从局部坐标覆盖整张图改为严格 `sourceIndex -> image pixel` 映射。仅支持单个 PLY 临时工件流程；亮度图必须与原始 PLY 同次扫描、同尺寸、同 row-major 点序。
- 映射只读取 `planeIndices` 对应的原图像素，使用原始 PLY 点数校验图像像素数，拒绝过滤点、缺失/重复/越界 source index、点数变化和当前点序回退；不再执行局部坐标投影、包围盒覆盖、中心裁剪、缩放、翻转或配准。
- WObj1 不参与纹理采样；WObj1 建立/修改不会清除已有纹理。点云、平面索引或 source provenance 变化仍会清除纹理；边缘结果在平面索引不变时保留纹理。
- 验证：源码树外 `C:\pcv-build\texture-raster-debug` Debug 构建成功，CTest `17/17` 通过；`C:\pcv-build\texture-raster-release` Release 构建成功，CTest `17/17` 通过；两配置 `texture_mapping_tests.exe` 均退出码 `0`；两配置 `pointcloudview.exe` 在 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 下启动冒烟均退出码 `0`；`git diff --check` 通过。
- 统计修正：映射结果的 `skippedPointCount` 按平面点总数减成功映射数报告；严格栅格校验失败时不发布覆盖。
- 风险/未完成：未使用真实 PLY 与同次扫描亮度图进行人工 GUI 视觉验收，无法在本轮确认现场图像与 PLY 的 row-major 起点/点序确实一致；未接入实时相机取流；Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable。

### 2026-10-07（点云预览工作台 UI 简化）

- 主模块：`apps/pointcloudview` UI 编排；不修改模块 10、30、50、60 的算法和公共 API。
- 主窗口删除左侧常驻总控中心、步骤导航和文件列表可见布局，顶部保留“打开点云”“打开扫描 JSON”和“工具”菜单；深色 3D 画布占据主体，文件名、点数和任务状态通过状态栏显示。
- 现有 Designer 控件被迁移到五个独立、无模式且可同时打开的工具窗口：数据源、点云清理、平面与 WObj1、边缘与输出、扫描规划；窗口关闭只隐藏，参数、结果和后台任务仍由 `MainWindow` 保留。渲染设置继续复用 `RenderDialog`，相机和多帧配准保留原业务入口。
- 取点或边缘选择时自动隐藏对应工具窗口，主窗口底部显示状态、计数、撤销、取消和确认/完成操作条；Esc、Backspace 及自动 PLY/扫描 JSON 流程继续使用原状态机和 revision 门禁。
- 验证：`C:\pcv-build\ui-simplify-debug`、`C:\pcv-build\ui-simplify-release` 构建成功；Debug/Release 全量 CTest 均 `17/17`；两配置 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟退出码均为 `0`。
- 风险/未完成：本轮未使用真实点云逐一人工验收多窗口布局、预览占客户区 85% 的像素比例、工具窗口关闭恢复和取点交互；真实相机、机器人可达性、碰撞/干涉及现场数据仍需验收；Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable。

### 2026-10-08（PLY 原始坐标纹理实验入口）

- 增加“PLY 纹理实验（原始坐标）”来源：单个 PLY 直接保留原始 XYZ、点序、`declaredPointCount` 和 identity `sourceIndex`，不读取 XML，不执行手眼转换、Start/End 位姿、WObj1 或临时工件四件套。
- 实验模式复用两组三点和同面校验；第一组三点按任意平面方向的垂直距离拟合，第二组通过后直接选择同次扫描亮度图。
- 复用严格 `source_raster_index` 映射：图像像素数必须等于 PLY 声明点数，固定左上角起始 row-major；仅 `planeIndices` 对应点显示灰度，失败、尺寸不符或取消不发布新纹理。
- 新增 headless `texture_experiment_tests`，覆盖声明点数、identity source index、垂直平面、同面校验、严格像素映射、取消和尺寸错误拒绝。
- 本轮验证：源码树外 Debug 构建成功，Debug 全量 CTest `18/18` 通过。Release 构建、Release CTest、真实 PLY/亮度图 GUI 视觉验收和现场 row-major 一致性仍待完成。

### 2026-10-08（扫描规划单线误生成双线修复）

- 主模块 `60_geometry_workpiece`；修改 `scan_planner.h/.cpp`、`scan_planner_tests.cpp` 和模块/架构/数据契约文档。
- 原严格比较 `requiredWidth > cameraWidthMm` 在 ROI 投影宽度略超过视野时立即增加第二条线；新增固定 `0.1 mm` 容差，宽度不超过 `cameraWidthMm + 0.1 mm` 只输出 `P0/P1`，超过阈值仍按实际覆盖要求规划多条线。
- 原始 `requiredWidthMm` 不截断；结果和内部 JSON 记录 `singleLineWidthToleranceMm` / `single_line_width_tolerance_mm`，不新增 UI 参数，不改变 P.json 的 schema、编号和姿态顺序；单线重叠字段沿用请求值，不表示存在相邻扫描带。
- 测试覆盖 Global/LocalRoi 的 `99.9/100/100.099/100.1/100.1001/101 mm`、单线段数、P.json 点数/编号以及多线首尾覆盖与重叠保证；真实相机有效视野和现场 ROI 边界误差仍需验收。
- 本轮验证：复用源码树外 `C:\pcv-build\texture-experiment-debug-v1` / `C:\pcv-build\texture-experiment-release-v1` 执行完整 Debug/Release 构建成功；Debug 扫描定向测试通过，双配置全量 CTest 均 `18/18`（Debug `82.23 s`、Release `8.36 s`），均含扫描边界与 JSON 回归；`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 双配置启动冒烟退出码 `0`，`git diff --check` 通过。
- 验证边界：未使用触发问题的真实 ROI 进行 GUI 复验；容差最多接受总宽 `0.1 mm` 的视野超出，真实相机有效宽度仍需现场确认。Release `windeployqt` 仍提示缺少本机 Visual Studio release redistributable，便携部署未验收。现有无关修改保留，未提交。

### 2026-10-08（三点平面拟合等价提速 v2）

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。

## 十一、2026-10-09：文档整理与下一阶段开发登记

- 范围：全仓既有 47 个 Markdown 文档（含本文件）核对，新增正式 v0.4 需求基线、10/60 规范模块文档及 v0.4 收敛开发方案；本轮没有代码或 CMake 更新。
- 当前状态：10/20/30/50/60 已有实现，40 仅兼容聚合、移动判定未实现；扫描规划与外部全剖/点剖刀路规划分开；原始坐标纹理仍属实验。生产 namespace/API 与工具 target 迁移尚未收敛。
- 文档治理：当前文档顶部给出规范入口、日期与状态；历史正文、旧目录/target 和历史测试结论保留追溯。新接口只能在实际实现后标记完成。
- 后续顺序：公共边界与兼容映射 → 生产调用方迁移 → 兼容引用审计与独立验收；方案见 [module_convergence_v0.4.md](docs/development/module_convergence_v0.4.md)。本次仅交付方案，不实施代码迁移或删除兼容层。
- 验证：51 份 Markdown 落盘内容核对一致，161 个本地 Markdown 链接目标存在；一级标题、代码围栏和替换字符检查通过，未发现锚点链接；`git diff --check` 通过。不运行构建、CTest 或 GUI。历史记录中的 `15/15`、`16/16`、`17/17`、`18/18` 等不代表本次验证，也不代表当前所有配置的测试数量。
- 工作区：本次仅产生 Markdown 修改及新增，未提交；检查时另见 `.pcv_tmp_build.bat`、`.pcv_tmp_configure.bat`、`.pcv_tmp_ctest.bat`、`.pcv_tmp_run.bat` 删除状态，本次未操作这些文件。
- 风险：真实点云 GUI 全流程、实际控制点与 Start/End、现场 row-major 一致性、手眼物理方向、机器人可达性/碰撞、Release 便携部署和外部刀路 schema/页面形态仍待验收或冻结。整理记录不改变这些结论。
