# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
最后核对日期：2026-09-22
状态：数据契约基线（v0.3）；架构和 UI 交接已进入 v0.4，发布验收待完成

## 1. 目标和事实来源

v0.3 继续作为 JSON、PLY、坐标、错误码和默认数值的数据契约基线；模块职责、目录、target 和 UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准。源码、CMake 和测试优先于历史文档。

当前未实现模块为 `90_interferenceplane` 和 `100_qualityreport`。本版本不为这两个模块建立功能 target，也不报告虚假成功。

## 2. 模块基线

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

迁移期兼容 target：旧模块名 `pcv_m05_camerainput`、`pcv_m10_pointcloudread`、`pcv_m20_pointcloudrender`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，以及 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`（仅转发）。兼容层在所有调用方迁移并完成独立审计前保留。

## 3. 数据流程

### 2026-09-18：2.5D 高程图配准增量契约

- `30_pointcloud_stitch` 新增可选 2.5D 高程图配准路径，`IcpOptions.heightMapRegistrationEnabled` 默认 `false`；关闭时旧 ICP 行为和输出保持不变。
- 新路径只允许受限 `Δx/Δy` 与小范围 `Δz/yaw`，网格分辨率自动估计并限制 `0.5~2.0 mm`，有效掩膜缺失网格不参与评分。`IcpDiagnostics` 和回归/GUI JSON 透传网格、有效比例/点数、评分前后、改善率、受限 ICP 层数、来源和拒绝原因。
- 选择规则固定为：2.5D 全部验收通过选 `heightmap_restricted_icp`；2.5D 失败且旧 ICP 通过选 `legacy_icp`；两者失败选 `none`。最终有效可见比例 `<0.25` 时设置 `visibility_hard_rejected` 并阻止正式输出，不能用命中点内覆盖率绕过。
- 本增量不改变手眼矩阵、机器人位姿语义、PLY 编码、`robot_base` 坐标约定，也不实现 pose graph、硬件时间戳、`Δt` 估计或 40 的工件移动判定。
- 验证记录：Debug 全量 CTest 15/15；Debug/Release 定向 `multiframe_registration_tests` 与 `stitching_interface_tests` 各 2/2。源码树外回放中 `1/test_1` 成功并回退 `legacy_icp`；`5_40/test` 以可见比例 `0.2246` 硬拒绝且无正式合并 PLY。GUI 人工、生产夹具和便携 Release 部署尚未验收。

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

单帧处理：输入校验 → `10` 读取/缓存/预处理 → `20` 渲染和点选 → `50` 坐标转换 → `60` 平面/边缘/ROI/WObj/临时工件 → `10` 事务成套输出。

多帧处理：`30` 是唯一拼接编排：组织输入并读取 PLY，调用 `50` 转换，执行 ICP/多帧配准和 seam fusion，生成 manifest 后由 `10` 提交正式文件。取消或失败时不得发布部分结果或复用旧正式输出。

## 4. 稳定契约

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

### 4.1 扫描路径 JSON 增量契约（2026-09-30）

- schema 为 `scan_trajectory_v1`，`kind=robot_scan_trajectory`，字段使用 `snake_case`。
- 机器人位姿数组固定为 `[X,Y,Z,A,B,C]`，单位分别为 mm/deg。WObj1 X 是横向相机覆盖轴，Y 是扫描行程轴；扫描线按 `-X → +X` 编号并全部同向。全局使用拟合平面有效点，局部使用当前 ROI 有效点。
- `center_pose` 是投影包围盒中心沿 WObj1 `+Z` 偏移 `scanHeightMm` 后的最终中心位姿；`required_area.width_mm`、`required_area.travel_length_mm` 由有效点自动计算，另有 `travel_extension_mm` 与 `final_travel_length_mm`；默认 `camera_width_mm=100`、`requested_overlap_mm=50`，`actual_overlap_mm`、`line_spacing_mm` 和 `scan_line_count` 描述扫描带。
- `travel_direction` 产品固定为 `positive_y_to_negative_y`；每条线起点为 `vMax+50`、终点为 `vMin-50`。`points` 按 P 点顺序保存 `point_id`、`point_name`、`robot_pose`、`scan_line_index`、`line_endpoint`；`segments` 保存每条线的起止 P 点。
- 项目 JSON 使用 `scan_trajectory_v1`；InoRobot 兼容 `P.json` 使用 `sr2026-p-json-mvp-2`、`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`、点号从 10 开始且最多 9990 点，XYZ 转为 WObj1 局部坐标，姿态重排为 `[X,Y,Z,RZ,RY,RX]`。本期不调用控制器写入 API。
- 两种扫描 JSON 都由 `ArtifactTransaction` 原子提交。失败、取消或 revision 过期不得发布旧结果或部分文件，覆盖必须由用户明确确认。

验证记录：2026-09-30 源码树外 Debug/Release 构建成功，`scan_planner_tests` 双配置通过，全量 CTest 双配置均 `16/16`，`pointcloudview` 双配置自关闭启动冒烟退出码 `0`，`git diff --check` 通过。人工 GUI 流程因 computer-use connector 初始化错误（`unsupported Codex auth method: apikey`）未完成；真实点云 ROI 和设备现场行为仍需验收。

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

### 2026-10-08（扫描规划单线容差修复）

- 单线判定改为 `requiredWidthMm <= cameraWidthMm + 0.1 mm`，吸收严格浮点比较造成的单线/双线边界跳变；超过阈值仍按实际宽度保证多带覆盖。
- 内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm`，保留原始范围；单线时 `actual_overlap_mm` 沿用请求值，扫描线数为 1 时没有相邻带的实际重叠。InoRobot `P.json` 字段、编号和姿态顺序保持兼容。
- 回归覆盖两种模式的 `99.9/100/100.099/100.1/100.1001/101 mm`、P 点与段数量及 P.json 点数；真实相机有效视野和现场 ROI 边界仍需设备验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`（含扫描边界回归），双配置应用启动冒烟退出码 `0`，`git diff --check` 通过；真实 ROI GUI 复验及 Release 便携部署未完成。

### 2026-09-24（模块 50 手眼标定统一与点云倒转修复）

- 登记主模块 `50_coordinate_conversion`；调用方清理涉及 `apps/pointcloudstitch/regressionrunner.cpp` 与 `apps/pointcloudview/pointcloudprocessor.cpp`；相关入口 `apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`。
- 根因：坐标转换存在重复实现（回归工具自带的 `RTmatDepth2robot` 解析与 `poseMatrix()`、view 应用自带的刚体校验/SLERP/进度启发式/内联矩阵乘法），且旧合并路径的 LineProfileXz 语义与 JSON 黄金路径不同（把输入 Y 当相机坐标保留、按进度轴与 `EmbeddedMotion` 启发式可选位姿）。点云上下/法向反转风险来自这两点，而不是矩阵方向。
- 修复：模块 50 成为唯一数学实现，新增 `composeBaseFromDepth()`（固定 `T_base_depth = T_base_flange * T_flange_depth`）、`computeScanTravel()`（带符号行程）、`lineProfileCameraPoint()`/`lineProfileScanRatio()`（`[X,0,Z]` 与 `y/signedTravel`）、`RobotCloudStatus` 状态码；两个调用方的重复实现删除，改为调用模块 50。未使用求逆、Z 翻转或镜像作为修复手段。
- 契约影响：位姿顺序 `[X,Y,Z,A,B,C]`、旋转约定、单位、PLY/PNG/JSON 文件格式、四件套文件名和既有错误码均未变化；模块 50 的改动为增量符号，`transformPointToRobotBase()` 仅追加可选 `QString *error` 尾参；`apps/pointcloudview/pointcloudprocessor.cpp` 的合并缓存版本号从 8 提升到 9 以失效旧算法缓存。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`；4 项直接相关测试定向运行 `4/4`；`pointcloudview` Debug/Release 启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio release redistributable 发出警告，便携部署未验收。
- 未验证：真实手眼 XML 的物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一、真实机器人安全/可达性/碰撞与干涉，均未在本轮范围内。

