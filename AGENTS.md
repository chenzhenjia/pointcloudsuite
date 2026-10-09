# PointCloudSuite Agent 工作手册

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

适用范围：本仓库。沟通默认使用中文；代码、命令、变量名、文件名、路径和错误信息保留原文。最后核对日期：2026-09-17。

## 1. 真相源与安全边界

- 当前源码、CMake 和测试是事实来源；v0.4 模块/UI 协同真相源为 `docs/architecture/module_ui_integration_v0.4.md`，历史文档只用于追溯。
- 只修改用户明确指定的文件和模块，保留其他未提交修改。
- 删除、覆盖、批量重命名、外部上传、账号或数据库操作前必须获得明确授权。
- 禁止写入 password、secret、token、API key、private key。
- 缓存、日志、导出物和运行数据不得写入源码树。
- Windows 源码、构建、输入和输出路径支持 Unicode；构建目录仍应放在源码树之外。
- 不把 build*/、test_pointcloud_a/、mid_gap/ 或实验报告当作稳定 API。
- 不使用 `git reset --hard`、`git checkout --` 等破坏性命令回退用户修改。

## 2. 当前项目基线

- 项目基线：PointCloudSuite v0.4 模块重整；数据字段和 PLY/JSON 契约继续沿用 v0.3，发布验收仍未完成。
- 语言和工具：C++17、CMake 3.19+、Qt 6.5+、Windows/MSVC。
- 根选项：
  - `PCV_BUILD_POINTCLOUDVIEW`
  - `PCV_BUILD_POINTCLOUDSTITCH`
  - `PCV_BUILD_CLI`（默认 `OFF`，生成独立的 `pointcloudcli` 点云读取和临时工件入口）
  - `PCV_BUILD_TOOLS`
  - `PCV_BUILD_TESTS`
  - `PCV_BUILD_EXPERIMENTAL_CAMERA`（CMake 默认 `ON`；构建脚本默认关闭）
- 外部算法依赖：不依赖 Python、Open3D、PCL、VTK 或 CUDA。
- 当前架构与交接文档：`docs/architecture/module_ui_integration_v0.4.md`；数据契约文档：`docs/requirements/pointcloudview_v0.3.md`。
- 模块详细记录：`docs/modules/<module>.md`。

## 3. 模块与 target

规范模块及唯一职责：

| 编号 | 模块 | 规范目录 | 规范 target |
|---|---|---|---|
| 10 | 点云读写、相机帧输入、过滤与事务输出 | `modules/10_pointcloud_io` | `pcv_m10_pointcloud_io` |
| 20 | 点云渲染 | `modules/20_pointcloud_render` | `pcv_m20_pointcloud_render` |
| 30 | 点云拼接与 seam fusion | `modules/30_pointcloud_stitch` | `pcv_m30_pointcloud_stitch` |
| 40 | 工件位置确认规划占位 | `modules/40_pointcloud_matching` | `pcv_m40_pointcloud_matching`（`INTERFACE`） |
| 50 | 坐标转换 | `modules/50_coordinate_conversion` | `pcv_m50_coordinate_conversion` |
| 60 | 平面、边缘、ROI、WObj、临时工件 | `modules/60_geometry_workpiece` | `pcv_m60_geometry_workpiece` |
| 90 | `90_interferenceplane` 干涉平面检查 | 仅文档占位 | 未实现 |
| 100 | `100_qualityreport` 质量报告 | 仅文档占位 | 未实现 |

迁移期兼容 target 包括旧模块名 `pcv_m05_camerainput`、`pcv_m10_pointcloud_read`、`pcv_m10_pointcloudread`、
`pcv_m20_pointcloudrender`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、
`pcv_m50_coordinateconversion`、`pcv_m60_plane_fitting`、`pcv_m60_planefitting`、`pcv_m70_roi_template`、`pcv_m80_plane_output`、`pcv_m80_planeoutput`，以及
`pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`。它们只能做 target/header
转发，不得新增与规范模块重复的实现；只有在所有调用方迁移、弃用周期结束并完成独立
评审后才能删除。

## 4. 依赖方向和代码边界

