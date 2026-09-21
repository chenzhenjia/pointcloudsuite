# PointCloudSuite v0.4 模块与 UI 交接中间文档

状态：架构冻结草案（2026-09-17）。本文件是模块重整、UI 同事接入和后续交接的协同真相源；源码、CMake、公共头和测试优先级高于历史文档。本轮优先完成开发计划与文档同步，按用户要求暂不运行 CTest。

## 1. 六模块职责与数据流

| 模块 | 规范目录/target | 职责 | 依赖 |
|---|---|---|---|
|10 pointcloud_io|`modules/10_pointcloud_io` / `pcv_m10_pointcloud_io`|PLY/缓存、相机输入、过滤、PNG/PLY/JSON writer、`ArtifactTransaction`|core/infrastructure/Qt Core+Gui|
|20 pointcloud_render|`modules/20_pointcloud_render` / `pcv_m20_pointcloud_render`|OpenGL、画布、点选、渲染 DTO|core|
|30 pointcloud_stitch|`modules/30_pointcloud_stitch` / `pcv_m30_pointcloud_stitch`|多帧拼接编排、ICP、多帧配准、seam fusion、诊断、manifest|10/50|
|40 pointcloud_matching|`modules/40_pointcloud_matching` / `pcv_m40_pointcloud_matching`|规划中的工件位置确认占位；本版不实现移动判定|30/60（兼容聚合）|
|50 coordinate_conversion|`modules/50_coordinate_conversion` / `pcv_m50_coordinate_conversion`|手眼标定、位姿插值、坐标变换唯一实现|core|
|60 geometry_workpiece|`modules/60_geometry_workpiece` / `pcv_m60_geometry_workpiece`|平面拟合、平面/边缘几何、ROI、WObj、临时工件|10/50|

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
- 30/60 只生成领域结果和 manifest；正式文件由 10 事务提交。取消不得发布部分结果或复用旧正式输出。

## 5. 文件所有权白名单

主线维护：`modules/**`、`src/**`、`apps/pointcloudview/mainwindow.*`、根 CMake、稳定公共头、模块测试和本文件。

UI 同事限定：`apps/pointcloudview/features/**`、`apps/pointcloudview/ui/` 中经交接登记的 UI 专属 `.ui`、UI 专属资源/样式文件、UI 测试与交互说明。主线持有 `MainWindow` 壳、生命周期、模块依赖和稳定 API。现有 `pointcloudstitch` 视觉本轮不纳入统一。

临时 adapter 只能位于 `apps/pointcloudview/adapters/**`，文件头必须标记 `Experimental` 或 `Deprecated`，并注明 owner、创建日期、替代 API、删除条件和截止版本；不得成为模块间长期依赖。

## 6. UI 与刀路规划接入边界

新页面采用独立 feature + adapter；具体 QWidget/QDialog 形态由 UI 同事冻结后补充。后端只提供版本化 DTO、错误码、取消语义和只读查询接口；主线不实现刀路规划算法，不冻结尚未确定的刀路 JSON schema。UI 接入不得直接包含 `modules/*/src` 或调用应用 processor/seam fusion/PLY writer。

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

验收门禁（后续执行）：Debug/Release 新构建目录可配置；全量 CTest；`pointcloudview` 主窗口和新页面 GUI smoke；JSON/PLY schema、文件名、坐标约定、错误码和默认数值不变；高倾角配准基线判定及漂移在预算内。真实相机、机器人夹具、40 移动判定和刀路算法明确标记为未验收。本轮没有执行 CTest，不得把历史测试结果当作本轮通过。

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

## 10. 已知问题与回滚点

- 当前工作区原有用户未提交修改作为迁移基线保留；不执行远程提交、上传、删除旧兼容层或真实机器人联调。文件数量以实时 `git status --short` 为准，不在文档中冻结。
- 既有构建目录权限不足，本轮使用新的源码树外可写目录；GUI smoke 和真实夹具未在本次自动化验证中执行。
- 回滚以本文件映射表和三批迁移边界为准：优先恢复对应模块目录/CMake/转发 header，保留用户原有 8 个文件，不使用破坏性 reset/checkout。
