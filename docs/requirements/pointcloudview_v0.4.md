# PointCloudSuite v0.4 需求与状态基线

版本：v0.4（架构与需求版本；CMake `PROJECT_VERSION=1.0.0` 未修改）。核对日期：2026-10-09。状态：实现与兼容迁移并存，发布/现场验收未完成。本次仅文档静态整理。

## 1. 文档职责与目标

源码、CMake、公共头和测试定义实际能力。本文件负责当前范围、实现状态与完成条件；[模块/UI 协同文档](../architecture/module_ui_integration_v0.4.md) 负责命名、依赖、所有权与交接；[v0.3 数据契约](pointcloudview_v0.3.md) 保留既有 schema、文件名、坐标、错误码及历史增量；[target.md](../../target.md) 负责业务目标与追加式变更记录。[开发方案](../development/module_convergence_v0.4.md) 登记尚未实施的收敛任务。

`pointcloudview` 的目标是统一点云处理、局部标注、外部全剖/点剖刀路结果接入与展示。后端负责人为陈振佳，前端/外部项目负责人为于海军；两方共同负责接口冻结与端到端验收。目标链路中的外部项目步骤不能写为本仓库已实现。

## 2. 模块与能力状态

| module | target | current_state | target_state |
|---|---|---|---|
| 10 `pointcloud_io` | `pcv_m10_pointcloud_io` | PLY/缓存、过滤、相机帧、writer、事务已有实现；相机实验受开关控制 | 规范 IO API 与生产调用收敛 |
| 20 `pointcloud_render` | `pcv_m20_pointcloud_render` | Qt/OpenGL 画布、点选、纹理灰度覆盖已有实现 | 完成真实桌面交互和纹理视觉验收 |
| 30 `pointcloud_stitch` | `pcv_m30_pointcloud_stitch` | ICP、多帧、seam、2.5D 可选路径、快速重叠精配准、manifest 与事务编排已有实现 | 分组请求成为主实现，旧入口仅适配 |
| 40 `pointcloud_matching` | `pcv_m40_pointcloud_matching`（`INTERFACE`） | 仅聚合 30/60；业务未实现 | 后续版本定义位置确认与移动判定 |
| 50 `coordinate_conversion` | `pcv_m50_coordinate_conversion` | 唯一手眼、插值与坐标数学实现 | `pcv::coordinates` 公共门面与生产调用收敛 |
| 60 `geometry_workpiece` | `pcv_m60_geometry_workpiece` | 平面/边缘/ROI/WObj、临时工件、扫描规划、严格栅格纹理已有实现 | `geometry/workpiece` 公共门面与交接收敛 |
| 70/80（历史模块） | 兼容 `INTERFACE` target | 分别转发 60/10 | 保留兼容周期，独立审查后才可清理 |
| 90/100 | 无功能 target | 干涉平面/质量报告未实现 | 后续独立需求与验收 |

依赖为 `30 -> 10/50`、`60 -> 10/50`、`40(兼容) -> 30/60`、`20 -> core`。共享模块不依赖 `apps/`；最终文件操作由 10 `ArtifactTransaction` 实施。现有 30/60 入口仍会编排事务，并非全部是纯结果接口。

## 3. 当前业务链与契约

- PLY/扫描 JSON 临时工件：读取 → 50 转换到 `robot_base` → 两组三点拟合/同面校验 → WObj1、边缘、ROI/Mask → 10 原子提交 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png`、`temp_workpiece_info.json`。会话取消、输入 revision 变化与同步失败不可发布部分四件套。
- 手眼链固定为 `T_base_depth = T_base_flange * T_flange_depth`，`RTmatDepth2robot=T_flange_depth`。`LineProfileXz` 用 `[X,0,Z]`，输入 Y 仅表示 `y/signedTravel`；`FullXyz` 保留完整 XYZ。控制器位姿 `[X,Y,Z,A,B,C]`，`A=Rx/B=Ry/C=Rz`，`Rz(C)*Ry(B)*Rx(A)`；长度 mm、角度 deg。
- 扫描规划是相机扫描轨迹，支持 `Global/LocalRoi`，UI 默认 LocalRoi；范围来自 WObj1 XY 投影，默认高度/相机宽度/重叠/两端延长为 `50/100/50/50 mm`，单线容差 `0.1 mm`，产品固定 `-X→+X` 线序与 `+Y→-Y` 行程。输出项目 `scan_trajectory_v1` 与 InoRobot `sr2026-p-json-mvp-2`；P[10] 起始、最多 9990 点、XYZ 为 WObj1 局部坐标，姿态 `[X,Y,Z,RZ,RY,RX]`，不写控制器。
- 纹理采用严格 `source_raster_index`：原始 PLY 点数等于亮度图像素数，左上角 row-major，仅当前平面点接收灰度；不裁剪/缩放/翻转/配准，无合法 provenance 或经过过滤/降采样/拼接时拒绝。`PLY 纹理实验（原始坐标）` 是独立实验入口，不转换到 `robot_base`、不建立 WObj1、不输出四件套。
- 拼接公共 API 默认 `Legacy`，GUI 默认 `PoseOverlapFine`；快速模式使用位姿初值与两层重叠区 ICP（体素 `0.5/0.2 mm`、距离 `3.0/0.6 mm`、迭代 `20/15`、每对预算 `10 mm/1°`），默认关闭 seam/preview。低质量或无有效修正可警告并回退位姿；取消、输入、映射与文件错误仍失败。`formal_output` 仅表示事务成功，严格质量状态为 `not_run`。
- 输出 PLY 为 binary little-endian；正式 Mask PNG 为 Grayscale8 0/255，显示用亮度灰度可为 0～255。JSON 字段、文件名、错误码和原有默认值以已发布契约与对应公共头为准；本次整理不修改 schema。新增扫描诊断字段和拼接模式是各日期记录的增量，不是本次新增能力。

## 4. 未实现边界与 UI 交接

40 正式目标声明：对经过点云处理和平面/坐标转换后的数据，生成含 `robot_base` 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

40 当前为“规划目标，未实现”：无基准模板持久化、基准比较、移动阈值/6DoF 判定、机器人执行/修正下发或新输出闭环。现有临时工件的 baseline 文件不构成已经实现的 40 移动判定。

主线维护 `MainWindow` 壳、生命周期、共享模块与稳定 API。同事工作区为 `features/**`、登记的 UI 专属 `.ui`/资源/样式以及 UI 测试/说明；临时 adapter 仅在 `adapters/**`，带 owner、日期、替代 API 与删除条件。外部高点标注、全剖/点剖刀路项目由同事负责，其 schema、部署接入方式与页面形态仍待冻结；扫描规划不替代刀路规划。

## 5. 完成条件与验证状态

文档整理完成条件：全仓 Markdown 均登记状态/规范入口，链接与目录映射有效，当前/目标/历史明确，更新记录进入 `target.md`。

代码收敛完成条件：生产调用迁移到正式公共 API、旧入口仅适配、无重复算法，取消/错误/事务与来源映射保持兼容，迁移批次与回滚点有记录。完整软件验收还需独立执行源码树外 Debug/Release 构建、CTest、GUI smoke、真实数据与高倾角基线回放；本轮按用户要求只静态检查，不执行这些命令。

历史验证数量仅对应当时源码/选项。当前源码新增的测试与未完成性能/真实数据验收不能借用旧结论；相机、机器人可达性/碰撞、便携部署和外部项目联调仍需独立验收。
