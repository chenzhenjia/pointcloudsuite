# 模块、UI 与交接协同规范 v0.5

2026-10-23 连续采集 v2：默认同步保存亮度、仅发布几何点云；第四步独立纹理开关默认关闭。10 提供兼容的严格/几何读取模式，30 显式只读几何；应用负责分列状态、严格纹理校验、参数恢复及工作台 revision 发布/清除旧覆盖，60 算法不变。`StitchingPublication::intensitySourceDirectory` 传递逐帧目录；不修改已有工序 JSON 业务。[当前说明](../development/camera_capture_intensity_decoupling_v2.md)。

日期：2026-10-15。当前基线见[需求](../requirements/pointcloudview_v0.5.md)与[软件架构](software_architecture_v0.5.md)。本文件维护协作规则；详细代码变更集中到[版本日志](../changelog/v0.5.md)，并在 [target.md](../../target.md)登记摘要。

## 职责与依赖

10 为读取、过滤、writer/事务和实验相机；20 为渲染；30 为整帧与所选平面拼接；50 为唯一坐标数学；60 为几何/工件/扫描/纹理。40 是 30/60 的 INTERFACE 兼容聚合，移动判定未实现；70/80 分别转发 60/10。90/100 进入[路线图](../development/roadmap_v0.5.md)。

依赖为 `30→10/50`、`60→10/50`、`40→30/60`、`20→core`。共享 UI 位于 apps，允许依赖算法；模块不得依赖 apps。所选平面通过应用适配 60→30，30 不链接 60。

## 命名与兼容映射

| legacy_entry | current_entry | future_boundary |
|---|---|---|
| 05、旧 10 read、根 filtering、80 writer | modules/10_pointcloud_io | pcv::io 公共门面 |
| 旧 40 registration、多帧/ICP/seam | modules/30_pointcloud_stitch | pcv::stitching；旧 pointcloud 保留兼容 |
| 旧 50 conversion | modules/50_coordinate_conversion | pcv::coordinates |
| 旧 60 plane、70 ROI | modules/60_geometry_workpiece | pcv::geometry/workpiece |
| 三维工件模板 v1（新增） | modules/60_geometry_workpiece/include/pcv/workpiece/workpiece_template_v1.h | pcv::workpiece::saveWorkpieceTemplate/loadWorkpieceTemplate |
| stitch 应用旧窗口实现 | apps/stitching_ui / pcv_stitching_ui | 当前唯一共享拼接窗口 |
| 旧 namespace/header/target | alias/forwarding 或原接口 | 逐批迁生产调用，不复制实现 |

当前 IO 多数为 `pcv::detail::io`，过滤为 `pcv::detail::filtering`，plane writer 为 `pcv::output`；坐标与整帧算法仍多为 `pointcloud::`，整帧编排为 `pcv::interface`。所选平面是 `pcv::stitching`，单击选面是 `pcv::planefitting`。`pcv::scan/texture/camera` 保留独立领域。实际头和 target 明细见架构与各模块文档。

目录/文件用 lower_snake_case；target 用 pcv_mNN_lower_snake_case（应用共享库沿用 pcv_stitching_ui）；类型 PascalCase；函数/局部变量 lowerCamelCase；常量 kPascalCase。JSON 新字段 snake_case，单位用 _mm/_deg/_ms；不得为统一命名修改既有 schema 字段。

旧类型/函数按兼容批次使用 [[deprecated]]；namespace/forwarding header 采用说明和映射，不能声称已全部标记。公共门面 → 生产调用 → 兼容审计分批记录，删除独立安排；保留旧新默认值差异。

### target 转发明细

以下为 CMake 中实际的 INTERFACE 链接；逗号表示并列依赖，不是 alias 已完全删除。

| legacy_target | forwards_to |
|---|---|
| pcv_m05_camerainput、pcv_m10_pointcloud_read、pcv_m10_pointcloudread、pcv_io、pcv_filtering、pcv_output | pcv_m10_pointcloud_io |
| pcv_m20_pointcloudrender | pcv_m20_pointcloud_render |
| pcv_m30_pointcloudstitch | pcv_m30_pointcloud_stitch |
| pcv_m40_pointcloudregistration | pcv_m40_pointcloud_matching |
| pcv_m40_pointcloud_matching | pcv_m30_pointcloud_stitch、pcv_m60_geometry_workpiece |
| pcv_m50_coordinateconversion | pcv_m50_coordinate_conversion |
| pcv_m60_plane_fitting、pcv_m60_planefitting、pcv_m70_roi_template | pcv_m60_geometry_workpiece |
| pcv_m80_plane_output | pcv_m10_pointcloud_io |
| pcv_m80_planeoutput | pcv_m80_plane_output |
| pcv_registration | pcv_m40_pointcloud_matching、pcv_m50_coordinate_conversion、pcv_m30_pointcloud_stitch |
| pcv_interface | pcv_m30_pointcloud_stitch、pcv_m60_geometry_workpiece |

