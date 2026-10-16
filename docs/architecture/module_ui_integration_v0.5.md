# 模块、UI 与交接协同规范 v0.5

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
