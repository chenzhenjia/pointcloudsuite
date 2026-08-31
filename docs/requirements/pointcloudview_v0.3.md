# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
日期：2026-08-31  
状态：活动基线

## 1. 目标

v0.3 将当前 PointCloudSuite 统一为企业级模块化项目。源码、CMake target、测试和文档按十个稳定编号治理；已有 v0.2 功能作为实现基线，后续需求必须可追溯到模块、接口、测试和变更记录。

当前事实来源仍为源码、CMake 和测试。v0.1/v0.2 文档只作历史参考。

## 2. 模块基线

| 编号 | 模块 | 当前实现映射 | 状态 |
|---|---|---|---|
| `10_pointcloudread` | 点云读写 | `src/io`、`include/pcv/io`、`pcv_io` | 已实现，迁移中 |
| `20_pointcloudrender` | 点云渲染 | `apps/pointcloudview` Qt/OpenGL | 部分实现 |
| `30_pointcloudstitch` | 点云拼接 | `apps/pointcloudstitch` 流程与接缝 | 部分实现 |
| `40_pointcloudregistration` | 点云配准 | `src/registration` ICP/结构验收 | 已实现，边界收敛中 |
| `50_coordinateconversion` | 坐标转换 | `src/registration` 手眼和位姿变换 | 已实现，迁移中 |
| `60_planefitting` | 平面拟合 | `apps/pointcloudview/pointcloudprocessor.*` | 已实现，抽取中 |
| `70_roi_template` | ROI/模板 | `pcv_interface` 与 pointcloudview 平面映射 | 部分实现 |
| `80_planeoutput` | 工具平面输出 | `src/output`、`pcv_output` | 已实现，迁移中 |
| `90_interferenceplane` | 干涉平面检查 | 暂无独立实现 | 未实现 |
| `100_qualityreport` | 质量报告 | 暂无独立实现 | 未实现 |

模块目标目录为 `modules/<编号>_<名称>/`，每个已实现模块包含 `include/`、`src/`、`tests/`、`CMakeLists.txt` 和 `README.md`。迁移期间保留现有目录与兼容 target。

## 3. 处理流程

标准流程为：输入契约校验 → `10` 读取/缓存 → `20` 渲染与点选 → `50` 坐标转换 → `60` 平面拟合 → `70` ROI/模板 → `80` 成套输出。多帧场景由 `30` 编排并调用 `40` 配准。`90`、`100` 未实现时不得生成伪成功结果。

## 4. 跨模块契约

