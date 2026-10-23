# PointCloudSuite Agent 工作手册

适用本仓库，更新 2026-10-22，文档/架构 v0.5。[索引](docs/README.md) · [需求](docs/requirements/pointcloudview_v0.5.md) · [协同规范](docs/architecture/module_ui_integration_v0.5.md) · [pointcloudview 架构与算法导读](docs/development/pointcloudview_architecture_analysis_v1.md)。原正文与历史区重整记录见集中归档。

## 沟通、范围与安全

默认中文，代码、命令、变量、路径、字段和错误保留原文。只处理用户明确指定任务/文件，简单稳定优先，不做无关重构或过度防御。报告做了什么、改了什么、如何验证和剩余风险。

保留已有未提交/未跟踪修改，不擅自覆盖；删除、覆盖、批量重命名/修改、上传、账号/数据库操作须有明确授权。不得写入凭据。禁止 reset --hard、checkout -- 等破坏性回退；用户仅要求本地提交时不 push。缓存、日志、构建、导出与运行数据必须在源码树外，Windows 路径支持 Unicode。

## 真相源与模块边界

源码、公共头、CMake、测试是实现事实；当前规范为 v0.5 需求/架构/契约/协同文档，旧版本短页只指向当前和历史，不再定义现行规则。文档版本不等于 CMake PROJECT_VERSION，不能自行升级软件或 schema。

| module | target | responsibility |
|---|---|---|
| 10 | pcv_m10_pointcloud_io | PLY/缓存、相机实验输入、过滤、writer/事务 |
| 20 | pcv_m20_pointcloud_render | 画布、OpenGL、拾取、渲染 DTO |
| 30 | pcv_m30_pointcloud_stitch | 整帧/所选平面拼接、诊断与输出编排 |
| 40 | pcv_m40_pointcloud_matching | INTERFACE 聚合 30/60，位置确认业务未实现 |
| 50 | pcv_m50_coordinate_conversion | 唯一手眼/位姿/坐标数学 |
| 60 | pcv_m60_geometry_workpiece | 平面/单击/边缘/ROI/WObj1、临时工件、扫描/纹理 |

30→10/50，60→10/50，40→30/60，20→core；70/80 兼容转发 60/10。共享模块不得依赖 apps；20 是唯一允许 Widgets/OpenGL 的领域模块，apps/stitching_ui 是应用层共享库。core/infrastructure 位于基础层。40/90/100 不得写成功能已实现。

算法实现放模块 src，公共头 include；wrapper 只做参数/结果映射。50 是唯一坐标数学，不复制 XML/SLERP/矩阵转换。30 不新增 60 依赖，应用负责选面 DTO；MainWindow 壳、任务生命周期、共享拼接 UI 和模块依赖由主线维护。同事白名单、临时 adapter 与交接冻结按协同规范执行。

### pointcloudview 架构与函数速查

pointcloudview 的分层是：入口 `apps/pointcloudview/main.cpp` → 应用编排 `MainWindow`/`SelectedPlaneDialog`/`apps/stitching_ui` → 领域模块 10/20/30/50/60 → `pcv_core`/`pcv_infrastructure`。PLY、JSON、PNG、PCVMAP、相机 SDK 是数据/外部边界，不是领域模块。`MainWindow` 持有当前点云、来源、revision 和临时工件会话；`SelectedPlaneDialog` 持有多帧顺序、六分屏、选面快照与调度状态；`PointCloudCanvas` 负责 GPU 显示、拾取及 overlay；共享拼接窗口只做整帧任务 UI/编排。相机 UI 仅在 `PCV_BUILD_EXPERIMENTAL_CAMERA` 开启时编译。

| owner | 关键入口 | 责任 |
|---|---|---|
| `MainWindow` | `openPointCloud`、`loadSelectedSource`、`loadFinished`、`publishCanvasCache` | 读取输入、确认异步结果仍有效、发布画布与来源状态 |
| `MainWindow` | `applyNoiseRemoval`、`noiseFinished` | 发起过滤并按输入 revision 接受/丢弃结果 |
| `MainWindow` | `determinePlaneCandidate`、`confirmPlaneCandidate`、`runPlaneExtraction` | 三点平面候选、用户确认和正式平面处理编排 |
| `MainWindow` | `planScanTrajectory`、`textureMappingFinished`、模板读写入口 | 调用 60 的扫描/纹理/模板 API 并更新 UI |
| `SelectedPlaneDialog` | `loadRound`、`requestFit`、`picked`、`confirmRound` | 多帧加载、选面索引/候选、拾取回译与整轮快照确认 |
| `SelectedPlaneDialog` | `schedule`、`startJob`、`finishJob`、`finishGlobal` | 有界后台任务调度、身份校验和结果收敛 |
| `SelectedPlaneDialog` | `stitch`、`saveTemplate`、`cancelAndWait` | 完整快照拼接、成功模板保存、取消/join |
| 模块 20 `PointCloudCanvas` | `setCloud`、`setDisplayCloud`、`setSelectedIndices`、`setTextureGrayOverlay` | 完整/展示点集、拾取映射、选择和灰度覆盖显示 |
| 模块 10 | `pcv::detail::io::readPly`、`readPlyCached`、`voxelDownsample`、`removeStatisticalOutliers`、`ArtifactTransaction::commit` | PLY/缓存、预处理和正式文件事务 |
| 模块 30 | `pcv::interface::stitch`、`stitchRawLineProfiles`、`pcv::stitching::stitchSelectedPlanes` | 整帧配准/仅转换，以及所选平面相邻精配准、合并和输出 |
| 模块 50 | `loadHandEyeCalibration`、`robotPoseToMatrix`、`interpolateRobotTransform`、`composeBaseFromDepth`、`transformLineScanToRobotBase` | 唯一标定解析、位姿数学和点云坐标转换 |
| 模块 60 | `fit`、`preparePlaneSelectionIndex`、`selectPlaneAtPoint`、`prepareTempWorkpiece`、`finalizeTempWorkpiece` | 平面、单击选面、临时工件几何及四件套输出 |
| 模块 60 | `mapPlaneBrightness`、`planScanTrajectory`、`saveWorkpieceTemplate`、`loadWorkpieceTemplate` | 灰度映射、扫描轨迹和模板读写 |