### 2026-09-20（PLY 临时工件线路接入：打开 PLY → robot_base → 四件套）

- 登记主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`（唯一坐标转换实现）、`10_pointcloud_io`（`ArtifactTransaction` 与 PLY 读写）、`apps/pointcloudview`（参数收集、状态编排与结果展示）。
- 数据契约不变：四件套文件名 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`；binary little-endian PLY；Grayscale8 PNG 且像素只取 0/255；JSON 仍为 `sr2026-temp-workpiece-info-mvp-2`，字段名、精度（WObj1 六元组保留 3 位小数）与错误码未变化；未创建新的 PLY 专用 schema。
- 位姿与坐标约定不变：`[X,Y,Z,A,B,C]`，`A=Rx`、`B=Ry`、`C=Rz`，旋转 `Rz(C)*Ry(B)*Rx(A)`，长度 mm、角度 deg；PLY 线路固定按 `LineProfileXz` 解释，不提供 `FullXyz` 切换。
- 新增行为：现有“打开单个 PLY”入口改为 PLY 线路（文件夹扫描 PLY 仍是只读预览）。参数对话框强制选择手眼 XML、手工输入 Start/End，并显示标定绝对路径、有效性、`camera -> robot_base` 方向与失败原因；输出目录在流程开始前校验（非空、非盘符根、非构建/缓存/源码树目录、无四件套同名冲突），PLY 线路固定 `allowOverwrite = false`。第一组三点选满后自动平面拟合并自动进入第二组，第二组同面校验通过后自动建立 WObj1、边缘分割、ROI、Mask 并一次性提交四件套。
- 模块 60 新增 `TempWorkpieceOptions::useInMemoryScan` / `scanInfo` 与 `TempWorkpiecePreparation` 的转换诊断字段；JSON 入口行为、公共头既有字段和 `temp_scanning_info.json` 读取逻辑保持兼容。
- 验证：仅完成代码级与测试代码级修改和静态检查；本轮未执行 Debug/Release 构建、CTest 和 GUI 人工验收，不得把上述功能记为已通过验收。