- 点坐标单位为 mm；公共位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`；坐标转换必须声明源坐标系和目标坐标系。
- 正式平面输出为 Grayscale8 PNG（前景 255、背景 0）、binary little-endian XYZ PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- 输出文件先写入 staging 目录，PNG/PLY/JSON 全部成功后一次性提交；任一失败必须回滚。
- 共享模块不得依赖应用；共享算法不得依赖 Qt Widgets；运行数据不得写入源码树。

## 5. 代码与变更治理

每次需求登记一个主模块编号。修改公共头文件、CMake 或跨模块接口时，必须列出影响模块和回归测试。模块文档使用追加式日志，记录日期、需求 ID、修改文件、行为变化、验证命令、结果和未验证风险。

## 6. 开发路线

1. M0：已完成，建立 v0.3、十个模块文档、`AGENTS.md` 规则和需求追踪。
2. M1：已完成，建立 `modules/` 目录、统一 target 命名和现有实现映射。
3. M2：已完成 `10`、`50`、`80` 的真实源码迁移；`60` 保留处理器兼容入口。
4. M3：已完成 `30` 接口编排与 `40` 配准算法拆分，应用入口已切换模块 target。
5. M4：已完成 `70` ROI/模板接口抽取；`20` 渲染仍保留在 Qt 应用边界，待独立渲染组件需求明确后迁移。
6. M5：进行中。兼容 target/forwarding header 暂保留，待全部调用方切换到模块化头路径后清理。
7. M6：根据实际业务需求启动 `90` 干涉平面检查和 `100` 质量报告。

## 7. 验收门槛

文档变更执行 `git diff --check` 和 `git status --short`。代码迁移执行 `scripts/build_windows.ps1 -Config Debug -BuildTests`、`scripts/run_tests.ps1 -BuildDir C:\qt-build-pointcloudsuite` 和全量 CTest；涉及公共头、CMake、处理器或输出契约时必须完整重编译。GUI 点击、真实生产数据和 `90`/`100` 功能在未验证前明确标记为未验证或未实现。

## 8. 变更记录

### 2026-08-31

- 建立 v0.3 活动基线，固定十个模块编号、代码迁移策略、标准流程和验收门槛。
- 建立模块文档与 `modules/` CMake 兼容入口；未移动现有用户修改。

### 2026-08-31（M2-M4 迁移）

- `10_pointcloudread`：PLY 读写、缓存和 writer 源码迁移到 `modules/10_pointcloudread`。
- `40_pointcloudregistration`：多帧配准与接缝融合迁移到 `modules/40_pointcloudregistration`。
- `50_coordinateconversion`：手眼标定和线扫坐标转换迁移到 `modules/50_coordinateconversion`。
- `70_roi_template`：临时工件/ROI 接口迁移到 `modules/70_roi_template`。
- `80_planeoutput`：平面输出契约迁移到 `modules/80_planeoutput`。
- `30_pointcloudstitch`：stitching 接口迁移到 `modules/30_pointcloudstitch`。
- 旧 `pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output` 保留为兼容聚合 target；旧公共头保留 forwarding header。
- Debug 构建与 CTest `12/12` 通过。

### 2026-08-31（M5.1 首轮）

- 共享 seam fusion 增加公共类型头和 kernel 适配入口，应用层 `seamfusion.cpp` 改为转发到共享实现。
- 共享实现支持真实投影重叠检查、接缝方向校验、融合带处理、候选最近邻插值及失败保护；新增真实重叠回归用例。
- 当前仍保留应用级 `mergePlyCloudsInWorld()`/`registerPair()`，因此 M5.1 的“单一配准流水线”尚未完成；`60_planefitting` 与 `20_pointcloudrender` 尚未开始抽取。
- 使用可写外部构建目录完成 `pointcloudview`、`pointcloudstitch` Debug 构建和 CTest `12/12`。

### 2026-08-31（M5.2 首轮）

- 新增 `pcv_m60_planefitting` 静态库和 `pcv::planefitting::fit` 基础 API，覆盖控制点校验、初始平面、容差分类和结果诊断。
- `pcv::planefitting::fit` 已可独立构建和测试；`pointcloudview` 的 `extractPlaneFromPoints` 暂继续使用完整 legacy 算法，以保留 RANSAC/PCA 细化、连通域和诊断字段，待字段完整迁移后再切换。
- 新增独立 `plane_fitting_tests`，应用级几何回归继续保留用于兼容性验证。

### 2026-08-31（M5.3 首轮）

- `pcv_m20_pointcloudrender` 已从 `INTERFACE` 改为静态库，新增 `RenderSnapshot`、版本号和点状态校验契约。
- 新增 `pointcloud_canvas_contract_tests`；`PointCloudCanvas` 的 Qt/OpenGL 实现仍保留在 `mainwindow.cpp`，待完成 context、FBO 和异步生命周期抽取后再迁移。
- `PointCloudCanvas` 已在应用入口接入快照校验和渲染版本递增；实体迁移仍需先完成公共渲染 DTO 下沉。
- 已新增 `pcv::render::CoordinateFrame`、`pcv::render::Contour` DTO，应用显式转换后调用画布；该步骤解除渲染实体迁移对 processor 类型的直接依赖。
- `stitchRawLineProfiles` 已解除 seam fusion 占位禁用；无真实投影重叠时按 fail-closed 契约保留点云并输出诊断，真实重叠融合失败仍阻止正式输出。

### M5.4 兼容层审计（2026-08-31）

- 当前仍有生产代码直接编译 `apps/pointcloudstitch/pointcloudprocessor.cpp`，其中保留独立 `registerPair()`/`mergePlyCloudsInWorld()`，因此 `40_pointcloudregistration` 尚未成为应用配准唯一事实来源。
- `tools/registration_diagnostic`、旧 forwarding header、`pcv_registration`/`pcv_io`/`pcv_output` 聚合 target 仍存在引用；不得删除。
- `20_pointcloudrender` 已具备独立 DTO 和快照契约，但 `PointCloudCanvas` 实体仍位于 `mainwindow.cpp`；需完成真实 OpenGL 组件迁移及 GUI 验收后再进入清理阶段。
- `40_pointcloudregistration` seam fusion 已在帧扫描、融合带构建和最近邻插值阶段加入周期性取消检查；取消时保持输入映射和正式输出不变。
- `20_pointcloudrender` DTO 契约新增坐标系与轮廓非有限值校验，画布入口在发布前 fail-closed；实体迁移和真实 GUI 验收仍未完成。
- seam fusion 已修正未匹配点处理：融合带内未获得互相最近邻的点保留原始来源索引和 scan ratio，并在诊断中计数。
- 应用 seam wrapper 已切换为共享 `SeamFusionOptions`、`SeamFusionResult`、`SeamFusionDiagnostic` 类型别名，减少跨模块结果模型漂移；旧包装函数仍保留兼容。
- 应用拼接处理器头已移除重复 ICP/结果结构，直接复用 `pcv_m40_pointcloudregistration` 公共模型；`mergePlyCloudsInWorld()` 仍作为兼容入口保留。
- `WorldCloudInput` 已下沉至 `pcv::stitching` 纯数据 DTO，应用保留 `pointcloud::WorldCloudInput` 兼容别名；配准结果结构仍待后续迁移。

### 2026-08-31（M5.3 画布实体迁移）

- `PointCloudCanvas` 已移入 `modules/20_pointcloudrender`，应用源文件不再包含画布类实现；模块依赖方向保持为 `apps -> pcv_m20_pointcloudrender`。
- 本阶段未改变 OpenGL 绘制、点选、FBO 或异步发布行为；真实 GUI 交互和关闭烟测仍是发布前置条件。
- 公共画布头迁移已完成 Debug/Release 双配置构建和 CTest 验证；GUI 真实交互验收仍未完成。
- `60_planefitting` 公共契约已补齐完整拟合所需选项和诊断字段，为后续逐字段迁移 RANSAC/PCA 与连通域算法做准备；当前应用仍使用兼容实现。

### 2026-08-31（UI 目录分离）

- `apps/pointcloudview/ui/mainwindow.ui` 和 `apps/pointcloudstitch/ui/stitchingwindow.ui` 作为独立 Qt Designer 文件保存。
- CMake 已改为显式引用 `ui/*.ui`，保留 `ui_mainwindow.h`/`ui_stitchingwindow.h` 的 AUTOUIC 生成方式；UI 文件可直接用 Qt Designer 编辑。

### 2026-08-31（M5.1 输入 DTO 下沉）

- 拼接输入纯数据类型已归属 `pcv::stitching`，应用通过兼容别名接入；本阶段未改变 PLY、位姿或输出契约。

### 2026-08-31（M5.2 平面拟合算法迁移）

- `pcv_m60_planefitting` 已成为平面拟合算法的生产调用入口，覆盖 PCA 初始平面、RANSAC、PCA 细化、连通域筛选、预览延迟分类和诊断字段。
- `apps/pointcloudview/pointcloudprocessor.cpp` 的 `extractPlaneFromPoints` 仅执行 `ThreePointPlaneOptions -> pcv::planefitting::Options` 和结果字段映射；二维图像映射、ROI/Mask 和 UI 状态仍保留在应用侧。
- 验证：Debug 构建 `pointcloudview`、`pointcloudstitch` 和 `plane_fitting_tests` 成功；完整 Debug CTest `14/14` 通过。
- 风险：旧 `extractPlaneFromPointsLegacy` 函数仍在处理器源文件中保留，尚未删除；删除需单独提交并重新验证直接编译该处理器的工具和测试。

### 2026-08-31（M5.1 拼接配准流水线收敛）

- `apps/pointcloudstitch/pointcloudprocessor.cpp` 仍负责读取 PLY、手眼转换和来源索引生成；转换后的 `RobotBaseFrame` 统一交给 `registerRobotBaseFrames()` 执行 ICP、重叠诊断和结果拼装。
- 应用层旧 `registerPair()` 及后续拼接代码保留在兼容源中但不再执行，待工具和回归夹具完成迁移后单独删除。
- Debug 构建和完整 CTest `14/14` 通过；未执行真实 GUI/生产夹具验收。

### 2026-08-31（M5.4 发布审计阶段）

- Debug 与 Release 构建均已完成，两个配置的完整 CTest 均为 `14/14`。
- `rg` 审计确认 `pcv_io`、`pcv_registration`、`pcv_output`、旧 processor 源和 forwarding header 仍有兼容引用，因此本阶段不删除兼容层。
- `90_interferenceplane`、`100_qualityreport` 继续保持“未实现”。
- 发布候选仍未达成：缺少真实 GUI 点选/矩形选区/视角交互、生产拼接夹具逐字段比对，以及旧算法兼容代码清理评审。