### header 与实现归属

| legacy_header_or_directory | current_header_or_directory | actual_namespace |
|---|---|---|
| include/pcv/io/*、原 10 read | modules/10_pointcloud_io/include/pcv/io/* | pcv::detail::io；ArtifactTransaction 为 pcv::io |
| include/pcv/filtering/*、src/filtering | modules/10_pointcloud_io/include/pcv/processing/* | pcv::detail::filtering |
| include/pcv/output/plane_output.h、modules/80_plane_output | modules/10_pointcloud_io/include/pcv/io/plane_output.h | pcv::output |
| include/pcv/registration/multiframe_registration.h、原 40 registration | modules/30_pointcloud_stitch/include/pcv/stitching/multiframe_registration.h | pointcloud |
| include/pcv/registration/seam_fusion.h、原 40 seam | modules/30_pointcloud_stitch/include/pcv/stitching/seam_fusion.h | pointcloud |
| include/pcv/registration/handeye_transform.h | modules/50_coordinate_conversion/include/pcv/registration/handeye_transform.h | pointcloud |
| include/pcv/interface/stitching_interface.h | modules/30_pointcloud_stitch/include/pcv/interface/stitching_interface.h | pcv::interface |
| include/pcv/interface/temp_workpiece_interface.h、modules/70_roi_template | modules/60_geometry_workpiece/include/pcv/workpiece/*；interface/roi/matching 路径保留 | pcv::interface、pcv::roi、pcv::matching |
| apps/pointcloudstitch/stitchingwindow.h | apps/stitching_ui/stitchingwindow.h | 共享应用窗口，非领域 namespace |

这些表记录实现迁移关系，旧头仍按实际代码转发；不承诺每个旧符号已经具备编译器弃用属性。未来门面 namespace 与实际 namespace 分开登记。

## 文件所有权白名单

| owner | allowed_area | boundary |
|---|---|---|
| 陈振佳/主线 | modules、src、公共头、MainWindow、CMake、模块/契约测试、apps/stitching_ui | 数学/API、生命周期、依赖、事务和稳定业务会话 |
| 于海军/前端与外部项目 | apps/pointcloudview/features/**、交接登记的 UI 专属 .ui/资源/样式、UI 测试和交互说明 | feature 组件及外部高点/刀路项目 |
| 双方共同冻结 | DTO/schema、页面形态、跨项目调用、端到端样例 | 未冻结不宣称接入完成 |

现有 shared StitchingWindow 已共用，但本轮不另做 PointCloudStitch 视觉重设计。修改共享 UI 的任务语义、MainWindow 壳或生命周期须由主线处理；同事不能因 UI 改造直接复制窗口或算法。

临时 adapter 只能放在 `apps/pointcloudview/adapters/`，标记 Experimental 或 Deprecated，登记 owner、创建日期、实际替代 API、依赖、删除条件和回滚点。共享模块不得长期依赖应用 adapter；UI 不 include modules/*/src，不提交生成的 ui_*.h。

## 公共接口与数据发布

2026-10-16 连续采集v2：CameraDialog默认使用SdkImageFrame配置，操作员每会话确认当前设备一门控一整图及门控无效；10保留触发/自动行数/有界队列/v2会话，30/50既有直拼与行序数学复用。无PLC通信，异常仅停止软件接收；不改变主窗口发布/选面生命周期。[当前说明](../development/camera_capture_sdk_frame_v2.md)。

2026-10-16 连续采集：CameraDialog内嵌features/camera_capture_panel_v1，10在latest-frame覆盖前独立排队/保存完整扫描，50唯一数学按显式行序进度转换，30唯一编排直拼和事务输出，应用复用stitchingSucceeded发布完整CPU缓存/画布。采集/拼接与主界面处理互斥，停采后填写位姿，失败/取消/过期不发布；选面与共享窗口默认不动。[细节](../development/camera_capture_pose_stitch_v1.md)。

整帧共享 UI 调 `pcv::interface::StitchingRequest/stitch()`；所选平面调 `pcv::stitching::stitchSelectedPlanes()`；单击调 `pcv::planefitting::preparePlaneSelectionIndex/selectPlaneAtPoint()`。取消、错误、provenance、坐标/单位及产物见[数据契约](../contracts/data_contracts_v0.5.md)。

