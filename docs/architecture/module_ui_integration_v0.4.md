# PointCloudSuite v0.4 模块与 UI 交接中间文档

状态：架构冻结草案（2026-09-22）。本文件是模块重整、UI 同事接入和后续交接的协同真相源；源码、CMake、公共头和测试优先级高于历史文档。本轮已完成扫描规划接入的 Debug/Release 构建和 CTest，GUI 真实点云全流程仍待人工验收。

## 1. 六模块职责与数据流

| 模块 | 规范目录/target | 职责 | 依赖 |
|---|---|---|---|
|10 pointcloud_io|`modules/10_pointcloud_io` / `pcv_m10_pointcloud_io`|PLY/缓存、相机输入、过滤、PNG/PLY/JSON writer、`ArtifactTransaction`|core/infrastructure/Qt Core+Gui|
|20 pointcloud_render|`modules/20_pointcloud_render` / `pcv_m20_pointcloud_render`|OpenGL、画布、点选、渲染 DTO|core|
|30 pointcloud_stitch|`modules/30_pointcloud_stitch` / `pcv_m30_pointcloud_stitch`|多帧拼接编排、ICP、多帧配准、seam fusion、诊断、manifest|10/50|
|40 pointcloud_matching|`modules/40_pointcloud_matching` / `pcv_m40_pointcloud_matching`|规划中的工件位置确认占位；本版不实现移动判定|30/60（兼容聚合）|
|50 coordinate_conversion|`modules/50_coordinate_conversion` / `pcv_m50_coordinate_conversion`|手眼标定、位姿插值、坐标变换唯一实现（`T_base_depth = T_base_flange * T_flange_depth`）|core|
|60 geometry_workpiece|`modules/60_geometry_workpiece` / `pcv_m60_geometry_workpiece`|平面拟合、平面/边缘几何、ROI、WObj、临时工件、扫描路径规划|10/50|

数据流固定为：输入 PLY/相机帧 → 10 读取/预处理 → 50 转换到 `robot_base` → 30 拼接或 60 几何处理 → 10 事务 staging/commit → 应用/UI 展示。共享模块不得依赖 `apps/`。

## 2. 40 模块正式声明

对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

当前状态：**规划目标，未实现**。本版不实现基准模板持久化、当前/基准比较、移动阈值和 6DoF 判定、机器人执行/坐标修正下发或新的正式输出闭环。`pcv_m40_pointcloud_matching` 与旧 registration/matching header 仅为 `INTERFACE` 转发入口。

## 3. 旧名称映射

| 旧 | 新/状态 |
|---|---|
|`modules/10_pointcloud_read`, `pcv_m10_pointcloud_read`|`modules/10_pointcloud_io`, `pcv_m10_pointcloud_io`；旧 target 转发|
|`modules/40_pointcloud_matching` 的 multiframe/ICP|物理迁入 30；旧 registration header 转发|
|`modules/40_pointcloud_matching` 的 ROI|物理迁入 60；旧 matching header 转发|
|`modules/60_plane_fitting`|`modules/60_geometry_workpiece`；`pcv_m60_plane_fitting`/`pcv_m60_planefitting` 转发|
|`modules/70_roi_template`|实现迁入 60，target/header 兼容转发|
|`modules/80_plane_output`|实现迁入 10，target/header 兼容转发|
|`src/filtering` / `pcv_filtering`|实现迁入 10 `pcv::processing`，旧 target/header 转发|
|`pcv::detail::io`、`pointcloud::`|保留一个版本周期；新 API 优先 `pcv::io/stitching/coordinates/geometry/workpiece`|

重命名按三批执行：公共边界 → 应用/UI → 历史兼容清理。每批均需维护映射和编译验证，不允许无记录批量替换。

### 命名与弃用规则

