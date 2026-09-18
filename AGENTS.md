# PointCloudSuite Agent 工作手册

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
