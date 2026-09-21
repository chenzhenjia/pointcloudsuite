# 60_geometry_workpiece：工件几何

状态：已实现，兼容入口保留。规范实现位于 `modules/60_geometry_workpiece/{include,src}`，模块 target 为 `pcv_m60_geometry_workpiece`；`pcv_m60_plane_fitting`、`pcv_m60_planefitting` 和 `pcv_m70_roi_template` 仅转发。职责包括平面拟合与诊断、平面/边缘几何、ROI、WObj 和临时工件流程；正式文件通过 10 的事务 API 提交。应用入口仅负责参数转换、生命周期和结果映射。本文件文件名保留旧名称以避免历史链接失效。

接口/测试：`pcv::geometry`、`pcv::workpiece`，以及兼容的 `pcv::planefitting::fit`；`plane_fitting_tests`、`roi_matching_tests`、`temp_workpiece_interface_tests` 和几何回归测试。

最后核对日期：2026-09-21。纯算法实现不得依赖 `apps/` 或 Qt Widgets。本文件历史正文中的旧名称与旧职责为当时状态，仅用于追溯。

## 变更记录

### 2026-09-21（临时工件自动链失败恢复）

- 应用层统一管理 `None/Json/Ply` 临时工件会话；参数取消、旧会话替换和画布版本变化不会复用过期 source index 或覆盖策略。
- 自动 WObj1、边缘分割、ROI/Mask 和四件套前置失败统一进入 `Failed`，保留已完成点云/几何结果并允许重试；主动放弃取点保留会话和转换点云，重新取点即可继续。
- 取消错误码按发生阶段保持不变，`cancelled=true` 为取消权威标志：PLY 读取为 `PCV_INPUT_002`，转换为 `PCV_TRANSFORM_001`，调用前或 finalize 取消为 `PCV_CANCELLED`；文档不再声称所有取消均为 `PCV_TRANSFORM_001`。
- 验证：源码树外 Debug/Release 全量构建成功，Debug/Release CTest 均为 `15/15` 通过；GUI smoke 和真实样件仍待验证。

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