- 共享算法放在模块 `src/`，公共头放在模块 `include/`。
- 共享模块不得依赖 `apps/`。
- `20_pointcloud_render` 是唯一允许依赖 Qt Widgets/OpenGL 的共享模块；其他算法模块不得反向依赖 UI。
- `30_pointcloud_stitch` 是唯一拼接编排边界，负责输入读取、坐标转换/匹配调用、seam fusion、诊断、正式输出和失败回滚。
- `40_pointcloud_matching` 是规划中的工件位置确认占位；当前不实现基准比较、移动阈值或 6DoF 判定。兼容聚合入口将现有多帧配准/ICP 转发到 30、ROI/临时工件几何转发到 60。
- `50_coordinate_conversion` 是坐标转换唯一数学实现来源；`60_geometry_workpiece` 负责纯几何拟合、ROI、WObj 和临时工件。
- `60_geometry_workpiece` 的正式输出必须通过 10 的 `pcv::io::ArtifactTransaction`，不得直接提交最终文件。
- `modules/10_pointcloud_io` 同时承载原 `05_camerainput`、根 `src/filtering` 和 80 writer 的实现；相机能力受 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制。
- `20_pointcloud_render` 的 `PointCloudCanvas` 实现位于 `modules/20_pointcloud_render/src/pointcloud_canvas.cpp`；应用只负责业务状态、异步生命周期和渲染 DTO 转换。
- Qt Designer 文件位于 `apps/pointcloudview/ui/`、`apps/pointcloudstitch/ui/` 和实验性 `apps/intensity_viewer/ui/`，不得把生成头文件提交为手工 UI 源。
- `pointcloudcli` 提供 `--help`、`--version`、`inspect ply`、`read ply` 和 `temp-workpiece run`；仅依赖 Qt Core/Gui/Concurrent，不接入 QWidget、OpenGL 或 GUI 事件循环。前两个 PLY 命令只读输入，不写出新文件；`--data-dir` 只用于临时工件命令，按 CLI 参数、`PCV_DATA_DIR`、exe 同级 `data`、`%LOCALAPPDATA%\\PointCloudSuite\\data` 顺序选择可写运行目录。
- `registration_diagnostic` 仅通过 30 的统一拼接接口调用流水线；新代码使用 `pcv::interface::StitchingRequest` / `stitch()`，`stitchRawLineProfiles` 仅为兼容入口。
- 应用 wrapper 只能做参数转换和结果映射，不得保留第二套算法。

## 5. 稳定契约

修改公共类型、输出格式、坐标约定或错误码前，先阅读 v0.4 协同文档、v0.3 数据契约和对应模块文档。

- PLY 读写：按属性名读取坐标和法向，支持 ASCII、binary little-endian、binary big-endian；保留点序、source index、bounds、取消和错误语义。
- 坐标转换：控制器位姿顺序为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`；旋转约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 配准和拼接：多帧流程由 30 的 `registerRobotBaseFrames()` 与 `applyTrajectorySeamFusion()` 统一编排；60 负责 ROI/临时工件几何。取消或失败时不得发布部分结果或复用旧正式输出。
- 长流程统一使用 `pointcloud::CancellationToken` 或等价取消回调；取消结果必须带诊断，不得当作成功结果发布。
- 并行 worker 与 Qt 隐式共享容器：worker 只允许通过 `const` 视图（`const QVector<T>&`、`.at()`、`->at()`）读取共享容器，禁止在下标访问中使用非 `const` 的 `container[i]`。Qt 的非 `const` `operator[]` 会 `detach()`，多线程同时 detach 同一 refcount > 1 的缓冲会破坏堆（历史现象为间歇 `0xc0000005`）。捕获列表必须显式列出只读对象；worker 只返回私有任务结构，共享结果、诊断和输出容器在 future 全部汇合后由主线程按确定顺序提交。新增或修改并行路径时，回归测试必须保留额外共享副本使 refcount > 1，并覆盖 `parallelEnabled=false` 与 `maxParallelTasks=1/2/4` 的等价性。
- seam fusion 默认值：`halfWidth=8.0f`、`mutualDistance=0.6f`、`decisionCellSize=0.5f`。
- 平面拟合：拒绝重复、越界、非有限和共线控制点；保留 RMS、内点、阈值、planarity、连通域和取消诊断。
- 渲染：仅 GUI 线程修改 QWidget/OpenGL；拒绝点、轮廓、坐标系中的非有限值和越界索引。
- 平面输出：由 10 的 `ArtifactTransaction` 成套提交 PNG、PLY、JSON；任一写入失败整体失败并回滚。PNG 使用 `Grayscale8` 和 0/255；平面 PLY 使用 binary little-endian。
- 共享库不弹窗、不访问 QWidget；通过 Result 状态、错误码、取消标记和诊断返回结果。
- 应用层负责用户提示、窗口生命周期和线程结果发布。

### 2026-09-22（扫描规划收敛为局部 ROI）

- 扫描规划支持 `Global`/`LocalRoi` 双模式；`pcv::scan::planScanTrajectory()` 分别接受拟合平面或当前 ROI 有效索引，中心和范围由 WObj1 XY 投影包围盒计算，沿 `+Z` 使用统一 `scanHeightMm`，ABC 直接取 WObj1 `poseA/poseB/poseC`。
- `scan_trajectory_v1` JSON 支持 `mode=global` 与 `mode=local_roi`；UI 通过同一规划页选择模式，输入统一 `scanHeightMm`、相机宽度和 Y 行程方向，范围由平面/ROI 的 WObj1 XY 包围盒自动计算，旧结果失效、取消和事务导出行为保持不变。
- 真实机器人可达性、相机有效宽度、碰撞/干涉及真实样件 GUI 全流程仍需现场验证。

详细字段、JSON schema、错误码和测试矩阵以：
`docs/architecture/module_ui_integration_v0.4.md`、`docs/requirements/pointcloudview_v0.3.md`、`docs/modules/<module>.md`、公共头文件和测试源码为准。

## 6. 单模块开发流程

每个需求必须先登记主模块编号：

1. 执行 `git status --short`，记录已有工作区修改。
2. 阅读目标模块文档、公共头、CMake 和相关测试。
3. 设计或确认接口，列出受影响模块和依赖方向。
4. 只修改目标模块及必要的跨模块接口、测试和文档。
5. 先运行目标模块测试，再构建直接依赖和应用。
6. 涉及公共头、CMake、processor、UI 或输出契约时执行完整 Debug 构建和全量 CTest；发布相关改动补充 Release。
7. 更新 `AGENTS.md`、`docs/modules/<module>.md` 和 v0.4 协同文档；数据契约仍在 v0.3 需求文档中追踪。记录日期、需求、文件、行为、验证和风险；目录或 target 迁移时同步更新索引、架构和历史说明。
8. 提交前执行 `git diff --check` 和 `git status --short`。

跨模块修改必须在提交说明中列出影响模块、依赖变更和回归测试。未实现模块不得写入功能成功记录。

## 7. 构建与验证

Windows + Qt/MSVC 推荐使用仓库脚本，并显式传入可写、位于源码树外的构建路径：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
.\scripts\build_windows.ps1 -Config Release -BuildTests -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
```

