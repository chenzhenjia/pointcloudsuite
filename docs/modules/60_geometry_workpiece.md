# 60_geometry_workpiece：几何与工件流程 v0.4

核对日期：2026-10-09。状态：实现已有，规范 namespace 与生产调用迁移待完成；真实业务 GUI/现场验收未完成。规范 target 为 `pcv_m60_geometry_workpiece`，实现位于 `modules/60_geometry_workpiece`。

## 1. 职责与当前接口

本模块负责平面拟合、边缘/ROI、WObj1、临时工件、扫描轨迹与严格栅格亮度映射，依赖 10/50、core/infrastructure 和 Qt Core/Gui；不依赖 `apps/` 或 QWidget。

| header | current_api | target_state |
|---|---|---|
| `pcv/planefitting/plane_fitting.h` | `pcv::planefitting::fit` | `pcv::geometry` 公共门面 |
| `pcv/workpiece/temp_workpiece_geometry.h` | `pcv::roi` | `pcv::geometry/workpiece` 门面 |
| `pcv/workpiece/roi_matching.h` | `pcv::matching::matchRoiToRobotBase` | 规范几何门面，旧匹配头保留转发 |
| `pcv/workpiece/temp_workpiece_interface.h` | `pcv::interface::prepareTempWorkpiece/finalizeTempWorkpiece` | `pcv::workpiece` 门面 |
| `pcv/scan/scan_planner.h` | `pcv::scan::planScanTrajectory` 与两种 JSON 序列化 | 保留独立扫描领域接口 |
| `pcv/texture/texture_mapping.h` | `pcv::texture::mapPlaneBrightness` | 保留独立纹理领域接口 |

`pcv/workpiece/` 是当前 header 路径，不表示 `pcv::workpiece` namespace 已落地。旧 60/70 target 与 interface/roi/matching 转发头保留兼容周期。

## 2. 平面、ROI 与临时工件

平面拟合拒绝重复、越界、非有限及共线控制点，保留 RANSAC/PCA、RMS、内点/阈值、planarity、连通域和取消诊断。等价提速 v2 提供 `parallelEnabled=false` / `maxParallelTasks=4`、`Result::cancelled` 和 `StageTimings`；worker 只读 const Qt 容器，私有结果汇合后按输入顺序提交，PCA/RMS 保持串行累加顺序。

PLY 内存描述符与扫描 JSON 共用读取、50 手眼转换和几何尾段。两组三点通过后建立 WObj1、边缘、ROI/Mask；正式四件套为 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`，由 10 事务成套提交。取消的阶段错误码保留，`cancelled=true` 是取消权威标志。

模块入口已有事务编排，进一步拆分纯领域结果/manifest 与提交编排属于后续收敛任务。baseline 产物不能作为 40 已实现移动判定的依据。

## 3. 扫描轨迹与严格纹理

扫描规划支持 `Global`（拟合平面）/`LocalRoi`（当前 ROI，不重拟合），UI 默认 LocalRoi；WObj1 XY 投影包围盒定义中心/范围，中心沿 `+Z` 偏移。默认 `scanHeightMm=50`、`cameraWidthMm=100`、`overlapMm=50`、`travelExtensionMm=50`，产品方向固定 `+Y→-Y`、线序 `-X→+X`。宽度 `<= cameraWidthMm + 0.1 mm` 为一条线，超过容差再计算均匀扫描带，实际重叠不低于请求值。

内部点为 `P0/P1...` 的独立扫描段，ABC 继承 WObj1；项目 JSON 为 `scan_trajectory_v1`。InoRobot JSON 为 `sr2026-p-json-mvp-2`，`source_workpiece_info=temp_workpiece_info.json`、`wobj_num=1`，P[10] 起始、最多 9990 点，XYZ 为 WObj1 局部坐标、姿态顺序 `[X,Y,Z,RZ,RY,RX]`。应用负责后台取消/revision、展示与通过 10 事务导出，不写控制器，不实现 IK/碰撞或全剖/点剖刀路算法。

亮度映射固定 `source_raster_index`，原始 PLY 点数必须等于图像像素数，原点序为左上角 row-major，`pixelX=sourceIndex%width`、`pixelY=sourceIndex/width`。只有平面索引对应点着色；缺失/重复/越界 source index、过滤或点数变化使整次映射失败。不裁剪/缩放/翻转/配准/增强，只转 Grayscale8，忽略 alpha。WObj1 不参与采样，也不因 WObj1 变化清除纹理。

原始坐标纹理实验入口直接保留 PLY XYZ 与 identity source index，两组三点同面校验后接入相同映射；它不转换到 `robot_base`、不建立 WObj1、不生成四件套。显示灰度可为 0～255，不等于正式 Mask 的二值 0/255。

## 4. 开发与验收记录

已有测试为 `plane_fitting_tests`、`plane_fitting_equivalence_tests_v2`、`roi_matching_tests`、`temp_workpiece_interface_tests`、`scan_planner_tests`、`texture_mapping_tests`；实验应用入口另有 `texture_experiment_tests`。

2026-10-09 仅整理规范文档，不运行构建/CTest/GUI。等价提速的性能与第三点到完整显示的 GUI 验收、真实控制点/Start/End、row-major 一致性、WObj1 朝向、机器人可达性与碰撞仍需按各任务独立验收。

历史正文及变更保留在 [60_plane_fitting.md](60_plane_fitting.md) 和 [70_roi_template.md](70_roi_template.md)。当前需求见 [v0.4 基线](../requirements/pointcloudview_v0.4.md)，接口/owner 见 [协同文档](../architecture/module_ui_integration_v0.4.md)，后续任务见 [开发方案](../development/module_convergence_v0.4.md)。