### 2026-09-21（PLY/JSON 临时工件状态机排错修复）

- 根因：参数对话框取消或 JSON 解析失败会遗留 `WaitingForParameters`；自动链同步前置失败只返回；`m_tempWorkpieceSessionActive` 与 PLY 来源标记分离维护，可能让旧会话和覆盖策略串线；JSON 位姿摘要显示顺序错误。
- 修复：应用层改用 `None/Json/Ply` 单一来源状态，参数确认后才启动新任务；旧会话替换需确认，统一失效不删除磁盘文件；主动放弃取点保留会话，自动链失败统一进入 `Failed` 并允许重试/切换；JSON 显示改为 `[X,Y,Z,Rx,Ry,Rz]`。
- 契约：JSON schema、四件套文件名/格式、坐标转换数学和阶段错误码保持不变，取消由 `cancelled=true` 区分。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke、真实线扫 PLY/XML 和生产夹具未验证。

### 2026-09-21（代码验收与发布边界）

- PLY/JSON 临时工件状态机和底层取消/成功语义完成代码验收：Debug/Release 构建成功，双配置全量 CTest 均为 `15/15`。
- JSON schema、四件套文件名/格式、坐标旋转约定和阶段错误码保持不变。
- GUI smoke、真实线扫 PLY/XML、机器人运动方向和生产夹具验收仍待执行。

### 2026-09-17（PointCloudStitch 高倾角配准修复第一轮：命中口径 + 鲁棒粗对齐 + 提速）