算法实现和精确数据流程见[架构与算法导读](docs/development/pointcloudview_architecture_analysis_v1.md)。编码时按以下事实理解算法：

- 单击选面先对完整点云建不可变索引，局部多尺度 RANSAC/PCA 找初始平面，全帧垂距分类，再投影网格连通、边界跨小缺失带并稳健精化；候选必须满足容差，不补点/加厚/压平，仍需用户预览确认。
- 六分屏最多展示每格 300,000 个确定性抽样实测点；拾取映射回完整点下标，正式索引、确认快照、拼接和模板输入保留完整点集。
- 整帧 ICP 的输入转换、降采样/法向、相邻帧多层级配准、结构验证/回退和原序合并归 30；关闭 ICP 时是仅转换。所选平面路径只收 robot_base 坐标，不得再次手眼转换；配准修正受预算约束，`formal_output` 只表示事务成功，不等于严格质量通过。
- 60 临时工件以两组平面和来源信息生成 baseline/ROI/Mask/info；纹理依赖严格 row-major `source_raster_index` 对应，不做图像配准；扫描规划不等于刀路规划，不做 IK/碰撞/机器人控制。
- PLY 读取、过滤、事务语义归 10；画布/拾取归 20；所有坐标数学归 50。UI wrapper 负责适配 DTO 和发布，不复制模块算法。

## 稳定契约与线程

公共类型、输出、坐标/错误码变更前阅读[数据契约](docs/contracts/data_contracts_v0.5.md)及对应模块文档；既有 JSON/PLY/PCVMAP、schema 名称和默认值不得随命名整理改动。新命名规则及实际 namespace 债务见协同规范，不把头路径当 namespace。

位姿 [X,Y,Z,A,B,C]，A=Rx/B=Ry/C=Rz，Rz(C)*Ry(B)*Rx(A)；RTmatDepth2robot=T_flange_depth，T_base_depth=T_base_flange*T_flange_depth。LineProfileXz 为 [X,0,Z]，Y 仅作 y/signedTravel；负行程合法，法向只旋转不平移。禁止固定翻转/镜像补丁，已在 robot_base 的选面请求不再转换。

新业务正式输出经 10 ArtifactTransaction，30/60 可编排事务；旧 writePlaneOutput 在 10 内仍有自身回滚实现，收敛属于后续任务。Mask 为 Grayscale8 0/255，显示亮度 0～255。取消/失败不发布部分结果，已提交文件与 GUI 发布分别处理，不复用旧结果假装成功。

worker 只经 const QVector 视图、at()/->at() 读取共享 Qt 容器，捕获列表显式只读对象；禁止非 const operator[] 触发 detach。worker 返回私有任务结构，全部 future join 后按确定顺序提交。新增并行路径保留 refcount>1 的额外共享副本，覆盖串行和 1/2/4 任务等价、取消与 join 生命周期。GUI 线程独占 QWidget/OpenGL；关闭先取消/等待，revision 过期结果不发布。

平面拒绝重复/越界/非有限/共线控制点；渲染拒绝非法点/坐标/索引；来源映射与点序保留。seam 默认 8/0.6/0.5 mm。所选平面 warn_only、formal_output 仅为事务成功，不宣称严格质量通过。单击真实端到端性能不由合成模块基准替代。

## 开发、验证与记录

