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
| 2026-10-16 | 主线/Codex | 模块 30 多段所选平面预览缓存与结果显示 v1；已被当前帧展示 v2 纠正，相关源码增量撤回 | [历史记录](docs/changelog/v0.5.md#selected-plane-preview-acceleration-v1) |
| 2026-10-16 | 主线/Codex | 当前帧先显示最多 300,000 个均匀抽取点，再后台建完整索引；选面/正式输出及拼接结果显示完整，用户负责构建和动态验收 | [实现记录](docs/changelog/v0.5.md#current-frame-display-acceleration-v2) |
| 2026-10-22 | 主线/Codex | 连续采集向导同步保存 Mono8 亮度图，位姿直拼后生成灰度覆盖并自动发布到工作台；外置光源仅操作员提示，旧入口兼容 | [实现记录](docs/changelog/v0.5.md#camera-capture-texture-v1) |

所有代码更新必须在本表追加简要登记，详细日期、owner、文件/API、行为、实际验证、风险与回滚点只维护在版本日志；模块文档同步当前接口，不复制完整历史。[旧历史](docs/archive/history_pre_v0.5.md)及[文档去向](docs/archive/document_migration_v0.5.md)保留追溯。

## 本轮验证及风险

下述静态结论属于文档整理轮；随后工件模板 v1 增量实施的构建、CTest、规模验证及真实样例待验收项，单独记录在[版本日志](docs/changelog/v0.5.md#workpiece-template-v1)。

本轮仅静态检查，不运行构建、CTest、GUI/性能测试。历史 26/26 和合成 1751.37 ms 不代表当前重验或真实两秒达标；Release redistributable、便携部署、生产数据/GUI、复杂缺失带、实验相机和机器人均有未验收项。

文档整理阶段未提交/推送；用户随后授权单独提交文档并同步 Gitee develop，既有源码改动保留未提交，详见版本日志末尾的同步授权记录。不覆盖原始数据。总入口：[需求](docs/requirements/pointcloudview_v0.5.md) · [架构](docs/architecture/software_architecture_v0.5.md) · [文档索引](docs/README.md)。

## GitHub 上传登记（2026-10-15）

用户授权将当前项目（含现有源码、测试和文档修改）提交并上传至 github remote 的 chenzhenjia/pointcloudsuite，目标 develop。未新增业务代码；本轮仅执行 Git 差异、文件体积、常见凭据模式和远程同步检查，不重跑构建/CTest，既有验证记录不作为本轮结果。详细记录见 docs/changelog/v0.5.md。

2026-10-16 六分屏选面/完整处理优化/模板整合 v1：应用新增每轮最多六格、两帧任务、整轮确认/可撤销跳过和完整结果保存模板；10/50/60增加兼容的可选加速与256 MiB优化预算，30求解/完整结果不变。回归源码已补，尚未构建/运行；见[开发说明](docs/development/batch_plane_selection_template_integration_v1.md)。


2026-10-16 整合纠偏 v2：修正真实操作调度优先级、过期结果/OOM门禁和任意已完成轮次拼接；提交跳过释放缓存，撤销重载重选；补齐实际排队/GUI等待及解析/归并/投影/连通/快照诊断，完整投影窗口并行保留原序。保留六分屏与完整拼接/模板，模块30不变。回归源码已补，未构建/运行、未更新可执行程序；见[当前说明](docs/development/batch_plane_selection_template_integration_v2.md)。

2026-10-16 构建与运行修复 v1（owner：主线/Codex）：修复60索引变量重定义、主窗口QRC未编译；补资源加载和原生六画布绘制/拾取回归。全新Debug/Release全量构建成功，各43/43 CTest、原生六画布及两个桌面入口启动退出通过；正式数据/模块30不变。新EXE与真实样本/便携部署边界见[修复说明](docs/development/build_runtime_repair_v1.md)，详细记录见[版本日志](docs/changelog/v0.5.md#build-runtime-repair-v1)。此前未构建结论保留为历史，旧EXE未覆盖，未提交/推送。

2026-10-16 用户确认测试完成、报错解决且功能正常，本轮收敛，停止功能扩展及追加测试；修复记录和验证边界见上方条目。

2026-10-16 新任务：连续采集与位姿直拼v1。10完整扫描队列/事务保存/回放，50显式原栅格行序进度，30复用仅转换，CameraDialog直接填写XML/逐帧位姿并发布主界面；不改选面/旧ICP。构建与测试实际结果见[版本日志](docs/changelog/v0.5.md#camera-capture-pose-v1)，操作和风险见[说明](docs/development/camera_capture_pose_stitch_v1.md)。

2026-10-16 连续采集优化v2：10新增显式SDK图像边界/触发保留、逐帧可变高度v2会话和队列/保存统计；相机面板默认自动行数、兼容固定行数、显式现场确认。复用30/50既有直拼，不新增PLC/ICP；新增回归和全新源码外验证。[当前说明](docs/development/camera_capture_sdk_frame_v2.md)，实际结果见[日志](docs/changelog/v0.5.md#camera-capture-sdk-frame-v2)。保留所有既有工作，不提交/推送/上传。

2026-10-16 相机连续采集UI简化v1：主线/Codex按用户授权实施四步向导、单次现场预检、后台排空、位姿复制与实时校验、高级预览及自动发布返回工作台；不修改相机/坐标/拼接算法和schema，不实现纹理。验证及风险见[开发说明](docs/development/camera_capture_ui_simplification_v1.md)，详细记录见[日志](docs/changelog/v0.5.md#camera-capture-ui-simplification-v1)。保留既有工作，不提交/推送/上传。

2026-10-16 相机UI向导收尾：用户确认“测试通过，收尾吧”，停止追加功能和测试，仅补交付记录及静态检查。用户验收与本地自动验证分别记录；首轮Debug/Release构建日志完成，本地UI测试曾有启动故障，未补记为全量CTest通过。验证边界详见上述开发说明。

2026-10-16 相机取流启动顺序修复v1（owner：主线/Codex）：按新授权修复SDK整图测量前清缓存导致CALLORDER，改为测量后清理并门禁图像，保留异常上报/严格失败和资源释放；新增生产驱动SDK替身回归、修正CTest运行PATH。公共接口/格式/算法不变，保留所有既有工作。验证及真机边界见[说明](docs/development/camera_startup_order_fix_v1.md)和[日志](docs/changelog/v0.5.md#camera-startup-order-fix-v1)。

2026-10-16 上述修复最终验证：源码外Debug/Release全量构建及最终重编成功，各52/52 CTest，两个GUI共4次原生启动退出通过；真机三次扫描与干净机部署待验收。新Release路径、实际日志和首轮测试夹具修正见上述说明；旧程序未覆盖，未提交/推送/上传。

2026-10-16 本地版本归档：用户授权将当前源码、测试及文档改动提交到本地 Git 并输出提交号，明确不推送远端。本轮未新增业务代码，仅执行 Git 差异检查；不重跑构建/CTest，既有验证及真机风险以各任务记录为准。