- 目录和文件：`lower_snake_case`；target：`pcv_mNN_lower_snake_case`；namespace：`pcv::<domain>`。
- 类型：`PascalCase`；函数和局部变量：`lowerCamelCase`；常量：`kPascalCase`。
- JSON 字段：`snake_case`，单位写入字段名，例如 `_mm`、`_deg`、`_ms`。
- 新公共 API 使用 `pcv::io`、`pcv::stitching`、`pcv::coordinates`、`pcv::geometry`、`pcv::workpiece`。
- 旧 `pointcloud::`、`pcv::detail::*`、旧 target 和旧 header 保留一个版本周期，并以 `[[deprecated]]` 或 forwarding 入口标记；兼容层不得新增实现。
- 每批重命名必须更新本映射表、直接依赖构建结果和回滚点，禁止无记录的全文批量替换。

## 4. 公共 API 与数据约定

- 30 提供分组式 `pcv::interface::StitchingRequest`（`input`、`registration`、`seam`、`artifacts`）和 `stitch()`；旧 `StitchingOptions` 标记 `[[deprecated]]` 并映射到新接口。
- 10 提供 `pcv::io::ArtifactTransaction`：`stageBinaryPly`、`stageGrayscalePng`、`stageJson`、`commit`、`abort`。相对路径必须位于输出根目录；默认拒绝覆盖，可选 `allowOverwrite`；取消、失败时回滚并清理 staging/backup。
- PLY 正式输出为 binary little-endian；PNG 为 `Grayscale8` 且像素归一化为 0/255；JSON 字段 `snake_case`，单位写入字段名（`_mm`、`_deg`、`_ms`）。
- 坐标约定：`robot_base`；控制器位姿 `[X,Y,Z,A,B,C]`，`A=Rx`、`B=Ry`、`C=Rz`，旋转 `Rz(C)*Ry(B)*Rx(A)`；长度 mm、角度 deg。
- 手眼链：`RTmatDepth2robot` = `T_flange_depth`（flange_from_depth）；`p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`。禁止默认求逆，禁止固定 Z 翻转/镜像/180° 补丁。`LineProfileXz` 的相机点是 `[X,0,Z]`，输入 Y 只表示扫描进度（`ratio = y / signedTravel`）；`FullXyz` 保留完整 XYZ；法向与点共用同一旋转且不平移。
- 30/60 只生成领域结果和 manifest；正式文件由 10 事务提交。取消不得发布部分结果或复用旧正式输出。

## 5. 文件所有权白名单

主线维护：`modules/**`、`src/**`、`apps/pointcloudview/mainwindow.*`、根 CMake、稳定公共头、模块测试和本文件。

UI 同事限定：`apps/pointcloudview/features/**`、`apps/pointcloudview/ui/` 中经交接登记的 UI 专属 `.ui`、UI 专属资源/样式文件、UI 测试与交互说明。主线持有 `MainWindow` 壳、生命周期、模块依赖和稳定 API。现有 `pointcloudstitch` 视觉本轮不纳入统一。

临时 adapter 只能位于 `apps/pointcloudview/adapters/**`，文件头必须标记 `Experimental` 或 `Deprecated`，并注明 owner、创建日期、替代 API、删除条件和截止版本；不得成为模块间长期依赖。

## 6. UI 与刀路规划接入边界

扫描路径规划属于 60 模块领域 API。`pcv::scan::planScanTrajectory()` 支持 `Global`（拟合平面有效点）和 `LocalRoi`（当前 ROI 有效点，规划阶段不重新拟合），固定使用 WObj1 X 作为横向覆盖轴、Y 作为行程轴，产品方向固定为 `+Y → -Y`。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、两端延长 `travelExtensionMm=50`；模块从 WObj1 XY 投影包围盒自动计算中心、原始/最终行程和扫描带。P 点 ABC 直接取 WObj1 姿态，返回连续 `P0/P1...` 轨迹点、独立扫描线段、项目 `scan_trajectory_v1` JSON 和 InoRobot `sr2026-p-json-mvp-2` 兼容 JSON。应用/UI 只负责参数转换、结果展示和文件接入，不实现第二套路径算法；本版不做 IK、关节限位、碰撞或干涉平面检查。

