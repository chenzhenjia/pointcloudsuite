# PointCloudSuite Agent 工作手册

适用范围：本仓库。沟通默认使用中文；代码、命令、变量名、文件名、路径和错误信息保留原文。最后核对日期：2026-09-07。

## 1. 真相源与安全边界

- 当前源码、CMake 和测试是事实来源；历史文档只用于追溯。
- 只修改用户明确指定的文件和模块，保留其他未提交修改。
- 删除、覆盖、批量重命名、外部上传、账号或数据库操作前必须获得明确授权。
- 禁止写入 password、secret、token、API key、private key。
- 缓存、日志、导出物和运行数据不得写入源码树。
- Windows 源码、构建、输入和输出路径支持 Unicode；构建目录仍应放在源码树之外。
- 不把 build*/、test_pointcloud_a/、mid_gap/ 或实验报告当作稳定 API。
- 不使用 `git reset --hard`、`git checkout --` 等破坏性命令回退用户修改。

## 2. 当前项目基线

- 项目基线：PointCloudSuite v0.3；当前工作区已完成规范模块目录迁移和 30/40 职责重划，发布验收仍未完成。
- 语言和工具：C++17、CMake 3.19+、Qt 6.5+、Windows/MSVC。
- 根选项：
  - `PCV_BUILD_POINTCLOUDVIEW`
  - `PCV_BUILD_POINTCLOUDSTITCH`
  - `PCV_BUILD_CLI`（默认 `OFF`，生成独立的 `pointcloudcli` 参数入口）
  - `PCV_BUILD_TOOLS`
  - `PCV_BUILD_TESTS`
  - `PCV_BUILD_EXPERIMENTAL_CAMERA`（CMake 默认 `ON`；构建脚本默认关闭）
- 外部算法依赖：不依赖 Python、Open3D、PCL、VTK 或 CUDA。
- 当前统筹需求文档：`docs/requirements/pointcloudview_v0.3.md`。
- 模块详细记录：`docs/modules/<module>.md`。

## 3. 模块与 target

规范模块及唯一职责：

| 编号 | 模块 | 规范目录 | 规范 target |
|---|---|---|---|
| 10 | 点云读写与相机帧输入 | `modules/10_pointcloud_read` | `pcv_m10_pointcloud_read` |
| 20 | 点云渲染 | `modules/20_pointcloud_render` | `pcv_m20_pointcloud_render` |
| 30 | 点云拼接与 seam fusion | `modules/30_pointcloud_stitch` | `pcv_m30_pointcloud_stitch` |
| 40 | 点云匹配与配准底层能力 | `modules/40_pointcloud_matching` | `pcv_m40_pointcloud_matching` |
| 50 | 坐标转换 | `modules/50_coordinate_conversion` | `pcv_m50_coordinate_conversion` |
| 60 | 平面拟合 | `modules/60_plane_fitting` | `pcv_m60_plane_fitting` |
| 70 | ROI/模板 | `modules/70_roi_template` | `pcv_m70_roi_template` |
| 80 | 工具平面输出 | `modules/80_plane_output` | `pcv_m80_plane_output` |
| 90 | `90_interferenceplane` 干涉平面检查 | 仅文档占位 | 未实现 |
| 100 | `100_qualityreport` 质量报告 | 仅文档占位 | 未实现 |

迁移期兼容 target 包括旧模块名 `pcv_m05_camerainput`、`pcv_m10_pointcloudread`、
`pcv_m20_pointcloudrender`、`pcv_m30_pointcloudstitch`、`pcv_m40_pointcloudregistration`、
`pcv_m50_coordinateconversion`、`pcv_m60_planefitting`、`pcv_m80_planeoutput`，以及
`pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`。它们只能做 target/header
转发，不得新增与规范模块重复的实现；只有在所有调用方迁移、弃用周期结束并完成独立
评审后才能删除。

## 4. 依赖方向和代码边界