- 登记主模块 `40_pointcloud_matching`，配套 `30_pointcloud_stitch` 选项透传与失败信息细化；未改动坐标链、手眼矩阵、PLY 读写、渲染与平面输出模块。
- 证据基线与根因：现网 `stitching_report.json` 显示失败数据集的结构点 15 mm 命中率只有 0.182 / 0.229 / 0.376 / 0.487，而旧验收把 3 mm 覆盖率除以全部裁剪区结构点（门槛 0.65），在这些数据上任何刚体变换都无法满足；同期命中点内覆盖率为 0.633 / 0.639 / 0.519 / 0.716。另外 `5_40` 的切向粗对齐已找到约 8 mm 的正确候选，却因 5 mm 覆盖率增益只有 1.55% 低于 5% 门槛被丢弃。
- 契约变更（已确认）：默认验收口径改为“命中点分母 + 证据门槛（命中点 ≥ max(minimumStructuralPoints, 10% 裁剪区结构点数)）”，命中率不再作为硬门槛，低于 0.25 只输出警告；失败原因码区分为 `structural_points_insufficient` 与 `structural_coverage_insufficient`；`structuralCoverageOfMatched=false` 可逐位复现旧行为。对外错误码仍为 `PCV_STITCH_001`，fail-closed 语义不放松。
- 配准质量：切向粗对齐目标函数默认改为 trimmed 最近邻目标（最小 K 个距离均值，K=25%），旧覆盖率目标保留可切换；新增可选 trim 加权（默认关，实测在平面主导配对上会拉走已恢复位姿）、预对齐 p90 > 2 mm 时首层对应半径放宽到 4 mm、法向一致性门控与按主结构方向的裁剪框路由（两者均实现但**默认关闭**，见下条回放证据）。
- 提速：ICP 内对应搜索分块并行（块序合并，与串行逐位一致）、步长试探改为不落地的变换计数、relaxed retry 增加证据预判并记录跳过原因。速度指标按确认结论不设硬指标，以“判定一致 + 可测变快”为准。
- 文件：`modules/40_pointcloud_matching/include/pcv/registration/multiframe_registration.h`、`modules/40_pointcloud_matching/src/multiframe_registration.cpp`、`modules/30_pointcloud_stitch/include/pcv/interface/stitching_interface.h`、`modules/30_pointcloud_stitch/src/stitching_interface.cpp`、`tests/unit/registration/multiframe_registration_tests.cpp`、`docs/modules/30_pointcloud_stitch.md`、`docs/modules/40_pointcloud_matching.md`。
- 验证：Debug 构建成功、`ctest -C Debug` 14/14 通过（含 4 组新增用例：命中口径与证据门槛、4 mm 切向偏移下 trimmed/legacy 判定差异、结构裁剪框路由、稠密夹具上 ICP 内并行逐位等价）。
- 真实数据回放结果（Release `registration_diagnostic`，输入为原 PLY 硬链接 + 由报告反解位姿）：`1/test_1` 仍接受，修正与基线差 0.008 mm / 0.0009 度（在 0.01 mm/0.005 度预算内）；`5_40/test` **由拒绝转为接受**（命中率 0.2246、命中点内 3 mm 0.6566、修正 (-3.7708, -2.7613, 1.6065)/0.1154 度，严格档直接通过）；`6_50/test` 仍拒绝（命中率 0.3703、命中点内 3 mm 0.4989），按停止判据不放宽门槛。法向门控与结构路由在 `1` 组上把修正漂移到约 1.8 mm，故两者默认关闭、仅作为显式开关保留。
- 未验证项（不得视为已通过）：`4_30` 与 `3` 两组未回放；提速与基线报告不是同一构建配置，同配置 A/B 未执行；法向门控/结构路由在真实数据上只有“会改变结果”的证据、没有“更好”的证据；Release 全量 CTest 与 GUI 人工冒烟未执行。

### 2026-09-15（PointCloudStitch 间歇 0xc0000005：并行路径 Qt 隐式共享容器并发 detach）

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

### 2026-09-15（PointCloudStitch 陡峭平面配准修正）

- 登记主模块 `40_pointcloud_matching`，配套报告/状态字段位于 `apps/pointcloudstitch`；未修改坐标链、手眼矩阵、原始 PLY、`workpiece_list.json` 或窗口生命周期。
- 主平面拟合保留浅倾角 Z 高度带兼容路径；当该路径不适用时采用 1 mm 采样、固定候选序列的方向无关 RANSAC + PCA 精化。正式上限为相对 robot-base Z 轴 75 deg，超过上限按 `plane_tilt_exceeds_limit` 拒绝。
- 陡峭面使用目标平面的 `u/v/n` 局部重叠框，统一用于重叠计数、三层 ICP、步进保留、宽候选后的重建及结构验收。非平面结构和局部 `u/v` 边界点提供 Point-to-Point 辅助残差，主平面继续使用 Point-to-Plane。陡峭面必须具备两侧至少 100 个非平面结构特征点、`observableDof >= 3`、三层完成以及既有双向 3 mm 覆盖率阈值；纯平面/低特征情形按 `steep_plane_feature_insufficient` fail-closed。
- `stitching_report.json` 记录平面拟合方式、法向/倾角/RMS/内点、局部重叠、可观测性及陡峭面拒绝原因；坐标链异常继续只报警。
- 验证：Debug 构建 `multiframe_registration_tests`、`stitching_interface_tests`、`seam_fusion_tests`、`pointcloudstitch` 成功；新增 60/75 deg 特征面通过、76 deg 限制拒绝、60 deg 纯平面拒绝的回归。全量 CTest、GUI 人工操作及真实 `D:\stitch_test\6_50`/陡峭夹具重放待完成。