`pointcloudview` 主工作流在“边缘处理”后增加第五步“扫描规划”。页面默认局部 ROI，支持全局/局部模式、扫描高度、相机宽度和重叠距离，方向和“两端各延长 50 mm”只读显示；只读显示模块计算的中心、X 宽度、原始/最终 Y 行程、扫描线数量、实际间距和实际重叠。规划通过 `QtConcurrent` 调用领域 API，关闭窗口会取消并等待；发布前核对画布、平面、WObj1、ROI 和请求 revision。页面逐行显示内部 `P0=[X,Y,Z,A,B,C]`，并明确 P.json 的 `P[10]` 起始编号，分别以默认名 `scan_trajectory.json` 和 `P.json` 导出，覆盖前由 UI 明确确认，正式写入只通过模块 10 `ArtifactTransaction`。UI 不直接包含 `modules/*/src`，不调用应用 processor、seam fusion 或私有 writer。

## 7. 交接包格式与验收清单

交接包必须包含：文件白名单、API 依赖表、页面截图或交互说明、构建命令、测试结果、已知问题、临时 adapter 替代 API、owner、创建日期、删除条件、回滚点。

建议交接目录清单：

```text
handoff/
  ownership.md          文件白名单、owner、创建日期
  api_dependencies.md   稳定 API、DTO 版本、临时 adapter 与替代 API
  interaction.md        页面截图或交互说明、错误/取消行为
  verification.md       构建命令、实际测试结果、未验证项
  risks_and_rollback.md 已知问题、删除条件、回滚点
```

验收门禁：Debug/Release 新构建目录可配置；本轮双配置全量 CTest 均 `16/16`；`pointcloudview` 启动冒烟通过，新页面真实点云全流程 GUI smoke、JSON/PLY 现场数据、机器人夹具、40 移动判定和刀路算法仍明确标记为未验收。Release `windeployqt` 仍提示本机缺少 Visual Studio release redistributable，便携部署未验收。

### 2026-09-30 扫描规划验收补充

- Debug/Release 构建成功；双配置全量 CTest 均 `16/16`，`scan_planner_tests` 双配置通过；`pointcloudview` 双配置自关闭启动冒烟均退出码 `0`；`git diff --check` 通过。
- GUI 人工 LocalRoi 单线/多线、参数修改、固定方向、P.json 导出和旧结果失效未完成，原因是 computer-use connector 返回 `unsupported Codex auth method: apikey`；真实相机、机器人可达性、碰撞/干涉和控制器导入仍需现场验收。

## 9. 2026-09-20：PLY 临时工件线路接入记录