当前帧展示加速 v2 归 apps：`LoadDisplay` 完整读取/转换并均匀抽取最多 300,000 点，GUI 通过 20 的 `setDisplayCloud()` 发布后，`PrepareIndex` 使用 const 共享完整点云准备 60 的索引。期间允许视角操作，选面/确认/切帧/拼接禁用；索引失败或取消保留展示及已确认选择，可用三点备用。拾取回译完整下标，候选高亮仅显示可见子集。加载首个 frameSwapped 需校验帧、revision 及画布当前点云已绘制，计时仅记会话日志。上一轮 30 私有预览缓存/结果字段已撤回；30 恢复原配准及完整结果，应用用 `setCloud(result.points)` 显示拼接结果，不新增模块依赖。详见[现行开发说明](../development/current_frame_display_acceleration_v2.md)。

正式文件由 10 ArtifactTransaction staging/commit；30/60 可以编排事务。应用成功且任务仍有效才发布；共享拼接失败/取消不得清旧画布，成功则更新来源并清旧临时工件/平面/ROI/WObj1/纹理/轨迹。设置并行参数不得改变确定的合并顺序。

## 工件模板应用接入

工件模板应用接入：MainWindow 维护原始/当前 CloudSourceContextV1、revision、后台任务/取消/关闭 join；workpiece_template_ui_v1.cpp 通过模块 60 公共 API 编排，features/workpiece_template_dialog_v1.* 只实现名称/目录/摘要/基准确认表单。创建和打开入口位于工具菜单；来源状态独立于临时工件/WObj1 会话，普通 PLY 与原始相机/纹理坐标不因文件名而成为 robot_base。读取成功才替换画布及原始缓冲、清理旧派生会话；失败/取消/过期保留旧数据。已有模块依赖与公共契约不改，40 匹配/移动确认仍未实现。

## 外部刀路与交接包

相机扫描规划已有实现；全剖/点剖刀路、高点标注、路径 PNG 贴合与坐标 JSON 的完整外部闭环尚未冻结。保留版本化 DTO/JSON schema 需求，不发明正式名称或占位字段；先冻结文件/库/CLI/进程调用方式、坐标、结果和错误/取消语义。

交接包必须包含以下内容：

| item | required_content |
|---|---|
| ownership | owner、日期、文件白名单 |
| api_dependencies | 实际公共 API/target、DTO/schema 版本、坐标与单位 |
| interaction | 页面截图或交互说明、QWidget/QDialog 形态、忙碌/取消/失败流程 |
| validation | 构建命令、测试结果、固定样例、未验证项 |
| adapters | 临时 adapter、替代 API、创建日期、删除条件 |
| rollback | 已知问题、影响面、回滚点；不得覆盖后续用户修改 |

验收记录分别标明静态、构建、CTest、启动、GUI、真实数据与现场证据。输出成功不等于质量达标，合成模块耗时不等于用户端耗时；本次仅文档静态审计。

2026-10-16 六分屏整合映射：apps/pointcloudview/selectedplanedialog维护稳定Frame、六Panel、双Job、整轮提交/跳过及结果/模板编排；10可选关闭canonical、50窗口转换、60索引缓存/法向并行共同使用core/processing_budget.h预算。20只做映射显示和绘制revision；30求解/事务不变且不依赖60。模板创建复用features/workpiece_template_dialog_v1和60现有保存，读取复用主窗口入口。同步tests/CMakeLists.txt加入模板表单，静态交付不代表编译/运行通过。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)。


2026-10-16 整合v2：应用修正激活格/调度和全局完成门禁；所有任务载荷先通过稳定id/revision/token/kind/当前轮归属检查，过期OOM不阻塞新调度。排队计时包含线程池等待，worker结束至GUI回调另计；10/50/60诊断不写正式manifest。模块边界及模板编排不变，见[当前整合](../development/batch_plane_selection_template_integration_v2.md)。

2026-10-16 构建/运行收口：模块60仅修复索引局部变量命名冲突；应用pointcloudview及两个含主窗口资源的测试target显式AUTORCC，未新增模块依赖/公共接口/schema。六画布原生回归作为独立--native-gl模式，常规CTest仍offscreen；全新Debug/Release全量构建、各43项CTest和两个原生桌面入口启动通过。此前静态结论仅作历史，详见[修复说明](../development/build_runtime_repair_v1.md)。

## 相机向导应用映射（2026-10-16）