### 2026-09-09（亮度图取流验证 GUI）

- `10_pointcloud_read` 增加 `extractIntensityImage()`，并在扫描组帧中同步保留 `CameraFrame::intensityData`。
- 新增实验性独立 target `intensity_viewer`：固定 `Range_Image=7` 和 `MV3D_LP_RegisterImageDataCallBack`，支持实时 `Mono8` 预览及非覆盖 PNG 保存。
- 自动化验证：Debug 构建及 CTest `19/19` 通过；真实设备亮度帧、多批次同步和 GUI 人工操作仍待现场验收。

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

- 新增 `PCV_BUILD_CLI`（默认 `OFF`）和独立 `pointcloudcli` target；开启后可在关闭 `pointcloudview`、`pointcloudstitch` 的配置中单独构建 CLI，CLI-only 路径仅配置 `Qt6::Core`、`Qt6::Gui`、`Qt6::Concurrent` 及 CLI 所需共享模块，不配置 Widgets/OpenGL，既有 GUI target、UI 和默认构建行为不变；
- `pointcloudcli temp-workpiece run <job.json>` 已登记为首期临时工件业务入口：复用扫描校验、坐标转换、三点平面拟合、第二组三点验证、自动 WObj1、ROI/Mask 和四件套原子输出；实时相机采集、拼接、干涉平面和质量报告不在本期范围；
- `pointcloudcli inspect ply <file.ply>` 和 `pointcloudcli read ply <file.ply>` 已接入 10 模块 PLY 读取接口；前者只检查 Header，后者输出完整读取摘要，均不默认写出文件；
- CLI 的早期骨架阶段仅提供 `--help`、`--version`、空参数及未知参数契约；当前实现已在此基础上接入 PLY Header/完整读取摘要和 `temp-workpiece run`，仍不接入 Qt Widgets、OpenGL 或 GUI 事件循环；
- `pointcloudcli` 不链接 `Qt::Widgets`、`Qt::OpenGL`、`PointCloudCanvas` 或 GUI application target；`PCV_BUILD_CLI=ON` 与 `PCV_BUILD_TESTS=ON` 时注册 15 项 CLI CTest，覆盖参数契约、PLY Header/完整读取、三种 PLY 格式、Bounds、临时工件矩形/索引 ROI、覆盖保护和路径穿越拒绝；
- 验收范围：独立 CLI target 构建与参数测试，及默认 GUI 配置回归。GUI 人工操作和真实生产夹具不属于本项自动化验收。

### 2026-09-13（CLI 一键构建与便携部署）

- `scripts/build_windows.ps1` 新增 `-CliOnly`、`-Deploy`、`-DeployDir`；CLI-only 自动探测 CMake、MSVC 和 Qt，显式关闭两个 GUI 应用，`-Deploy` 仅允许 Release 并执行 Qt/VC 运行库部署、依赖检查和清空 PATH 的 15 项契约测试。部署目标必须在源码树外且不能是盘符根目录；已有目标目录会在安全检查后清空重建。
- `pointcloudcli temp-workpiece run` 新增 `--data-dir`/`PCV_DATA_DIR`/exe 同级 `data`/`%LOCALAPPDATA%\\PointCloudSuite\\data` 可写目录回退，缓存不再依赖固定盘符；GUI 数据目录和默认 `PCV_BUILD_CLI=OFF` 保持不变。
- 新增 `scripts/build_pointcloudcli_release.bat` 作为 Release 便携构建入口；便携目录不包含业务输入文件，不提供安装程序或签名。

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

### 2026-10-08（三点平面拟合等价提速 v2）

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。
