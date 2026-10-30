# 30_pointcloud_stitch：整帧与所选平面拼接

2026-10-16 SDK整图采集v2复用现有captureFrameDirectory和位姿直拼：每帧从10读自己的width/height及原始栅格下标，经50转换一次；不增加ICP/seam或改变正式manifest。本轮30源码不变，新增不同高度串并行/映射回归。[当前说明](../development/camera_capture_sdk_frame_v2.md)。

2026-10-16 连续采集直拼：StitchingFrameInput.captureFrameDirectory读取10保存的相机点云/栅格映射，经50行序插值复用仅转换和事务四件套。此输入拒绝ICP/seam，点序和来源保留，诊断明确pose_only/not_run；旧PLY与选面默认不变。[实现与验收](../development/camera_capture_pose_stitch_v1.md)。

版本 v0.5，核对日期 2026-10-15。target pcv_m30_pointcloud_stitch，依赖 10/50、Qt Core/Gui/Concurrent，不依赖 60 或 apps。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [详细日志](../changelog/v0.5.md)。

## 整帧唯一编排入口

pcv/interface/stitching_interface.h 提供 pcv::interface::StitchingRequest/StitchingResult 与 stitch，按 input/registration/seam/artifacts 分组；多帧与 seam 算法头路径 pcv/stitching，实际 namespace 主要 pointcloud。读取→50 转 robot_base→多帧 ICP/结构校验→可选 seam→10 事务输出；取消或失败不返回可发布部分结果。

2026-10-09 算法曾恢复到 6329426，当前是在该恢复基线上继续集成共享 UI/所选平面等工作区增量，不是所有文件仍逐字等于旧提交。PoseOverlapFine、2.5D 及对应专属测试已回滚，不属于当前模式。完整配准保留既有质量门禁，仅手眼转换不等同执行配准验收。

分组请求当前仍映射弃用 StitchingOptions 并调旧实现，反向适配未完成。新 allowOverwrite=false、旧 true；sampleStride=8、previewRatio=0.1、并行默认开启、maxParallelTasks=0；seam 默认关闭，halfWidth/mutualDistance/decisionCellSize 为 8/0.6/0.5 mm。共享 UI 已透传 seam 距离/块大小和并行参数。正式四件套与可选 source/pre/post 共同事务。

## 所选平面独立入口

pcv/stitching/selected_plane_stitch.h 提供 pcv::stitching::SelectedPlaneStitchRequest/Result 与 stitchSelectedPlanes。应用先调用 50/60，再映射 SelectedPlaneFrame；输入已在 robot_base，30 不再次手眼，亦不增加对 60 的依赖。

只开放法向位移和两个倾角，保留不可观测的平面内机器人初值。两层受限 point-to-plane ICP、每对 10 mm/1°，按原相邻 pair 累计；无解/无重叠/超预算重置当前累计锚点并警告继续，累计漂移报告但不额外设总预算。保留原始实测点、顺序和 provenance，不补点/压平/seam/平均/去重。输入/映射/取消/内存/事务错误终止。

quality_policy=warn_only，strict_validation_status=not_run；formal_output 仅表示文件事务成功，旧未运行验收为 null。独立三件产物、PCVMAP、选择方式/参数和持久/返回计时范围见数据契约。单击与三点都必须预览确认，不能沿用 v1 的“第三点直接加入”历史操作说明。

## 当前帧展示加速 v2（2026-10-16）

当前帧降采样归选面对话框的展示流程：完整读取/坐标转换后，最多显示 300,000 个按原点序均匀抽取的实测点，完整索引另在后台准备。选面算法、确认快照和本模块输入仍为完整点集；拼接完成使用 `setCloud(result.points)` 完整显示。

上一轮 v1 的私有帧缓存、pair 接口改动、`SelectedPlaneStitchResult` 预览字段和 manifest 预览诊断已撤回，恢复原先完整点裁剪及两层配准路径，原体素参数和约束不变。不新增 preview PLY，不改变正式文件契约。现行行为见[开发说明](../development/current_frame_display_acceleration_v2.md)，[v1](../development/selected_plane_stitch_preview_acceleration_v1.md) 仅供历史追溯；本轮构建及动态验证由用户执行。

## 应用、兼容与并行

pcv_stitching_ui 在应用层为 View/Stitch 共用窗口，调用分组入口；view 选面对话框调独立选面入口。旧 RegistrationDialog 停止编译，原 stitch 窗口为 forwarding；regressionrunner/registration_diagnostic 保留旧命令，诊断工具仍用旧 Options 和兼容 target。

worker const 读取 Qt 隐式共享容器并返回私有片段；全部 join 后按输入顺序合并。回归必须持有额外共享副本，覆盖串行及 1/2/4 任务等价、取消与 join 生命周期。

2026-10-23 亮度保存/纹理解耦 v2：采集帧输入显式使用模块 10 的 `GeometryOnly`，亮度缺失或损坏不阻断有效几何直拼；仍严格校验 PLY/原栅格来源、取消及转换完整性。纹理开启由应用另行严格校验/映射，不新增 30→60 依赖，不改几何数学及输出格式。[当前说明](../development/camera_capture_intensity_decoupling_v2.md)。

## 验证与风险

测试：stitching_interface_tests、multiframe_registration_tests、seam_fusion_tests、selected_plane_stitch_tests_v1；应用另有 selected_plane_dialog/stitching_ui/stitching_view。历史构建、500 次重复和 26/26 仅见日志的当次记录。本轮不构建/CTest/GUI。

真实平面错选/漏选、平面内机器人误差、回退残余错位、累计漂移、GUI 性能及生产质量未验收；所选平面 warn_only 不替代现场质量判断。构建组合与 API 债务见[开发方案](../development/development_plan_v0.5.md)。

六分屏/模板整合（2026-10-16）：30继续接收按原列表顺序排除跳过帧后的完整确认快照，原配准/预算/回退和PLY/PCVMAP/manifest契约不改；拼接结果完整显示。选面跳过、会话和拼接到60模板的调用都由应用编排，30不新增60依赖、预览缓存或结果降采样字段。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)，未重新构建或测试。


2026-10-16 整合v2：继续不改本模块算法、公开类型或manifest；应用任意轮满足全局完成条件即可按原序提交完整快照，结果setCloud完整显示并由应用交60保存模板。模块30不新增60依赖，不恢复配准缓存/previewPoints/preview PLY。见[当前整合](../development/batch_plane_selection_template_integration_v2.md)。


2026-10-23 深度会话v1：采集来源支持depth.c16，实际拼接经10兼容接口按需转换；保留原栅格、50数学和完整输出，不新增60依赖。[说明](../development/camera_depth_preview_cache_v1.md)。

## 2026-10-26 新单页应用接入所选平面

当前 `scan_geometry_demo_v1` 已通过应用层共享任务及正式会话控制器调用 `stitchSelectedPlanes`。输入为人工批量审核后的完整 robot_base 选面快照；参考搜索归模块 60，30 不新增 60 依赖。模块 30 算法、受限修正、仅位姿选项、重叠双份/原序合并和正式 PLY/PCVMAP/manifest 契约不变。新 GUI 的人工干涉标注与会话修剪不进入输出，界面明确 formal_output 仅为事务成功与 warn_only。见 [v2 说明](../development/scan_geometry_real_selected_plane_v2.md)；本轮未执行构建、测试或质量验收。