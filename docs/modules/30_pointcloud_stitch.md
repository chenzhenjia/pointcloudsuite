# 30_pointcloud_stitch：整帧与所选平面拼接

版本 v0.5，核对日期 2026-10-15。target pcv_m30_pointcloud_stitch，依赖 10/50、Qt Core/Gui/Concurrent，不依赖 60 或 apps。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [详细日志](../changelog/v0.5.md)。

## 整帧唯一编排入口

pcv/interface/stitching_interface.h 提供 pcv::interface::StitchingRequest/StitchingResult 与 stitch，按 input/registration/seam/artifacts 分组；多帧与 seam 算法头路径 pcv/stitching，实际 namespace 主要 pointcloud。读取→50 转 robot_base→多帧 ICP/结构校验→可选 seam→10 事务输出；取消或失败不返回可发布部分结果。

2026-10-09 算法曾恢复到 6329426，当前是在该恢复基线上继续集成共享 UI/所选平面等工作区增量，不是所有文件仍逐字等于旧提交。PoseOverlapFine、2.5D 及对应专属测试已回滚，不属于当前模式。完整配准保留既有质量门禁，仅手眼转换不等同执行配准验收。

分组请求当前仍映射弃用 StitchingOptions 并调旧实现，反向适配未完成。新 allowOverwrite=false、旧 true；sampleStride=8、previewRatio=0.1、并行默认开启、maxParallelTasks=0；seam 默认关闭，halfWidth/mutualDistance/decisionCellSize 为 8/0.6/0.5 mm。共享 UI 已透传 seam 距离/块大小和并行参数。正式四件套与可选 source/pre/post 共同事务。

## 所选平面独立入口

pcv/stitching/selected_plane_stitch.h 提供 pcv::stitching::SelectedPlaneStitchRequest/Result 与 stitchSelectedPlanes。应用先调用 50/60，再映射 SelectedPlaneFrame；输入已在 robot_base，30 不再次手眼，亦不增加对 60 的依赖。

只开放法向位移和两个倾角，保留不可观测的平面内机器人初值。两层受限 point-to-plane ICP、每对 10 mm/1°，按原相邻 pair 累计；无解/无重叠/超预算重置当前累计锚点并警告继续，累计漂移报告但不额外设总预算。保留原始实测点、顺序和 provenance，不补点/压平/seam/平均/去重。输入/映射/取消/内存/事务错误终止。

quality_policy=warn_only，strict_validation_status=not_run；formal_output 仅表示文件事务成功，旧未运行验收为 null。独立三件产物、PCVMAP、选择方式/参数和持久/返回计时范围见数据契约。单击与三点都必须预览确认，不能沿用 v1 的“第三点直接加入”历史操作说明。

## 应用、兼容与并行

pcv_stitching_ui 在应用层为 View/Stitch 共用窗口，调用分组入口；view 选面对话框调独立选面入口。旧 RegistrationDialog 停止编译，原 stitch 窗口为 forwarding；regressionrunner/registration_diagnostic 保留旧命令，诊断工具仍用旧 Options 和兼容 target。

worker const 读取 Qt 隐式共享容器并返回私有片段；全部 join 后按输入顺序合并。回归必须持有额外共享副本，覆盖串行及 1/2/4 任务等价、取消与 join 生命周期。

## 验证与风险

测试：stitching_interface_tests、multiframe_registration_tests、seam_fusion_tests、selected_plane_stitch_tests_v1；应用另有 selected_plane_dialog/stitching_ui/stitching_view。历史构建、500 次重复和 26/26 仅见日志的当次记录。本轮不构建/CTest/GUI。

真实平面错选/漏选、平面内机器人误差、回退残余错位、累计漂移、GUI 性能及生产质量未验收；所选平面 warn_only 不替代现场质量判断。构建组合与 API 债务见[开发方案](../development/development_plan_v0.5.md)。