CLI-only 构建会自动探测 CMake、Visual Studio/MSVC 和 Qt（也可通过 `-CMakePath`、`-QtDir` 显式覆盖），并关闭两个 GUI 应用：

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

`-Deploy` 仅接受 `-CliOnly -Config Release`，会生成包含 Qt/VC 运行库、`data` 目录和 `README.txt` 的便携目录，并在清空 PATH 的子进程中执行 15 项 CLI 契约测试。部署目录必须在源码树外且不能是盘符根目录；已有目录会在安全检查后清空重建。`PCV_BUILD_CLI` 默认仍为 `OFF`；不传 `-CliOnly` 时 GUI 构建逻辑保持不变。

脚本默认使用 `NMake Makefiles` 并将 `PCV_BUILD_EXPERIMENTAL_CAMERA` 设为 `OFF`；需要实验相机时追加 `-ExperimentalCamera`。`NMake Makefiles JOM` 不支持头文件依赖跟踪。

CTest 使用对应构建目录和配置运行：

```powershell
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

工具构建：

```powershell
cmake -S . -B <ToolsBuildDir> -DPCV_BUILD_TOOLS=ON
cmake --build <ToolsBuildDir> --config Debug --target registration_diagnostic
```

当前自动化回归包含 `roi_matching_tests`、`seam_fusion_tests`、`multiframe_registration_tests`、
`plane_fitting_tests`、`pointcloud_canvas_contract_tests`、`handeye_transform_tests`、
IO/输出/接口测试，以及实验相机开关开启时的 `frame_converter_tests`、`frame_source_tests`
和实时 pipeline 测试；启用 `PCV_BUILD_CLI` 时另注册 15 项 `cli_` 契约测试。验证结论只报告本次实际运行的命令和结果；GUI 人工操作、真实生产
夹具和未实现模块不能用单元测试替代。

## 8. 兼容层清理

删除兼容 target 或 forwarding header 前必须同时满足：

- 生产源码、CMake、测试和工具不再引用旧名称；
- 所有公共 include 已切换到规范模块路径；
- 应用 processor 不再包含重复配准、拼接或平面算法；
- GUI 和接口只保留一条 stitching/registration/seam pipeline；
- Debug/Release 构建、全量 CTest、GUI smoke 和真实夹具验收通过；
- 弃用周期结束或获得明确删除批准。

删除动作必须独立提交，便于回退；不得与功能迁移混在同一提交。

## 9. Git 与汇报

- 默认保留用户修改，只提交明确属于本任务的文件。
- 用户要求本地提交时只执行本地 `git commit`，不执行远端 push。
- 汇报必须说明：
  - 做了什么；
  - 修改了哪些文件和 API；
  - 如何验证及结果；
  - 未验证项和剩余风险；
  - 最终工作区状态。

推荐汇报格式：

```text
做了什么：
- ...