主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`、`10_pointcloud_io`；UI 编排 `apps/pointcloudview/mainwindow.*`（仍属主线维护白名单）。

- 现有“打开单个 PLY”入口改造为 PLY 临时工件线路，不新增独立一键入口；文件夹扫描 PLY 保持只读预览。
- 应用层只做参数收集、状态编排、异步生命周期和结果展示：PLY 线路参数对话框直接调用模块 50 的 `loadHandEyeCalibration()` 与 `validateRigidTransform()` 做即时校验，主窗口内不新增手眼转换、平面拟合、ROI 或输出算法。
- 模块 60 的 `prepareTempWorkpiece()` 新增内存扫描描述符分支（`TempWorkpieceOptions::useInMemoryScan` + `scanInfo`），与 JSON 分支共用同一段“读取 PLY → 加载手眼标定 → `transformLineScanToRobotBase()`”实现；JSON 入口行为不变。
- 两组三点是 PLY 线路唯一的人工步骤：第一组选满后自动拟合平面并自动进入第二组；第二组同面校验通过后由既有 JSON 后半段编排自动完成 WObj1、边缘分割、ROI、Mask 与四件套提交，两线路共用同一条自动链，未复制算法。
- 应用层新增工作流状态约束（Idle/ReadingPly/Transforming/SelectingPrimaryPlanePoints/ValidatingPrimaryPlane/SelectingSecondaryPlanePoints/ValidatingSecondaryPlane/Completed/Failed/Cancelled 语义），自动与 worker 阶段禁止重复打开数据源，画布 revision 改变即失效旧会话，取消结果不得发布为成功。
- 四件套契约与错误码未变化；PLY 线路默认禁止覆盖已有正式文件，不在自动执行中途询问覆盖。

验证状态：2026-09-21 源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过。GUI smoke 和真实夹具仍未验收。

### 2026-09-21：状态机排错与验证记录

- `apps/pointcloudview` 将临时工件来源收拢为 `None/Json/Ply` 单一状态；参数对话框取消、JSON 解析失败不再切换工作流状态。
- 旧 `Cancelled/Failed` 会话在新源参数确认后必须经过替换确认；确认后失效旧会话但保留画布，取消则完整保留旧会话。
- 自动链同步失败统一进入 `Failed`，保留当前转换点云和已完成几何结果，允许重试和打开新源；不发布部分四件套。
- JSON 位姿摘要按 `[X,Y,Z,Rx,Ry,Rz]` 显示，公共坐标转换契约不变。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke 和真实夹具未执行。

### 2026-09-21：代码验收与发布边界

- 代码验收：Debug/Release 源码树外构建成功，双配置全量 CTest 均为 `15/15`，`git diff --check` 通过。
- 发布边界：本次仅发布 PLY/JSON 临时工件状态机、取消/失败语义、模块 60 共享转换路径、对应测试和文档；既有模块 30/PointCloudStitch 工作区修改不纳入。
- GUI smoke、真实线扫 PLY/XML、机器人夹具和 WObj1 朝向仍需人工现场验收。

## 11. 2026-09-24：手眼标定统一与点云倒转修复

主模块 `50_coordinate_conversion`；调用方清理：`apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`。

- **黄金基准**：扫描 JSON 路径（`pcv::interface::prepareTempWorkpiece()` → `transformLineScanToRobotBase()`）是手眼标定的黄金基准，其余离线 JSON、PLY 临时工件、多帧和拼接路径必须与它逐点一致。
- **根因**：点云上下/法向反转风险来自“重复实现 + LineProfileXz 语义不一致”，不是矩阵方向问题。旧 `apps/pointcloudview/pointcloudprocessor.cpp` 自带刚体校验、SLERP 插值、进度轴与 `EmbeddedMotion` 启发式和内联 `baseFromFlange * flangeFromDepth`，旧 `regressionrunner.cpp` 自带 XML 解析与 `poseMatrix()`；旧合并路径还把输入 Y 当相机坐标保留。
- **修复**：50 成为唯一数学实现，新增 `composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus` 状态码；两个调用方的重复解析、重复插值和内联矩阵乘法全部删除，改调 50 的公共函数。**没有**通过矩阵求逆、Z 翻转或镜像来“修”方向。
- **调用链**：`apps/pointcloudview` 的扫描 JSON 与 PLY 临时工件路径（经 60）、`modules/60_geometry_workpiece`（`transformScanToRobotBase()`、`roi_matching`）、`modules/30_pointcloud_stitch`（`stitching_interface.cpp`、`multiframe_registration.cpp`）、`apps/pointcloudstitch` UI 与回归路径全部复用 50；调用方只保留参数转换和结果映射。
- **验证**：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，`handeye_transform_tests`、`temp_workpiece_interface_tests`（含 JSON 入口 vs 多帧入口逐点一致性）、`stitching_interface_tests`、`multiframe_registration_tests` 定向测试 `4/4`，`pointcloudview` 双配置启动冒烟均 `ExitCode=0`。真实样本有效转换 `4,389,670` 点，JSON robot_base 与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 因缺少本机 Visual Studio release redistributable 发出警告，便携部署未验收；真实 XML 物理方向、机器人 A/B/C 现场定义、相机安装方向和实时相机预览仍未完成现场验证。

## 10. 已知问题与回滚点

- 当前工作区原有用户未提交修改作为迁移基线保留；不执行远程提交、上传、删除旧兼容层或真实机器人联调。文件数量以实时 `git status --short` 为准，不在文档中冻结。
- 既有构建目录权限不足，本轮使用新的源码树外可写目录；GUI smoke 和真实夹具未在本次自动化验证中执行。
- 回滚以本文件映射表和三批迁移边界为准：优先恢复对应模块目录/CMake/转发 header，保留用户原有 8 个文件，不使用破坏性 reset/checkout。

## 12. 2026-10-06：离线 PLY + 亮度图纹理点云

- **范围**：在 `pointcloudview` 增加离线测试链路：打开现有 PLY，沿用三点平面提取，选择 PNG/JPG/BMP 亮度图，将图像灰度映射到现有平面点并在 3D 画布显示；不接入实时取流，不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- **60 模块**：新增 `pcv::texture::mapPlaneBrightness()`。当前版本要求单个 PLY 保留原始 `sourceIndex`，亮度图像素数必须等于原始 PLY 点数，按左上角起始 row-major 直接采样；仅 `planeIndices` 对应点着色，图像 `Format_Grayscale8`、忽略 alpha，不做投影、裁剪、缩放、翻转或配准；source provenance 不完整时拒绝并返回诊断。
- **20 模块**：新增 per-point 灰度覆盖 DTO 与校验，增加纹理灰度颜色模式和 OpenGL 上传缓冲。有效平面点显示纹理灰度，非平面点/无效点回退当前颜色模式；点云、平面或坐标系变化清除旧覆盖，模式切换保留缓存。
- **应用/UI**：增加“选择亮度图并贴到平面”入口。图像读取、映射和取消在后台执行，完成后由 GUI 线程发布覆盖并切换纹理灰度；revision 变化、取消或失败不发布半成品，新图成功前不替换旧纹理。状态栏显示尺寸、平面点数、成功/跳过数和映射方式。
- **验证**：源码树外 Debug/Release 构建成功；双配置全量 CTest 均 `17/17`；新增 `texture_mapping_tests` 与扩展后的 `pointcloud_canvas_contract_tests` 通过；`git diff --check` 需在提交前执行。
- **未完成验收**：真实 PLY + 亮度图 GUI 手工流程（方向、中心裁剪、模式切换、替换、取消、无效图片和无 WObj1 回退）、大点云性能、真实相机取流和现场机器人验收仍未完成。Release `windeployqt` 因本机缺少 Visual Studio release redistributable 未完成便携部署。

## 13. 2026-10-06：亮度图改为严格 PLY 栅格对应

- 离线纹理流程限定为单个 PLY 临时工件。转换阶段保留原始 PLY `declaredPointCount`、当前点到原始点的 `sourceIndices` 和过滤统计；文件夹 PLY、过滤/降采样后的点云和无 provenance 点云禁用纹理。
- 亮度图像素数必须等于原始 PLY 点数，固定使用左上角起始 row-major 索引：`x = sourceIndex % width`、`y = sourceIndex / width`。仅 `planeIndices` 对应像素映射到当前平面点；非平面像素完全忽略。
- 亮度图只做 `Format_Grayscale8` 转换，禁止局部坐标投影、包围盒覆盖、中心裁剪、缩放、翻转、配准和对比度增强。任何过滤点、缺失/重复/越界 source index 或点数变化都会使整次映射失败。
- WObj1 不参与纹理采样，WObj1 变化不清除已有纹理；点云或平面索引变化清除纹理，同一平面索引的边缘结果更新保留纹理。
- 最终验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `17/17`；两配置纹理定向测试和 `pointcloudview` 自关闭启动冒烟通过；`git diff --check` 通过。真实 PLY/亮度图的 GUI 视觉验收、现场 row-major 对应关系和实时取流仍未验证。

## 14. 2026-10-07：点云预览工作台 UI 简化

- **主窗口职责**：`apps/pointcloudview/mainwindow.*` 保留后台任务生命周期、取消、revision 门禁、PLY/JSON/相机/多帧业务流程和状态栏；左侧“采集与处理总控中心”、步骤导航、上一步/下一步和常驻文件列表不再作为主窗口可见布局。顶部仅保留两个数据入口和“工具”菜单，3D 预览为客户区主体，验收目标为无工具窗口时至少占客户区 85%。
- **工具窗口职责**：复用现有 Designer 控件建立五个 `Qt::NonModal` 窗口：数据源（文件列表和当前文件信息）、点云清理、平面与 WObj1、边缘与输出、扫描规划。窗口设置为不自动删除，关闭只隐藏；参数、结果和后台 watcher 仍归 `MainWindow` 管理，可同时打开多个窗口。渲染设置继续使用 `RenderDialog`，相机和多帧配准入口未移除。
- **取点交互**：开始第一组三点、第二组三点或边缘选择时隐藏对应工具窗口并显示主窗口底部临时操作条；操作条按当前模式提供计数、撤销、取消和确认/完成，Esc、Backspace、PLY 自动流程和扫描 JSON 流程复用既有语义。操作完成、取消或失败后恢复对应窗口。
- **数据源迁移**：`m_fileList` 仍是既有加载逻辑的唯一列表来源，但控件归属数据源窗口；单文件、文件夹扫描、扫描 JSON 和多帧结果继续更新同一列表和状态栏，不改变输入路径、来源状态或临时工件会话保护。
- **验证与边界**：Debug/Release 构建和全量 CTest 均通过（各 `17/17`），两配置 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟退出码为 `0`。真实点云 GUI 人工验收、85% 实际像素测量、相机和机器人现场验证仍待完成；Release `windeployqt` 缺少 Visual Studio release redistributable。
### 2026-10-08（PLY 原始坐标纹理实验入口）

- `pointcloudview` 的数据源选择增加独立“PLY 纹理实验（原始坐标）”分支。它只读取单个 PLY，保留原始 XYZ、声明点数和 identity source index，不进入模块 50 手眼转换、WObj1 建立或模块 60 临时工件四件套自动链。
- 实验分支仍复用三点平面拟合和第二组三点同面校验；实验拟合固定使用任意平面方向的垂直距离。校验成功后才开放同次扫描亮度图选择。
- 亮度图显示继续由模块 60 `mapPlaneBrightness()` 和模块 20 per-point overlay 完成：图像像素数必须等于 PLY 声明点数，左上角起始 row-major，只有 `planeIndices` 对应点着色；取消、失败、尺寸不符或 provenance 不完整均不得发布新覆盖。
- 新增 `texture_experiment_tests` 为无 QWidget/OpenGL 的应用契约测试，覆盖读取、平面、同面和严格映射；真实 GUI 视觉验收与现场 PLY/亮度图点序一致性仍需人工完成。

### 2026-10-08（扫描规划单线容差修复）

- `60_geometry_workpiece` 的扫描线判定增加固定 `0.1 mm` 单线容差，ROI/WObj1 投影宽度不超过 `cameraWidthMm + 0.1 mm` 时只发布一条扫描线 `P0/P1`。
- 只有超过容差才按实际宽度、相机覆盖和请求重叠计算多条线；内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm` 诊断字段，InoRobot `P.json` 契约保持不变。
- 测试补充单线容差边界和超容差多线边界；真实相机有效视野、ROI 边界误差和机器人现场行为仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，双配置全量 CTest 均 `18/18`；`pointcloudview` 双配置启动冒烟退出码 `0`，`git diff --check` 通过。真实 ROI GUI 复验未完成；Release 便携部署仍缺本机 Visual Studio release redistributable。

### 2026-10-08（三点平面拟合等价提速 v2）

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。