1. 先 git status --short，记录工作区及主责模块/应用层，阅读公共头、CMake、测试和当前文档。
2. 明确接口、依赖和影响面，只改授权模块及必要跨界文件；并发修改先重读合并。
3. 按风险运行定向测试/构建；涉及公共头、CMake、processor、UI 或输出时独立执行源码树外 Debug 完整构建/CTest，发布变更补 Release。用户明确暂缓时遵从，并标明未运行。
4. 真实 GUI、相机、机器人、生产样例和部署分别验收，不由单元测试替代。只报告实际命令/结果，历史测试不得充当本轮通过。
5. 所有代码更新必须同步模块当前文档、协同映射；在 target.md 登记简要摘要，详细记录只追加 docs/changelog/v0.5.md，包含日期、owner、文件/API、行为、验证、风险和回滚点。
6. 交付前 git diff --check 与 git status --short，报告已有修改、任务增量、未跟踪/删除及未提交状态。

推荐构建工具及命令见[Windows 构建](docs/development/build-windows.md)。C++17、Qt6.5+、MSVC；根 CMake3.19+，preset version6 另要求3.25+。优先 NMake Makefiles，避免 JOM；VSLANG/头文件依赖护栏保留，公共类型修改后全量重建。GUI/CLI/Tools/Tests 组合存在待验证项，不沿用“固定开启 GUI”等旧文字。

## 兼容清理与本轮例外

2026-10-16 连续采集v2：用户授权实施、构建与测试。新入口默认SDK整图/自动行数，人工确认当前配置一次门控对应一整图；保留触发，未知轮廓拒绝，v2会话逐帧高度独立，v1固定行数兼容。异常只停软件接收，不控制机器人；30/50数学及选面不改。[说明](docs/development/camera_capture_sdk_frame_v2.md)与[本轮记录](docs/changelog/v0.5.md#camera-capture-sdk-frame-v2)。保留已有修改，不提交/推送/上传。

2026-10-16 连续采集与位姿直拼v1：用户授权实施及实际构建测试。主责10/30/50，CameraDialog直接采集/填XML和位姿/直拼并发布主界面；固定行数、原栅格行序进度、完整逐帧事务保存，非预览点云输入。首版无ICP/seam，不改选面算法。接口、会话格式及最终验证见[说明](docs/development/camera_capture_pose_stitch_v1.md)和[日志](docs/changelog/v0.5.md#camera-capture-pose-v1)。保留已有用户修改，不提交/推送/上传。

2026-10-16 当前帧展示加速 v2：选面对话框在完整读取/转换后，先显示最多 300,000 个确定性均匀抽取的实测点，再后台建立完整选面索引。画布拾取回译完整下标，完整候选只在展示子集上高亮，正式选面/快照/输出保持完整。已撤回上一轮模块 30 缓存及结果预览字段，拼接结果恢复完整显示，不新增 manifest 字段。按本任务用户安排不运行构建/CTest/GUI/性能测试；验收步骤见[开发说明](docs/development/current_frame_display_acceleration_v2.md)，上一轮 v1 说明仅保留历史。

旧 target/header/namespace 不新增重复实现；保留兼容周期。删除前须生产和工具引用清零、公共 include 迁移、无重复算法/流水线、构建/CTest/GUI/现场验收及弃用期或明确批准；删除独立评审/提交，不与迁移混合。

本次用户已授权按 v0.5 清单重写文档、合并历史、删除18份重复 Markdown 和1份 txt，源码兼容层不删。本轮只静态审计，不构建/CTest/GUI/性能测试、不提交/push/上传。[详细结果](docs/changelog/v0.5.md#documentation-audit)与[文档迁移表](docs/archive/document_migration_v0.5.md)为本次记录；先前规则和变更可在历史归档追溯。

2026-10-16 六分屏整合 v1：当前轮最多六帧完整数据和索引，每格展示≤300000确定性均匀实测点；两个帧级任务、每任务≤2计算worker，单任务≤4，共享256 MiB可选优化存储预算（不含业务数据/原有索引）。正式选面/结果/模板始终完整，30不依赖60。模板复用v1 PLY+JSON与主窗口读取。按用户安排仅静态检查，构建/CTest/GUI/性能由用户执行；见[开发说明](docs/development/batch_plane_selection_template_integration_v1.md)。


2026-10-16 整合v2纠偏：以[当前说明](docs/development/batch_plane_selection_template_integration_v2.md)替代v1当前执行规则，历史保留。默认活动格不抢索引，完整选面请求优先；过期id/revision/token/kind结果不改变队列或内存暂停；所有轮次提交后任意轮可拼接；提交跳过释放旧快照/缓存，撤销重载重选。投影连续窗口并行、全join后原序归并，新增分阶段耗时仅会话日志。按用户安排本轮不构建/CTest/GUI/性能、不更新程序、不提交推送。

2026-10-16 构建/运行修复：用户本轮明确授权实际构建与运行，以上静态交付限制仅属历史。修复60索引同作用域count重定义、应用/两主窗口测试target未启用AUTORCC导致图标缺失；新增资源加载及--native-gl六画布GPU拾取回归。全新源码树外Debug/Release全量构建成功，各43/43 CTest，原生六画布及两GUI自关闭均通过；不含相机/真实大样本/干净机部署。旧构建目录不覆盖，具体EXE、命令及风险见[修复记录](docs/development/build_runtime_repair_v1.md)。