- 共享算法放在模块 `src/`，公共头放在模块 `include/`。
- 共享模块不得依赖 `apps/`。
- `20_pointcloud_render` 是唯一允许依赖 Qt Widgets/OpenGL 的共享模块；其他算法模块不得反向依赖 UI。
- `30_pointcloud_stitch` 是唯一拼接编排边界，负责输入读取、坐标转换/匹配调用、seam fusion、诊断、正式输出和失败回滚。
- `40_pointcloud_matching` 负责平面/ROI 点匹配、覆盖校验、`robot_base` 完整回传，并保留 ICP、相邻帧配准和结构校验底层能力；seam fusion 的实现与公共类型归属 30。
- `50_coordinate_conversion` 是坐标转换唯一数学实现来源；`60_plane_fitting` 只负责纯几何拟合和诊断。
- `70_roi_template` 的 ROI 匹配必须调用 `pcv::matching::matchRoiToRobotBase`，不得保留第二套匹配实现。
- `modules/10_pointcloud_read` 同时承载原 `05_camerainput` 的相机帧转换、Mock、组帧和可选 Mv3dLp 实现；相机能力受 `PCV_BUILD_EXPERIMENTAL_CAMERA` 控制。
- `20_pointcloud_render` 的 `PointCloudCanvas` 实现位于 `modules/20_pointcloud_render/src/pointcloud_canvas.cpp`；应用只负责业务状态、异步生命周期和渲染 DTO 转换。
- Qt Designer 文件位于 `apps/pointcloudview/ui/` 和 `apps/pointcloudstitch/ui/`，不得把生成头文件提交为手工 UI 源。
- `pointcloudcli` 仅提供 `--help`、`--version` 和参数错误契约，不接入点云业务或 Qt。
- `registration_diagnostic` 仅通过 `pcv::interface::stitchRawLineProfiles` 调用统一拼接流水线。
- 应用 wrapper 只能做参数转换和结果映射，不得保留第二套算法。

## 5. 稳定契约

修改公共类型、输出格式、坐标约定或错误码前，先阅读 v0.3 需求文档和对应模块文档。

- PLY 读写：按属性名读取坐标和法向，支持 ASCII、binary little-endian、binary big-endian；保留点序、source index、bounds、取消和错误语义。
- 坐标转换：控制器位姿顺序为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`；旋转约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 配准和拼接：多帧流程使用统一的 `registerRobotBaseFrames()` 与 `applyTrajectorySeamFusion()`；ROI 流程使用 `pcv::matching::{RoiMatchInput,RoiMatchResult,matchRoiToRobotBase}`。取消或失败时不得发布部分结果或复用旧正式输出。
- 长流程统一使用 `pointcloud::CancellationToken` 或等价取消回调；取消结果必须带诊断，不得当作成功结果发布。
- seam fusion 默认值：`halfWidth=8.0f`、`mutualDistance=0.6f`、`decisionCellSize=0.5f`。
- 平面拟合：拒绝重复、越界、非有限和共线控制点；保留 RMS、内点、阈值、planarity、连通域和取消诊断。
- 渲染：仅 GUI 线程修改 QWidget/OpenGL；拒绝点、轮廓、坐标系中的非有限值和越界索引。
- 平面输出：PNG、PLY、JSON 必须成套提交；任一写入失败整体失败并回滚。PNG 使用 `Grayscale8` 和 0/255；平面 PLY 使用 binary little-endian。
- 共享库不弹窗、不访问 QWidget；通过 Result 状态、错误码、取消标记和诊断返回结果。
- 应用层负责用户提示、窗口生命周期和线程结果发布。

详细字段、JSON schema、错误码和测试矩阵以：
`docs/requirements/pointcloudview_v0.3.md`、`docs/modules/<module>.md`、公共头文件和测试源码为准。

## 6. 单模块开发流程

每个需求必须先登记主模块编号：

1. 执行 `git status --short`，记录已有工作区修改。
2. 阅读目标模块文档、公共头、CMake 和相关测试。
3. 设计或确认接口，列出受影响模块和依赖方向。
4. 只修改目标模块及必要的跨模块接口、测试和文档。
5. 先运行目标模块测试，再构建直接依赖和应用。
6. 涉及公共头、CMake、processor、UI 或输出契约时执行完整 Debug 构建和全量 CTest；发布相关改动补充 Release。
7. 更新 `AGENTS.md`、`docs/modules/<module>.md` 和 v0.3 需求追踪，记录日期、需求、文件、行为、验证和风险；目录或 target 迁移时同步更新索引、架构和历史说明。
8. 提交前执行 `git diff --check` 和 `git status --short`。

跨模块修改必须在提交说明中列出影响模块、依赖变更和回归测试。未实现模块不得写入功能成功记录。

## 7. 构建与验证

Windows + Qt/MSVC 推荐使用仓库脚本，并显式传入可写、位于源码树外的构建路径：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
.\scripts\build_windows.ps1 -Config Release -BuildTests -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
```

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
和实时 pipeline 测试。验证结论只报告本次实际运行的命令和结果；GUI 人工操作、真实生产
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