CameraDialog负责设备源/SDK通知/高级实时预览；CameraCaptureWizard负责四步连续采集UI、状态快照、预检与位姿批量校验，旧CameraCapturePanelV1为同一实现的alias。底层继续复用10 FrameSource/CaptureRecorder、30 StitchingRequest、50坐标数学，无新增领域模块依赖。详细交互、生命周期、验证和回滚见[开发说明](../development/camera_capture_ui_simplification_v1.md)。

2026-10-16 SDK启动顺序修复：模块10独占开始测量/清缓存/图像门禁及失败清理，应用仍仅依FrameSource::start返回成功进入取流态；原有错误通道传递阶段、原始状态及处理提示，无新增公共接口/应用依赖。见[说明](../development/camera_startup_order_fix_v1.md)。

2026-10-22 连续采集纹理点云 v1：`CameraCaptureWizard` 默认开启同步 Mono8 亮度采集，并记录 `external_light_enabled`/`external_light_operator_confirmed`；外置光源仅由操作员按提示开关，软件不控制硬件。应用从 10 的 `CaptureRecorder` 回读逐帧亮度图，调用 60 的 `mapMultiFrameBrightness`，在拼接事务成功后通过 `StitchingPublication` 和 `PointCloudCanvas::setTextureGrayOverlay()` 发布灰度覆盖；映射失败、亮度缺失或尺寸错误不发布部分结果。高级实时预览不启用纹理保存。[实现说明](../development/camera_capture_texture_v1.md)

2026-10-23 STEP/CAD模板v1：主责60私有OCCT解析/采样/独立CAD包，20独立CAD面画布仅依赖core DTO，apps负责导入选面任务、revision、来源和发布。新增来源枚举Unknown/RobotBase/CadModel，CAD禁机器人派生流程；保持30→10/50和60→10/50，40未实现匹配。见[实施说明](../development/step_cad_template_v1.md)。


2026-10-23 深度主缓存v1：10拥有C16/辅助元数据、SDK按需转换、session-v3/frame-v2；20提供SDK无关深度画布；应用CameraDepthPreviewV1负责固定帧/悬停/确认、revision/token/join及camera/mm发布；30适配深度来源，50/60数学不改，保留同期CAD/STEP修改。[说明](../development/camera_depth_preview_cache_v1.md)。
# 2026-10-23 UI shell v1

`MainWindow` 继续拥有数据读取、任务生命周期、revision 门禁和模块编排。`WorkbenchShellV1` 仅负责主窗口工作页、顶部入口和右侧覆盖抽屉，不拥有算法或后台任务。相机、拼接、六分屏和模板页面由原控制器继续持有，切页只改变显示；工具抽屉隐藏不销毁页面。

模块 20 的 `PointCloudCanvas` 增加绘制层叠加：黑色画布、自动 Z 高度伪彩、无数值彩条、左下 XYZ 轴和右上方向控件。画布仍负责 OpenGL、拾取和渲染 DTO，不依赖应用层。

2026-10-23 六分屏页面紧凑化：`SelectedPlaneDialog` 将参数、帧列表/轮次操作和运行日志置于两个默认收起的可展开容器内，六格画布仍常驻；仅设置 QWidget 可见性，不改变 Frame/Panel/Job、revision、取消/join、日志缓冲及提交行为。自动单测覆盖默认收起和展开/收起。原生 GUI 视觉截图未能在本轮稳定生成，设计对照见根目录 `design-qa.md`，状态 blocked。

## 独立扫描几何演示映射 v1（2026-10-26）

主责应用层 `apps/scan_geometry_demo_v1`；独立 EXE `scan_geometry_demo_v1`。`.ui`/QSS/QRC 提供 Designer 可编辑主体和原型图标，`ScanGeometryWindowV1` 持有流程模型、按钮状态、QTimer/token 及关闭/重新扫描确认，`DemoCanvasV1` 组合模块 20 并转换显示下标到稳定点 ID，`ScanGeometryModelV1` 维护六段示例、候选审核、修剪历史和确认快照。所有演示事件在 GUI 线程执行；无后台算法 worker 或硬件任务。

运行依赖仅为 core 点类型、Qt 和模块 20，不调用 10/30/50/60 的真实采集、配准或坐标数学。20 仅追加实例 `setAppearance`、既有 `PointColorOverlay` 的公开配色入口及浅底点数提示；默认调用方保留黑底/白选中。方向、相机操作、单点/矩形深度拾取继续复用 20，选面身份和擦除/撤销语义归应用。