改了什么：
- 文件：...
- 关键行为/API：...

如何验证：
- 命令：...
- 结果：...

风险/未验证项：
- ...
```

## 10. 变更记录（追加，不修改以上规则）

### 2026-09-20（PLY 线路：打开 PLY → robot_base → 四件套）

- 主模块 `60_geometry_workpiece`；协作 `50_coordinate_conversion`、`10_pointcloud_io`；UI 编排 `apps/pointcloudview`。
- 现有“打开 PLY”入口接入临时工件流程：手眼 XML + 手工 Start/End 位姿 → 转换到 `robot_base` → 第一组三点自动平面拟合 → 第二组三点同面校验 → 自动 WObj1、边缘分割、ROI、Mask → `ArtifactTransaction` 一次性提交四件套。两组三点是唯一人工步骤。
- 模块 60 追加 `TempWorkpieceOptions::useInMemoryScan` / `scanInfo` 与 `TempWorkpiecePreparation` 诊断字段（`inputYMinimum`、`inputYMaximum`、`signedTravel`、`dominantTravelAxis`、`cancelled`），JSON 入口行为与公共 JSON schema、四件套文件名/格式、错误码均未变化；兼容 target 与 forwarding header 未改动。
- 本轮未执行 Git 操作、未执行 Debug/Release 构建、未执行 CTest 和 GUI 验收；结论仅覆盖代码级与静态检查。

### 2026-09-21（PLY/JSON 临时工件状态机排错修复）

- `apps/pointcloudview` 新增 `None/Json/Ply` 单一临时工件来源状态、统一会话失效、旧会话替换确认和自动链失败出口；参数对话框取消或 JSON 解析失败不再遗留 `WaitingForParameters` 锁死状态。
- 主动放弃取点保留转换点云和会话，可重新取点；WObj1、边缘、ROI/Mask、四件套前置校验等同步失败统一进入 `Failed`，保留结果并允许重试；打开新源前确认是否放弃旧会话。
- JSON 位姿摘要统一显示 `[X,Y,Z,Rx,Ry,Rz]`，与 `[X,Y,Z,A,B,C]`、`A=Rx/B=Ry/C=Rz` 契约一致；公共 schema、四件套格式和错误码未改动。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke、真实 PLY/XML 和生产夹具未验证。

### 2026-09-21（代码验收与发布边界）

- 代码验收通过：源码树外 Debug/Release 构建成功，双配置全量 CTest 均为 `15/15`，`git diff --check` 通过。
- 本次发布只包含 PLY/JSON 临时工件线路修复、模块 60 接口/实现、对应回归测试和文档；已有 `30_pointcloud_stitch`、PointCloudStitch、临时脚本等工作区修改保留，不混入提交。
- GUI smoke、真实 PLY/XML 和生产夹具现场验收仍未完成，不能以自动化测试替代。

### 2026-09-22（扫描路径规划修订与 UI 接入）

- 主模块 `60_geometry_workpiece`；UI 编排 `apps/pointcloudview`；JSON 正式写入复用 `10_pointcloud_io::ArtifactTransaction`。
- 扫描规划固定 WObj1 X 横向/Y 行程：全局中心由用户输入 `XYZABC`，局部中心由 ROI 三维质心投影到 WObj1 XY 后沿 `+Z` 偏移并继承 ABC；目标宽度、行程长度、相机宽度和重叠距离均为 mm。
- 扫描线按 `-X → +X` 编号，所有线使用用户选择的同一 Y 方向，输出连续 `P0→P1`、`P2→P3`。`scan_trajectory_v1` 记录最终中心、请求/实际重叠、间距、方向、P 点与段。
- `pointcloudview` 增加第五步“扫描规划”、后台取消与 revision 门禁、P 点显示、覆盖确认和事务 JSON 导出；画布、WObj1、ROI 或参数变化后旧结果立即失效。
- 自动化与风险结论以本轮最终验证为准；不把软件轨迹计算等同于真实机器人可达性、相机有效宽度、安全距离、碰撞或干涉验收。
- 本轮验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟通过，`git diff --check` 通过；Release `windeployqt` 缺少本机 Visual Studio release redistributable，未做便携部署。未执行真实点云/ROI 的全流程 GUI 手工验收。

### 2026-09-22（全局与局部扫描路径规划统一）

- 恢复 `pcv::scan::ScanPlanningMode::{Global,LocalRoi}`；全局使用拟合平面索引，局部使用 ROI 索引，均从 WObj1 XY 投影包围盒计算中心、范围，统一沿 `+Z` 偏移 `scanHeightMm`。
- 扫描带固定 50% 重叠，UI 删除人工宽度/行程/重叠/ABC 输入，增加模式切换、范围显示、项目 JSON 与 InoRobot `P.json` 两个事务导出；InoRobot 使用 `sr2026-p-json-mvp-2`、`wobj_num=1`、P[10] 起始和 `[X,Y,Z,RZ,RY,RX]`。
- 本轮扫描单元测试通过，pointcloudview Debug 目标编译通过；Release、全量 CTest 和真实点云 GUI 全流程尚未在本轮重复验证。真实机器人可达性、相机有效宽度、安全距离、碰撞与干涉仍是现场风险。

### 2026-09-24（模块 50 手眼标定统一与点云倒转修复）

- 主模块 `50_coordinate_conversion`；调用方清理 `apps/pointcloudstitch/regressionrunner.cpp`、`apps/pointcloudview/pointcloudprocessor.cpp`；复用入口核对 `apps/pointcloudview`（扫描 JSON、PLY 临时工件）、`modules/60_geometry_workpiece`、`modules/30_pointcloud_stitch`、`apps/pointcloudstitch` UI 与回归路径。
- 黄金基准与统一契约：扫描 JSON 路径是手眼标定黄金基准；`RTmatDepth2robot` = `T_flange_depth`（flange_from_depth）；`p_robot_base = T_base_flange(Start/End 插值) * T_flange_depth * p_camera`，即 `T_base_depth = T_base_flange * T_flange_depth`。禁止默认求逆，禁止固定 Z 翻转、镜像或 180° 补丁。`LineProfileXz` 的相机点为 `[X,0,Z]`，输入 Y 只表示扫描进度（`ratio = input.y / signedTravel`，负行程是合法反向扫描，方向由 Start/End 决定）；`FullXyz` 保留完整 XYZ；点与法向共用同一旋转且法向不平移。
- 根因与修复：反转风险来自重复实现与 LineProfileXz 语义不一致。模块 50 新增 `composeBaseFromDepth()`、`computeScanTravel()`、`lineProfileCameraPoint()`、`lineProfileScanRatio()`、`RobotCloudStatus`/`robotCloudStatusCode()`/`robotCloudStatusText()`，`transformPointToRobotBase()` 追加可选 `QString *error` 尾参；删除回归工具的重复 XML 解析与 `poseMatrix()`、view 应用的重复刚体校验/SLERP/进度启发式/内联矩阵乘法，改调模块 50。既有矩阵方向、位姿约定、单位、PLY/PNG/JSON 契约与错误码不变；view 应用合并缓存版本提升为 9。
- 测试：`handeye_transform_tests` 覆盖乘法方向、JSON 黄金链路、LineProfileXz 的 `[X,0,Z]` 与 Y 语义、正负行程与反向扫描点序、FullXyz、法向、位姿往返、非有限/非正交/非单位/行列式非 +1 的拒绝、无行程/空点云/全部无效/超范围/取消与状态码；`temp_workpiece_interface_tests` 增加 JSON 入口与多帧入口的 robot_base 逐点一致性。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，4 项手眼/临时工件/拼接/多帧定向测试 `4/4`，`pointcloudview` 双配置启动冒烟 `ExitCode=0`。真实样本转换有效点 `4,389,670`，JSON 输出与历史 baseline、多帧入口逐点比对的最大绝对误差和 RMS 均为 `0`。Release `windeployqt` 提示缺少本机 Visual Studio release redistributable，便携部署未验收。
- 未验证：真实 XML 物理方向、机器人 A/B/C 与现场控制器定义、相机实际安装方向、实时相机预览统一（不在本轮范围）以及真实机器人安全/可达性/碰撞/干涉；结论只来自本轮实际执行的构建、回归和启动冒烟。

### 2026-09-30（扫描路径与 InoRobot P.json 规则修订）

- 主模块 `60_geometry_workpiece`；UI `apps/pointcloudview`；正式 JSON 仍复用 `10_pointcloud_io::ArtifactTransaction`。
- `Global`/`LocalRoi` 双模式保留，UI 默认 LocalRoi，局部模式直接使用 `localRoiIndices` 且不重新拟合。扫描高度、相机宽度、重叠默认分别为 50/100/50 mm；横向按 `cameraWidthMm-overlapMm` 均匀分布，首尾覆盖贴合 ROI X 边界，固定 `-X→+X` 排序和 `+Y→-Y` 行程，每条线两端各延长 50 mm。
- `ScanPlanningOptions` 增加 `overlapMm`/`travelExtensionMm`；结果明确报告原始/最终行程、扩展值、请求/实际重叠、实际间距和线数。InoRobot 限制最多 9990 点，`source_workpiece_info=temp_workpiece_info.json`；P.json 使用 `P[10]...`，姿态 `[X,Y,Z,RZ,RY,RX]`，不写控制器。
- UI 删除方向选择、增加重叠参数和固定延长提示，显示规划摘要及内部 `P0/P1` 与导出 `P[10]/P[11]` 的编号映射；保留后台取消、窗口关闭等待、revision 门禁、内部 JSON 导出和覆盖确认。
- 验证：本轮定向 `scan_planner_tests` 通过；Debug/Release 构建、全量 CTest、启动冒烟与真实点云 GUI 操作结果记录在本轮最终验收条目。机器人可达性、碰撞/干涉与真实控制器导入不属于软件验证。

### 2026-09-30（扫描规划最终验证）

- 追加测试覆盖：实际重叠不低于请求值、两端固定延长、倾斜 WObj1 的 P.json 局部 XYZ、无效坐标轴、退化 ROI 投影和 9990 点边界；UI 的 overlap 最大值随 cameraWidth 联动。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `16/16`，双配置 `scan_planner_tests` 通过，`POINTCLOUDVIEW_SELFTEST_CLOSE=1` 启动冒烟双配置退出码均为 `0`，`git diff --check` 通过。
- 未验证：GUI 人工 LocalRoi 单线/多线、参数修改、P.json 导出和旧结果失效；computer-use connector 返回 `unsupported Codex auth method: apikey`。真实相机、机器人可达性、碰撞/干涉和控制器导入仍需现场验收。

### 2026-10-06（离线 PLY + 亮度图纹理点云）

- 主模块 `60_geometry_workpiece` 新增 `pcv::texture::mapPlaneBrightness()`；当前契约输入现有点云、`planeIndices`、原始 PLY `sourceIndices`/点数和 `QImage`，输出等长灰度/有效标记、栅格尺寸、映射统计、取消和诊断。
- 映射固定为 `Format_Grayscale8`、忽略 alpha、不增强对比度、最近邻、等比中心裁剪；图像左→右对应局部 `+X`，顶部→局部 `Y` 最小值；不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON。
- `20_pointcloud_render` 增加 `PointColorOverlay` 校验、纹理灰度颜色模式和 GUI 线程 OpenGL 上传；非平面点/无效点回退原颜色模式，点云/平面/坐标系变化清理旧覆盖，切换颜色模式保留缓存。
- `apps/pointcloudview` 增加亮度图选择、后台映射、取消/revision 门禁、替换保护和状态栏统计；失败或取消不发布半成品。
- 验证：源码树外 Debug/Release 构建成功，双配置全量 CTest 均 `17/17`；纹理映射和渲染契约定向测试通过。
- 未验证：真实 PLY/亮度图 GUI 手工流程、视觉方向/中心裁剪效果、无 WObj1 现场方向和大点云性能；实时取流不在本轮范围。Release `windeployqt` 仍缺本机 Visual Studio release redistributable。

### 2026-10-06（严格 PLY 栅格索引亮度纹理修正）

- `pcv::texture::mapPlaneBrightness()` 改为严格原始 PLY `sourceIndex` 采样：原始点数必须等于亮度图像素数，点序固定为左上角起始的 row-major；仅 `planeIndices` 对应点接收灰度。
- `pointcloudview` 保存单个 PLY 的 `declaredPointCount`、`sourceIndices` 和转换过滤统计；过滤、降采样、拼接、文件夹 PLY 或无 provenance 点云禁用纹理入口并显示诊断。
- 纹理不再依赖 WObj1，WObj1 修改不清除纹理；点云/平面索引/source provenance 变化清除纹理；同一平面索引的边缘结果更新保留纹理。
- 不生成网格、UV、纹理文件、带颜色 PLY 或项目 JSON；未实现实时取流纹理接入。
- 最终验证：`C:\pcv-build\texture-raster-debug` 与 `C:\pcv-build\texture-raster-release` 构建成功，双配置 CTest 均 `17/17`；纹理定向测试、`pointcloudview` 自关闭启动冒烟和 `git diff --check` 均通过。真实 PLY/亮度图视觉验收、现场 row-major 一致性和实时取流仍未验证；Release `windeployqt` 缺少本机 Visual Studio release redistributable。

### 2026-10-08（PLY 原始坐标纹理实验入口）

- `apps/pointcloudview` 增加“PLY 纹理实验（原始坐标）”独立来源状态：单个 PLY 直接保留 XYZ、原始点序、`declaredPointCount` 和 identity `sourceIndex`，不经过手眼转换，不建立 WObj1，不生成临时工件四件套。
- 实验流程复用两组三点和同面校验；第一组三点使用任意平面方向的垂直距离拟合，第二组三点通过后才启用亮度图选择，复用严格 `source_raster_index` 映射。
- 新增 `tests/unit/pointcloudview/texture_experiment_tests_v1.cpp` headless 契约测试，并在 `tests/CMakeLists.txt` 注册；覆盖声明点数、source index、垂直平面、同面校验、严格 row-major 映射、取消和尺寸不匹配拒绝。
- 本轮 Debug 构建和全量 CTest `18/18` 已通过；Release 构建/CTest、真实 PLY/亮度图 GUI 视觉验收、现场 row-major 一致性和实时取流仍需验证。

### 2026-10-08（扫描规划单线容差修复）

- 主模块 `60_geometry_workpiece`；扫描线判定增加模块内固定 `0.1 mm` 容差，投影宽度 `<= cameraWidthMm + 0.1 mm` 时只生成 `P0/P1`，超过阈值才按实际覆盖要求生成多条线。
- 结果 DTO 增加 `singleLineWidthToleranceMm`，内部 `scan_trajectory_v1` 增加 `single_line_width_tolerance_mm`；保留原始宽度和单线重叠字段兼容值，不新增 UI 参数，不改变 InoRobot P.json 契约。
- 回归覆盖 Global/LocalRoi 单线/超容差多线边界、独立段数、P.json 点数和编号；真实相机有效视野、ROI 边界及现场机器人行为仍需验收。
- 验证：源码树外 Debug/Release 完整构建成功，全量 CTest 双配置均 `18/18`；扫描定向 Debug 测试通过，双配置全量测试均包含扫描回归；双配置 `pointcloudview` 自关闭启动冒烟退出码 `0`，`git diff --check` 通过。未使用真实 ROI 做 GUI 复验；Release `windeployqt` 仍缺本机 Visual Studio release redistributable。

### 2026-10-08（三点平面拟合等价提速 v2）

- 主模块 `60_geometry_workpiece`，UI `apps/pointcloudview`；保留渲染算法、手眼转换、阈值、300 轮 RANSAC、随机种子、20000 点模型采样上限和两组三点流程。
- `Options` 追加 `parallelEnabled=false`、`maxParallelTasks=4`，任务数限制为 1～4（含协调线程）；`Result` 追加 `cancelled` 和 `StageTimings timings`，记录候选筛选、RANSAC、PCA、分类、连通域、组装及总耗时，既有 JSON/PLY 输出格式不变。并行取消回调必须可并发调用。
- RANSAC 依原序生成候选、评分只计数、数量相同保留最早模型；逐点分类按连续输入区间分配并按输入顺序合并。worker 仅读取 const Qt 容器、返回私有输出，全部任务汇合后才发布；PCA/RMS 累加保持串行和原浮点顺序。
- 连通域保留原投影、自动半径、九邻格与精确距离判定；栅格只保留未访问点，发现后移除，每格预缓存九邻格指针以减少重复查找；连通域编号及最终索引仍按输入顺序生成。
- wrapper 仅透传参数、取消和诊断；UI 在点数不少于 100000 时启用最多四个任务，输入 revision 变化、关闭和析构先取消，再等待 worker。日志记录三点索引、阶段时间、GUI 发布时间及下一次 `frameSwapped` 的总耗时；自动 PLY/原始纹理流程从第三点选中开始计时，手动入口从执行拟合开始计时。
- 新增 `plane_fitting_equivalence_tests_v2`：密集/重复/不均匀密度、断开和临界连接、噪声倾斜、垂直、控制点跨域、非法输入与 deferred 路径；保留额外共享副本，对比串行与 1/2/4 任务完整结果，覆盖取消及 join 后生命周期。原算法 Debug/Release 生成的完整字段黄金摘要用于同工具链严格回归。
- 基准、输入副本、优化前源码/程序和完整结果位于 `C:\pcv-build\fitbench-baseline`；6300000 点原始 PLY 的 SHA256 为 `BE0552A1ADD1D64208F53B3D0F460CF9AC6BA3087EF332998D91F22F4AF246A1`。固定测试组覆盖上表面不同位置和低平面，每组预热一次、正式五次，不计读取/转换/结果序列化耗时。
- 验收尚未完成：用户确认实际控制点和 Start/End 参数未确定，暂不能保存真实 robot_base 测试输入或验收临时工件 PLY/JSON GUI。模块性能不能替代从第三点到完整显示的 GUI 验收；最终构建、CTest、性能数据及风险以本轮最终验证记录为准。

### 2026-10-08（PointCloudStitch 位姿粗配准＋重叠区快速精配准）

- 主模块 `30_pointcloud_stitch`；保留 `Legacy` 完整配准和仅位姿直拼入口，新增 `RegistrationProfile::PoseOverlapFine`。新模式以模块 50 的 `robot_base` 位姿为初值，仅在相邻帧机器人初值重叠区执行两层受限 point-to-plane ICP（体素 `0.5/0.2 mm`、对应距离 `3.0/0.6 mm`、迭代 `20/15`），每对修正预算 `10 mm / 1°`。
- 新模式跳过旧平面跟踪、切向/宽范围搜索、恢复重试、2.5D 和 legacy 双跑；仅更新可观测方向，退化、低覆盖、结构不足和无有效解按警告处理。无重叠、超预算或取消时回退机器人位姿或终止任务，按扫描顺序累计 pair 矩阵；不混用已修正目标，不重复累计。
- 新模式默认关闭 seam fusion 与 preview，保留全量有效点、`cloudIds`、`sourceIndices`、`scanRatios` 和事务化正式输出。manifest/诊断记录 `fine_applied`、`pose_fallback`、层数、对应数量、残差、退化状态、pair/cumulative matrix 及累计漂移；`formal_output` 只表示文件事务提交成功，质量策略为 `warn_only`。
- GUI 默认选择 `PoseOverlapFine`，公共 API 默认仍为 `Legacy`；`regressionrunner` 增加 `--pose-overlap-fine` 和 `--interface-pipeline`。未修改手眼矩阵、坐标约定、窗口生命周期、原始输入或提交/推送行为。
- 本轮源码树外构建目录：`C:\pcv-build\pose-overlap-fine-debug-v1`、`C:\pcv-build\pose-overlap-fine-release-v1`，生成器 `NMake Makefiles`，MSVC `19.51.36252.0`，Qt `C:\Qt\6.8.3\msvc2022_64`。Debug/Release 定向构建已成功；Debug 定向 `stitching_interface_tests`、`seam_fusion_tests`、`multiframe_registration_tests`、`pose_overlap_fine_tests` 已通过，100 次重复回归正在/已执行，以最终命令结果为准。
- 结构平面路径补充修复 `structuralOverlapPlane()` 返回结构平面时初始化 `axisU/axisV`，避免局部重叠范围退化为 `[0,0]`。测试覆盖水平、`30°/60°/75°`、纯平面可观测自由度、链累计、回退、取消、预算、低覆盖警告及串并行等价。
- `D:\stitch_test` 当前可见 `hand_eye_convert.xml` 和 PLY，但缺少可安全重放所需的 Start/End 位姿 JSON/TXT 或历史报告；因此 `1/test_1`、`5_40/test`、`6_50/test` 及真实倾斜夹具本轮未作真实回放，不以合成测试替代现场结论。GUI 人工拼接、真实输出质量、PageHeap 和 Release 便携部署仍需补验；Release `windeployqt` 可能受本机 Visual Studio release redistributable 缺失影响。

### 2026-10-09（全仓 Markdown 整理与文档职责校准）

- 本轮仅修改文档；正式需求与状态基线新增 `docs/requirements/pointcloudview_v0.4.md`，模块/UI 协同规则仍在 `docs/architecture/module_ui_integration_v0.4.md`，既有数据契约保留 v0.3。
- 新建 10/60 规范模块文档；旧 05/10/60/70/80 文档保留正文并作为历史/兼容索引。开发方案见 `docs/development/module_convergence_v0.4.md`；所有代码更新必须同步对应模块文档和 `target.md`，记录日期、文件/API、行为、实际验证及风险。
- 当前 namespace/API 迁移未完成：工具和 registrationdialog 仍调用旧拼接入口，分组请求内部仍适配旧实现。扫描规划、严格栅格纹理、快速拼接的当前能力按源码写，40/90/100 仍为未实现业务占位。
- 验证：51 份 Markdown 内容核对、161 个本地链接目标、一级标题、代码围栏和替换字符检查通过，未发现锚点链接；源码与 CMake 静态核对及 `git diff --check` 完成。本轮不运行构建、CTest 或 GUI，不引用历史测试计数作为本次通过。
