# PointCloudSuite 顶层目标与交付状态

2026-11-05 Codex：最终结果起灰点、蓝色统一模板与红色确认干涉区域（25%），完整结果固定间距/网格原点，修剪后台重建、取消失败可重试；input/result/trim/annotation/token门控，面片更新不重传点。60掩码仅补显示元数据/固定原点，正式输出不变。新增测试源码，用户运行构建/CTest/GUI；[说明](docs/development/scan_geometry_result_regions_v1.md)。保留已有修改，不提交推送。

2026-11-05 Codex：应用入口收敛为scan_geometry_demo_v1与pointcloudview；删除独立亮度/拼接应用（含--regression）、旧模拟源码与专属测试，共32个受控文件，移除16个无引用兼容target及旧模拟库。公共兼容头、必要后端、真实任务/共享窗口、CLI/诊断/转换工具保留；Tools默认OFF、CLI-only关闭两个GUI、修正配置依赖。仅静态检查，构建/CTest/GUI由用户执行；本机build/.qtcreator及已有修改保留，未提交推送。[清单与验收命令](docs/development/application_target_cleanup_v1.md) · [日志](docs/changelog/v0.5.md#application-target-cleanup-v1)。

2026-11-04 Codex：新GUI默认高度色，逐帧工件候选有效后切灰（含自动匹配/网格失败），清除/失效恢复；结果/修剪高度色，蓝红25%平面叠加保持。完整Z按帧来源/结果revision缓存，配色不上传点，图例/色条同步。补测试源码，本轮只静态检查，不构建/CTest/GUI/性能，由用户验证；保留既有修改、未提交推送。[说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-11-04-按工件拟合状态切换配色) · [日志](docs/changelog/v0.5.md#scan-geometry-fit-color-mode-v1)。

2026-11-05 Codex：`scan_geometry_demo_v1`增加可选SR-2026候选几何契约导出，事务提交`point_cloud_result.json`、base完整/候选PLY、0/128/255掩码及原始输入证据；缺实际扫描来源为partial，旧sidecar保留，不发布模板。附Schema、样例、只读校验工具；Debug/Release构建与定向验证及全量失败记录见[开发说明](docs/development/scan_geometry_contract_adapter_v1.md) · [日志](docs/changelog/v0.5.md#scan-geometry-contract-adapter-v1)。保留已有及并行修改，未提交推送。

2026-11-05 Codex：新扫描几何三维画布统一 `#59636f` 灰点、关闭高度色条，蓝/红色仅用于 25% 拟合区域及闭合边界；三点十字与修剪边框/计数独立显示。新增20实例颜色/控制点接口和应用显示/区域增量缓存，保留百万点、512 MiB、旧外观及二维预览。测试源码已补，本轮不构建/CTest/GUI；[开发说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-11-05-灰色点云与独立拟合面显示) · [日志](docs/changelog/v0.5.md#scan-geometry-gray-points-v1)。

2026-10-30 Codex：`pointcloudview`新增独立深度图连续取流页，复用10有界完整帧队列和事务写入，逐帧输出纯C16 RAW、配置/校准状态 JSON、SDK离线转换亮度及可用行元数据；增加SDK一致性校验的 C++ RAW→PLY 工具和文件契约测试。源码树外Debug/Release、CTest及虚拟相机验收见[开发说明](docs/development/depth_raw_capture_v1.md)。真机、持久化吞吐及物理精度需现场验证；保留未提交修改。

2026-10-30 Codex：`scan_geometry_demo_v1` 增加SDK深度会话帧预览和按需点云物化；轮次缓存C16/辅助亮度，显式处理或自动匹配时由官方SDK生成XYZ，模块50按原始栅格行插值位姿，审核确认后释放该帧活动XYZ/索引/显示缓存。旧PLY流程不变。构建、CTest、原生GUI、无SDK及真实相机验证结果见开发说明；单帧峰值内存尚未声称下降。[开发说明](docs/development/scan_geometry_demo_v1.md#2026-10-30-sdk深度缓存与按需物化)。

文档版本 v0.5。更新日期：2026-10-15。责任人：陈振佳（后端/主线）、于海军（前端/外部项目）。基线：HEAD 75d3e3b 加当前未提交/未跟踪实现；不代表发布版本。

2026-10-30 Codex：扫描几何选面结果增加完整分辨率拟合区域掩码叠加。模块 60 `buildPlaneMaskV1()` 生成贴合平面的占用网格和外/孔洞闭合环；模块 20 `PlanarMaskOverlay` 绘制蓝/红 25% 透明区域及边界，原始点色与正式拼接/输出不变。增加模块/Canvas/GUI 测试源码和 Qt Creator 人工验收步骤；按用户约定未构建或运行测试。详见 [实施说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-10-30-拟合平面掩码叠加显示) 与 [集中日志](docs/changelog/v0.5.md#scan-geometry-plane-mask-overlay)。

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

| 2026-10-29 | 主线/Codex | `scan_geometry_demo_v1` 接入扫描 JSON、WObj1、人工干涉持久化、修剪派生输出和 `selected_plane_geometry_v1.json` sidecar；Debug 全目标构建，定向会话测试通过 | [开发说明](docs/development/scan_geometry_demo_v1.md) · [契约](docs/contracts/data_contracts_v0.5.md) |
| 2026-10-29 | Codex | 扫描几何页恢复深色高度渐变画布、合并导入与保存入口、逐帧确认后直接拼接，并折叠低频操作；源码外 Debug/Release clean build 和两套全量 CTest 34/34 通过，原生 GUI 检查因用户停止 Computer Use 未完成 | [开发说明](docs/development/scan_geometry_demo_v1.md#2026-10-29-画布与操作区精简) · [日志](docs/changelog/v0.5.md#scan-geometry-ui-simplification) |
| 2026-10-29 | Codex | 扫描几何右栏增加 HTML 五步入口与四个会话门控子阶段；源码外 Debug/Release 构建成功、全量 CTest 各 34/34，GUI 初始控件状态已检查 | [开发说明](docs/development/scan_geometry_demo_v1.md#2026-10-29-右侧步骤式操作区) · [日志](docs/changelog/v0.5.md#scan-geometry-step-workflow) |
| 2026-10-30 | Codex | 新扫描几何 GUI 画布改浅灰、每帧展示上限升至 100 万并将会话预算调至 512 MiB；旧选面对话框保持 30 万，验证由用户执行 | [开发说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-10-30-画布浅灰与百万点显示) · [日志](docs/changelog/v0.5.md#scan-geometry-light-canvas-million-display) |

| date | owner | change_summary | detailed_record |
|---|---|---|---|
| 2026-10-26 | 主线/Codex | 独立 `scan_geometry_demo_v1` Designer GUI、离线扫描/选面/候选/修剪/确认；模块 20 实例配色兼容增量。已生成 Debug/Release EXE，用户接手最终验证 | [实施、实际记录与待验收](docs/development/scan_geometry_demo_v1.md) · [日志](docs/changelog/v0.5.md#scan-geometry-demo-v1) |
| 2026-10-23 | 主线/Codex | 连续采集亮度默认保存、纹理独立且默认关闭；事后图片损坏不阻断几何直拼，保留可选纹理与光源操作提示 | [实现与验证](docs/changelog/v0.5.md#camera-capture-intensity-v2) |
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
2026-10-23 process-job JSON v1: process_job_json parser, capture-session validation, GUI import, pose_only stitching, template index and terminal summary added; Debug build passed; real camera/robot/production samples remain unverified.

2026-10-23 STEP/CAD模板v1：新增可选OCCT7.9.3 STEP单实体导入、三维多面选择、曲面/单位法向采样与独立cad_model模板包；来源与机器人功能门禁分离，原实测v1不变。验证与风险见 [实施说明](docs/development/step_cad_template_v1.md) 和 [本轮日志](docs/changelog/v0.5.md#step-cad-template-v1)。

2026-10-23 STEP/CAD模板v1验证完成：STEP开启Debug/Release及关闭Debug全新源码外完整构建通过，各30/30 CTest；ON原生CAD窗口及两配置双GUI自关闭通过，百万/千万点合成生成/读写/取消与峰值内存已记录。真实工件由用户后续验收，详见上述本轮日志；保留已有工作，未提交/推送。


2026-10-23 相机深度主缓存v1：SDK整图保留C16及必要辅助亮度，session-v3/frame-v2、伪彩预览、固定帧悬停/锁定/确认、按需点云发布/导出；旧会话/固定行数保留。开发及验证见 docs/development/camera_depth_preview_cache_v1.md。当前552万虚拟帧首次高亮约12.5s，2秒门槛未通过，不宣称物理精度通过。

2026-10-23 功能基线保存：最终Debug/Release完整构建及各41/41 CTest通过，无SDK定向2/2、双配置双GUI原生自关闭、Release六画布/相机向导与官方SDK无损重载回归通过。最新552万虚拟帧完整高亮9865ms，2秒及新候选300ms未通过，多轮内存保留与现场验收待复核。按用户授权将当前全部已审阅功能源码/测试/文档保存为本地Git基线，保留亮度/CAD/STEP/工序JSON工作，不打包、不推送、不继续优化算法。详见[最终证据](docs/development/camera_depth_preview_cache_v1.md#2026-10-23-功能基线最终验证与保存)。

2026-10-23 2.5D深度栅格选面优化方案v2：保持C16主缓存和官方SDK临时XYZ精度，拟用原始像素邻接替代当前三维投影连通瓶颈；默认只选鼠标所在连通区域，保留0.4/3mm容差、不补点/不跨面。本轮仅保存方案，未修改优化算法，见 [方案](docs/development/camera_depth_plane_selection_optimization_v2.md)。
# 本轮 UI 更新（2026-10-23）

- `pointcloudview` 主窗口改为预览优先工作台：顶部高频入口、工作页下拉和右侧覆盖工具抽屉；已有处理、输出、相机、多帧、STEP 和模板业务保持原 owner。
- 模块 20 默认点大小改为 2，加入 3DMVS 风格黑底高度 `RAINBOW`、彩条、坐标轴和方向控件；不改变点云/JSON/模板契约。
- 原生 Debug UI 回归已验证 1280×720 预览占比约 90.9%、抽屉覆盖不缩放画布、任务页复用和取点条恢复；真实样本与 Release/全量 CTest 待后续运行。
- 六分屏选面页将参数/帧列表与运行日志改为默认收起的可展开面板，保留原数据和任务状态；Debug/Release 全量 CTest 各 41/41、最终全目标增量构建和显隐定向测试通过。六分屏原生截图与设计对照仍 blocked，启动自关闭未取得通过证据；详见 `design-qa.md` 和 changelog。

## 2026-10-26 当前 GUI 接入真实所选平面拼接 v2

同名 `scan_geometry_demo_v1` target 已改为离线 PLY 真实链路：应用层共用读取/转换/索引/选面任务，模块 60 新增固定参考面辅助匹配，GUI 接入异常暂停、人工修正/跳过、逐帧审核和批量确认，调用模块 30 正式所选平面拼接。原 pointcloudview 保留原调度并复用任务；新页支持六帧轮次、红色人工干涉标注和基于稳定结果下标的会话修剪/撤销。主界面及输入配置均有 Designer `.ui`，没有接入设备、整帧拼接、CAD 或模板业务。

Owner：Codex（实现），用户（验证）。提供三组 v2 测试源码、构建配置与验收清单；按用户明确安排未执行配置/构建/CTest/GUI/截图/DPI/性能验证，未生成新版 EXE。有限种子匹配成功率、真实数据性能和原生拾取仍待验收。未提交/推送/上传，保留既有工作区及附件。详细文件/API、风险及用户命令见 [v2 开发说明](docs/development/scan_geometry_real_selected_plane_v2.md)、[操作说明](apps/scan_geometry_demo_v1/README_v2.md)和 [集中日志](docs/changelog/v0.5.md#scan-geometry-real-selected-plane-v2)。

2026-10-30 渲染与高侧提醒增量：原始转换帧使用 pointcloudview 默认高度色/点大小/叠加参数与完整 Z 范围；工件面蓝、干涉面红，未选点保留高度色。模块 60 新增 `findHighSidePlaneCandidatesV1`，以默认 2 mm、100 点、最多 16 个确定性种子提出高侧软提醒；手工确认后才成为干涉标注，忽略不阻止拼接。新增 API/会话测试源码和 Designer 门限设置。按用户分工未运行构建、CTest 或 GUI 验收；见[开发说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-10-30-转换预览与高侧平面提醒)。

2026-10-30 标注可见性与世界 Z 高侧提醒：模块 20 新增独立 marker overlay buffer；GUI 工件蓝、干涉红标注采用大点、深色外缘和浅色内缘，point size 按 DPR 缩放，不改变原高度色或纹理 overlay。模块 60 高侧高度改为候选平面中心 Z 减工件面中心 Z，水平/倾斜工件均按 robot_base 世界 Z 判定；Designer 明示世界 Z 门限。补充 marker 显示与世界 Z 测试源码及验收命令。依用户分工未运行构建、CTest、GUI/DPI 验收；见[开发说明](docs/development/scan_geometry_real_selected_plane_v2.md#2026-10-30-标注可见性与世界-z-高侧提醒)。

2026-10-30 本地版本归档：按用户要求仅将当前工作区提交到本地 Git 并输出提交号，不推送远端。提交包含真实所选平面拼接 v2、扫描几何演示、高侧提醒/标注、渲染与主窗口更新及对应测试/文档。已执行 diff 检查；本轮未重跑构建/CTest/GUI，验证边界沿用对应开发说明。

## 2026-11-05 新 GUI 加载计时与日志 v1

新GUI拆分LoadDisplay/PrepareIndex，先显示后建完整索引；当前帧就绪即可选面，前台任务优先且最多两个帧任务/各2 workers。完整Z统计后台化，模块20增加可选发布/上传CPU计时和独立点云revision。新增折叠日志区、源码外会话JSONL，记录操作/任务/耗时/错误。保留百万展示/六帧/512MiB及旧工作台。静态检查；构建/CTest/GUI/真实性能待用户执行，无加速结论。详见 [开发与验收](docs/development/scan_geometry_load_performance_v1.md)。

计时补充：10记录缓存身份头读取，60新增budgetAcquireMs透传，均只观测不改变算法；新GUI日志范围经用户确认，操作/任务/耗时/错误，无全局Qt捕获。

## 2026-11-05 真实日志耗时优化 v2

新GUI复用自动匹配完整选面结果，共享快照/面片后处理并预留完整点向量；索引法向显式65536网格批次、预算不足4096/串行回退，仍2帧×2workers。模块20递增映射采用二分反查、乱序保留QHash。补选面/匹配/法向诊断、独立切帧可见等待及成功message/质量warning，新增源码外日志汇总工具与等价测试。保留SHA256、完整摘要、512MiB、正式schema及既有未提交修改。只做静态检查；构建/CTest/GUI/Release实测由用户执行，无新版EXE或加速结论。详见[开发与验收](docs/development/scan_geometry_load_performance_v2.md)。

## 2026-11-05 点云加载与选面耗时优化 v3

新GUI先显示PLY，后续任务顺序校验完整摘要/准备索引；未校验隐藏旧面片/提醒并禁止选面审核，同源恢复/变化失效，失败保留只读预览并重试。60新增瞬时27邻居表（13方向双填、原序PCA）、任务内连通workspace、完整面片稠密/稀疏预算回退及细项计时；不增加512MiB或2帧×2workers。日志增加v3/variant和校验/索引里程碑；测试源码、文档已补，未构建/CTest/GUI/性能，无新EXE/提速结论。[说明与命令](docs/development/scan_geometry_load_performance_v3.md)。


## 2026-11-05 STEP 选面模板与自动匹配 v1

新GUI增加独立Designer模板制作/匹配审核窗口，多解析平面蓝工件/红干涉、完整采样及旧schema兼容角色伴随事务包；40新增STATIC模板6DoF搜索/精化/证据门控，50新增刚体估计/组合/约束秩，60复用完整选面/区域和CAD特征。当前完整修剪结果作目标，候选须人工确认后另存版本化JSON；原模板/扫描产物与30算法不变。STEP开关/OCCT部署、日志、revision/token、关闭join及测试源码已接入。仅静态检查，未配置/构建/CTest/GUI/真实样本，无EXE/成功率结论。保留已有修改/删除，不提交推送。[开发说明与用户命令](docs/development/step_template_matching_v1.md)。