本次白名单增量为新应用、新模型/view 测试、根/模块/tests CMake 注册、模块 20 canvas 头/源及配套文档；不覆盖已有主窗口/渲染改动，不更改版本/schema，不提交/push/上传。详细文件、接口、实际验证、最后 CMake 未重配项及回滚边界见[开发说明](../development/scan_geometry_demo_v1.md)。用户接手最终 GUI、DPI、Qt Creator target 运行及旧工作台验收，演示 [design-qa](../../apps/scan_geometry_demo_v1/design-qa.md) 保持 blocked，不代表最终视觉已通过。

## 2026-10-26 离线单页所选平面拼接 v2 协同映射

| owner_and_entry | responsibility_and_boundary |
|---|---|
| apps/stitching_ui / pcv_selected_plane_tasks_v2 | 从旧 SelectedPlaneDialog 提取真实读取/50转换/展示抽样/完整索引/选面快照；不拥有 UI 或调度 |
| apps/pointcloudview / SelectedPlaneDialog | 保留原 UI、队列、线程生命周期、模板操作；使用共用任务和兼容类型别名 |
| apps/scan_geometry_demo_v1 / ScanGeometrySessionV2 | 新单页独立会话、输入/参考 revision/token/稳定帧 ID、六帧窗口、固定参考匹配、异常暂停、逐帧/批量审核、拼接和会话修剪 |
| apps/scan_geometry_demo_v1 / 深度帧生命周期 | 模块10读取深度会话帧供预览，显式处理/自动匹配时单帧物化；逐帧或批量确认后释放活动XYZ、索引和展示缓存，保留源深度与确认快照。模块20只显示中立深度DTO；模块50按原像素行插值FullXyz位姿 |
| modules/60 / matchReferencePlaneV2 | robot_base 参考平面辅助候选，复用不可变索引和 selectPlaneAtPoint；不属于 40 位置确认，不自动业务确认 |
| modules/30 / stitchSelectedPlanes | 仍只收完整已确认 robot_base 选面 DTO，30→10/50 不变；不链接60、不重复手眼转换、不接干涉标注 |
| modules/20 / PointCloudCanvas | 复用实例外观/逐点配色、display→full 映射与可见 GPU 拾取；无新渲染算法或默认外观变更 |
| modules/60 / findHighSidePlaneCandidatesV1 | 复用完整选面索引和 selectPlaneAtPoint 提供高侧候选；由 scan_geometry_demo_v1 展示软提醒，模块 30 不接收该状态 |

根 CMake/模块条件补齐新 GUI 单独启用时20/30和共享任务依赖。旧模拟模型/画布源码保留，不在正式窗口运行链路中；target 名称与 Designer 入口保持。主线维护公共任务、会话/生命周期和 CMake；模块 60 只增加匹配 API。细节及未运行验证见 [开发说明](../development/scan_geometry_real_selected_plane_v2.md)。

2026-10-30 渲染与高侧提醒增量：原始转换帧使用 pointcloudview 默认高度色/点大小/叠加参数与完整 Z 范围；会话标注为蓝工件、红干涉，未选点保留高度色。模块 60 提供 `findHighSidePlaneCandidatesV1`，仅生成应用软提醒；用户手工确认才进入干涉标注，跳过提醒不会门控拼接。见 [开发说明](../development/scan_geometry_real_selected_plane_v2.md)。

2026-10-30 可见性与世界 Z 增量：模块 20 `PointCloudCanvas` 增加独立 marker overlay buffer，扫描几何页对标注点绘制 DPR 缩放的大点及深/浅双边缘，不改变高度色、纹理 overlay 和旧调用方默认值。模块 60 `findHighSidePlaneCandidatesV1` 使用候选中心 Z 减工件面中心 Z，取消法向方向/倾角相关判定。见 [开发说明](../development/scan_geometry_real_selected_plane_v2.md#2026-10-30-标注可见性与世界-z-高侧提醒)。

2026-10-30 拟合区域显示增量：模块 60 `buildPlaneMaskV1()` 从完整选面点索引生成占用网格三角面与外环/孔洞闭合边界，复用处理预算和取消回调；应用任务携带结果并按输入/frame 生命周期清理。模块 20 `PlanarMaskOverlay` 用动态 VAO/VBO/EBO 绘制 25% 透明蓝/红平面和闭合轮廓，点颜色、拼接 DTO、输出文件和旧轮廓接口不变。详见 [扫描几何 v2 说明](../development/scan_geometry_real_selected_plane_v2.md#2026-10-30-拟合平面掩码叠加显示)。
