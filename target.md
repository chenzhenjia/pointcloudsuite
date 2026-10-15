# PointCloudSuite 顶层目标与交付状态

文档版本 v0.5。更新日期：2026-10-15。责任人：陈振佳（后端/主线）、于海军（前端/外部项目）。基线：HEAD 75d3e3b 加当前未提交/未跟踪实现；不代表发布版本。

## 北极星目标

以 pointcloudview 为统一入口，形成点云处理、局部标注、外部刀路规划结果接入与用户展示闭环。最终用户能查看点云、纹理、overlay、路径和坐标 JSON，清楚区分成功、取消、失败及不合规输入。

| business_flow | desired_sequence | current_gap |
|---|---|---|
| 全剖 | PLY → 点云处理/PNG → 外部全剖刀路 → 路径 PNG/坐标 JSON → 纹理点云展示 | 外部契约、调用和结果贴合未冻结 |
| 点剖 | JPG → 高点标注/high_point_overlay.png → 外部点剖刀路 → 路径 PNG/坐标 JSON → 展示 | 高点/刀路项目接入及端到端未验收 |

以上是业务目标，不是当前全部完成能力。相机扫描轨迹与外部刀路规划分开。

## 当前状态矩阵

| area | implemented | pending |
|---|---|---|
| 10/20/50 | PLY/缓存/预处理/事务、渲染、统一手眼 | namespace 门面、真实坐标/视觉、旧 writer 事务收敛 |
| 30 整帧 | 恢复算法基线上的完整配准/仅转换，共享 UI | 真实数据质量、构建组合、工具旧接口 |
| 30/60 选面 | 单击 v2、三点备用、预览确认、独立受限拼接 | 真实完整性/双层/漂移、点击至完整显示性能 |
| 60 工件 | 两组三点、WObj1、边缘/ROI、四件套、扫描、严格灰度、三维模板 v1 保存/读取 | 现场数据、row-major、WObj1/扫描、模板真实 GUI 样例验收 |
| view 应用 | 共享拼接发布与会话清理、Windows 内存监测 | DPI/最小窗口、真实操作及读数对照 |
| 原始坐标纹理 | 独立实验入口 | 不作为 robot_base 或临时工件结果 |
| 外部业务/40/90/100 | 只有规划或兼容边界 | 不声明算法、移动判定、干涉或质量报告已实现 |

PoseOverlapFine/2.5D 整帧模式已回滚，历史见归档；不因所选平面使用两层精配准而恢复这些模式。正式输出成功与几何质量、现场机器人安全分别记录。

## 责任与协作

陈振佳维护模块、稳定 API、MainWindow/共享 UI 生命周期、构建与契约；于海军负责登记的 features/UI 资源、交互、高点和刀路外部项目。双方冻结 DTO/schema、页面形态、调用方式并完成固定样例与端到端验收。文件白名单和交接包见[协同规范](docs/architecture/module_ui_integration_v0.5.md)。

## 开发优先级与完成门槛

P0 核对构建组合与 target 条件；P1 验收现有整帧/选面/临时工件/纹理/扫描和真实性能；P2 分组请求、公共门面和工具调用收敛；P3 冻结外部项目接入。40/90/100 独立远期立项。任务依赖和验收见[开发方案](docs/development/development_plan_v0.5.md)与[路线图](docs/development/roadmap_v0.5.md)。

目标闭环完成须有契约内真实样例、可复现输入/参数、正确产物和诊断、可见取消/失败/回滚、双方联调与端到端演示；不以成功但质量未知代替验收。不实现机器人执行/修正下发或宣称可达性/碰撞安全。

## 简要更新登记

| date | owner | change_summary | detailed_record |
|---|---|---|---|
| 2026-10-09 | 主线 | 整帧算法定向回滚后新增独立所选平面拼接 | [版本日志](docs/changelog/v0.5.md) |
| 2026-10-15 | 主线 | 共享拼接 UI、进程内存监测、单击 v1→v2 与来源参数 | [版本日志](docs/changelog/v0.5.md) |
| 2026-10-15 | 本轮文档整理 | v0.5 五层架构、契约、记录合并与文档清理；无源码变更 | [整理与验证](docs/changelog/v0.5.md#documentation-audit) |
| 2026-10-15 | 主线/Codex | 工件模板 v1：模块 60 独立版本事务保存/读取、GUI 基准确认及来源/revision 生命周期；保留既有修改 | [实现与验证](docs/changelog/v0.5.md#workpiece-template-v1) |

所有代码更新必须在本表追加简要登记，详细日期、owner、文件/API、行为、实际验证、风险与回滚点只维护在版本日志；模块文档同步当前接口，不复制完整历史。[旧历史](docs/archive/history_pre_v0.5.md)及[文档去向](docs/archive/document_migration_v0.5.md)保留追溯。

## 本轮验证及风险

下述静态结论属于文档整理轮；随后工件模板 v1 增量实施的构建、CTest、规模验证及真实样例待验收项，单独记录在[版本日志](docs/changelog/v0.5.md#workpiece-template-v1)。

本轮仅静态检查，不运行构建、CTest、GUI/性能测试。历史 26/26 和合成 1751.37 ms 不代表当前重验或真实两秒达标；Release redistributable、便携部署、生产数据/GUI、复杂缺失带、实验相机和机器人均有未验收项。

文档整理阶段未提交/推送；用户随后授权单独提交文档并同步 Gitee develop，既有源码改动保留未提交，详见版本日志末尾的同步授权记录。不覆盖原始数据。总入口：[需求](docs/requirements/pointcloudview_v0.5.md) · [架构](docs/architecture/software_architecture_v0.5.md) · [文档索引](docs/README.md)。

## GitHub 上传登记（2026-10-15）

用户授权将当前项目（含现有源码、测试和文档修改）提交并上传至 github remote 的 chenzhenjia/pointcloudsuite，目标 develop。未新增业务代码；本轮仅执行 Git 差异、文件体积、常见凭据模式和远程同步检查，不重跑构建/CTest，既有验证记录不作为本轮结果。详细记录见 docs/changelog/v0.5.md。
