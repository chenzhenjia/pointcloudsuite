# 30_pointcloudstitch：点云拼接

状态：流程模块已实现，应用级 UI 适配保留。规范实现位于 `modules/30_pointcloudstitch`，模块 target 为 `pcv_m30_pointcloudstitch`；配准算法归属 `40_pointcloudregistration`。

接口：`pcv::interface::stitchRawLineProfiles`。测试：`stitching_interface_tests`。应用只负责输入组织、进度、窗口状态和结果展示。

最后核对日期：2026-09-04。

### 2026-09-04（元数据补全）

- `stitchRawLineProfiles` 路径为 `RobotBaseFrame` 填入 `flangeFromDepth = calibration.flangeFromDepth`，配合模块 40 使报告/诊断记录真实标定矩阵。
- 验证：未编译；待 Debug 构建与 `stitching_interface_tests`。

### 2026-08-31（M5.1 seam pipeline）

- M5.4：`registration_diagnostic` 已改用 `pcv::interface::stitchRawLineProfiles`，工具不再编译应用 processor；固定四帧输入、no-seam/seam 两套输出均由同一接口流水线执行。
- 验证：工具 Debug 构建通过；真实夹具运行和 GUI/接口逐字段比对待现场数据。

- 移除 `seamEnabled` 的“暂时禁用”入口占位判断，使 `stitchRawLineProfiles` 可以调用共享 seam fusion。
- 无真实投影重叠时允许 fail-closed 保留完整点云并返回诊断；真实重叠但无可用互相对应点时仍返回 `PCV_STITCH_001`，禁止生成正式输出。
- 验证：接口错误安全测试覆盖启用 seam 时的非法 PLY 输入和正式输出不被替换。
- 应用 seam 适配器已改用 `pcv_m40_pointcloudregistration` 的共享结果类型别名，保留旧包装入口但移除第二套数据模型。
- `stitchingwindow.ui` 已移动至 `apps/pointcloudstitch/ui/`，CMake AUTOUIC 继续生成 `ui_stitchingwindow.h`。
- 新增 `pcv::stitching::WorldCloudInput` 纯数据 DTO；应用 `pointcloud::WorldCloudInput` 改为兼容别名，建立拼接输入类型的模块所有权。

### 2026-08-31（M5.1 输入 DTO 下沉）

- `WorldCloudInput` 已移入 `modules/30_pointcloudstitch/include/pcv/stitching/world_cloud_types.h`。
- 应用头仅保留 `pointcloud::WorldCloudInput` 兼容别名；`pointcloudstitch` Debug 构建和完整 CTest（14/14）通过。

接口/测试：`stitching_interface`、`applyTrajectorySeamFusion`；`stitching_interface_tests`、`seam_fusion_tests`。

### registration_diagnostic 通用输入

工具支持 `registration_diagnostic stitch`（省略子命令时保持兼容）以及 `inspect`。拼接可重复传入 `--input <file>`，或使用 `--input-dir <directory>` 扫描当前目录 `*.ply`；两者可混用并按规范化绝对路径去重。每个文件名需包含 `A<n>` 扫描编号，并通过 `--pose-info` 的 `A<n> Start ... ; END ...` 行匹配 `[X,Y,Z,A,B,C]` 位姿。`inspect` 支持 `--json`，报告原始 `format`、`requires_conversion` 和固定的 `canonical_format=binary_little_endian`；拼接入口仅在 canonical Binary 准备完成后继续。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m30_pointcloudstitch` 兼容入口；保留现有应用流程。
- M3/M4：`stitching_interface` 已迁移到模块源码，target 负责流程编排并依赖配准、坐标转换和读写模块。
- M5.1：应用 seam fusion 入口已增加 `ok/error/cancelled` 状态并在正式输出前 fail-closed；GUI 仍保留应用数据模型适配，待公共拼接类型完全收敛后删除适配层。
