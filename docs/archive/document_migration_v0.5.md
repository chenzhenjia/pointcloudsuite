# v0.5 文档迁移与删除清单

日期：2026-10-15。基线HEAD：75d3e3b9200664433d2f6edc98e9fea15e129823，加实施前全部工作区修改/未跟踪文件；本次只文档操作。最初build*排除漏掉build-windows.md，最终基线 **57份Markdown + 1份txt**，没有空文件。

## 处理与恢复规则

共删除18份重复/被替代Markdown及1份旧txt；新增9份v0.5规范/日志/归档与1份共享UI README，最终49份Markdown。旧需求v0.1～v0.4及v0.4协同保留短指引；代码兼容层、源码/CMake/测试和数据均不删除。

先迁内容和更新引用，再删除旧文件。下表destination为当前规范，evidence为原文独有段落/命令/验证去向；字面相同段落只存一次并引用证据编号，不同配置/措辞保留，历史不得作为当前规则。近期日志正文中的旧操作由现行用户指南替代。

当前文档只定义当前行为；完整历史证据集中归档。非文档基线207个文件的SHA256在本轮工具审计中保存，对照结果写[版本日志](../changelog/v0.5.md#documentation-audit)。恢复时按独立diff人工合并，不覆盖后续用户修改。

## 每份原文件去向

| source_path | action | destination | reason | evidence |
|---|---|---|---|---|
| `README.md` | 重整现行内容 | [当前入口](../../README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `AGENTS.md` | 重整现行内容 | [当前入口](../../AGENTS.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-01) / [来源段](../changelog/v0.5.md#supplement-source-01)（46段） |
| `DEPENDENCIES.md` | 重整现行内容 | [当前入口](../../DEPENDENCIES.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `target.md` | 重整现行内容 | [当前入口](../../target.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-02) / [来源段](../changelog/v0.5.md#supplement-source-02)（90段） |
| `docs/user-guide/POINTCLOUDVIEW_REQUIREMENTS.md` | 重整现行内容 | [当前入口](../user-guide/POINTCLOUDVIEW_REQUIREMENTS.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/research/rz90_cross_direction_stitching_research.md` | 保留研究正文/更新状态 | [当前入口](../research/rz90_cross_direction_stitching_research.md) | 研究有独立证据，保留原时点 | 研究正文保留 |
| `docs/research/camera_sdk_direct_canvas_plan.md` | 保留研究正文/更新状态 | [当前入口](../research/camera_sdk_direct_canvas_plan.md) | 研究有独立证据，保留原时点 | 研究正文保留 |
| `configs/README.md` | 重整现行内容 | [当前入口](../../configs/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/requirements/pointcloudview_v0.4.md` | 短迁移指引 | [当前入口](../requirements/pointcloudview_v0.4.md) | 旧路径兼容，正文归档 | [来源段](history_pre_v0.5.md#source-03) / [来源段](../changelog/v0.5.md#supplement-source-03)（17段） |
| `docs/requirements/pointcloudview_v0.3.md` | 短迁移指引 | [当前入口](../requirements/pointcloudview_v0.3.md) | 旧路径兼容，正文归档 | [来源段](history_pre_v0.5.md#source-04) / [来源段](../changelog/v0.5.md#supplement-source-04)（118段） |
| `docs/requirements/pointcloudview_v0.2.md` | 短迁移指引 | [当前入口](../requirements/pointcloudview_v0.2.md) | 旧路径兼容，正文归档 | [来源段](history_pre_v0.5.md#source-05) / [来源段](../changelog/v0.5.md#supplement-source-05)（29段） |
| `docs/requirements/pointcloudview_v0.1.md` | 短迁移指引 | [当前入口](../requirements/pointcloudview_v0.1.md) | 旧路径兼容，正文归档 | [来源段](history_pre_v0.5.md#source-06) / [来源段](../changelog/v0.5.md#supplement-source-06)（42段） |
| `docs/README.md` | 重整现行内容 | [当前入口](../README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/pointcloudview_phase2_development_handoff_2026-08-25.md` | 合并后删除 | [当前入口](../user-guide/POINTCLOUDVIEW_REQUIREMENTS.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-07) / [来源段](../changelog/v0.5.md#supplement-source-07)（48段） |
| `apps/pointcloudview/adapters/README.md` | 重整现行内容 | [当前入口](../../apps/pointcloudview/adapters/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `apps/pointcloudview/README.md` | 重整现行内容 | [当前入口](../../apps/pointcloudview/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `include/README.md` | 重整现行内容 | [当前入口](../../include/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/80_plane_output/README.md` | 重整现行内容 | [当前入口](../../modules/80_plane_output/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `apps/pointcloudview/features/README.md` | 重整现行内容 | [当前入口](../../apps/pointcloudview/features/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/contracts/point_cloud_plane_output_v0.1_proposal.md` | 合并后删除 | [当前入口](../contracts/data_contracts_v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-08) / [来源段](../changelog/v0.5.md#supplement-source-08)（10段） |
| `docs/architecture/overview.md` | 重整现行内容 | [当前入口](../architecture/overview.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/architecture/module_ui_integration_v0.4.md` | 短迁移指引 | [当前入口](../architecture/module_ui_integration_v0.4.md) | 旧路径兼容，正文归档 | [来源段](history_pre_v0.5.md#source-09) / [来源段](../changelog/v0.5.md#supplement-source-09)（77段） |
| `docs/architecture/io-module.md` | 合并后删除 | [当前入口](../modules/10_pointcloud_io.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-10) / [来源段](../changelog/v0.5.md#supplement-source-10)（7段） |
| `docs/architecture/filtering-module.md` | 合并后删除 | [当前入口](../modules/10_pointcloud_io.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-11) / [来源段](../changelog/v0.5.md#supplement-source-11)（4段） |
| `docs/architecture/code-composition.md` | 合并后删除 | [当前入口](../architecture/software_architecture_v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-12) / [来源段](../changelog/v0.5.md#supplement-source-12)（10段） |
| `examples/README.md` | 重整现行内容 | [当前入口](../../examples/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/modules/30_pointcloud_stitch.md` | 重整现行内容 | [当前入口](../modules/30_pointcloud_stitch.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-13) / [来源段](../changelog/v0.5.md#supplement-source-13)（49段） |
| `docs/modules/20_pointcloud_render.md` | 重整现行内容 | [当前入口](../modules/20_pointcloud_render.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-14) / [来源段](../changelog/v0.5.md#supplement-source-14)（38段） |
| `docs/modules/10_pointcloud_read.md` | 合并后删除 | [当前入口](../modules/10_pointcloud_io.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-15) / [来源段](../changelog/v0.5.md#supplement-source-15)（22段） |
| `docs/modules/05_camerainput.md` | 合并后删除 | [当前入口](../modules/10_pointcloud_io.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-16) / [来源段](../changelog/v0.5.md#supplement-source-16)（39段） |
| `docs/modules/10_pointcloud_io.md` | 重整现行内容 | [当前入口](../modules/10_pointcloud_io.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/modules/100_qualityreport.md` | 合并后删除 | [当前入口](../development/roadmap_v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-17) / [来源段](../changelog/v0.5.md#supplement-source-17)（8段） |
| `docs/modules/60_geometry_workpiece.md` | 重整现行内容 | [当前入口](../modules/60_geometry_workpiece.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-18) / [来源段](../changelog/v0.5.md#supplement-source-18)（25段） |
| `docs/modules/50_coordinate_conversion.md` | 重整现行内容 | [当前入口](../modules/50_coordinate_conversion.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-19) / [来源段](../changelog/v0.5.md#supplement-source-19)（19段） |
| `docs/modules/40_pointcloud_matching.md` | 重整现行内容 | [当前入口](../modules/40_pointcloud_matching.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-20) / [来源段](../changelog/v0.5.md#supplement-source-20)（40段） |
| `docs/modules/70_roi_template.md` | 合并后删除 | [当前入口](../modules/60_geometry_workpiece.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-21) / [来源段](../changelog/v0.5.md#supplement-source-21)（5段） |
| `docs/modules/60_plane_fitting.md` | 合并后删除 | [当前入口](../modules/60_geometry_workpiece.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-22) / [来源段](../changelog/v0.5.md#supplement-source-22)（34段） |
| `docs/modules/80_plane_output.md` | 合并后删除 | [当前入口](../modules/10_pointcloud_io.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-23) / [来源段](../changelog/v0.5.md#supplement-source-23)（8段） |
| `docs/development/stitching_ui_integration_v1.md` | 合并后删除 | [当前入口](../changelog/v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](../changelog/v0.5.md#shared-stitching-ui)（19段） |
| `docs/development/single_click_plane_selection_v2.md` | 合并后删除 | [当前入口](../changelog/v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](../changelog/v0.5.md#single-click-v2)（22段） |
| `docs/development/single_click_plane_selection_v1.md` | 合并后删除 | [当前入口](../changelog/v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](../changelog/v0.5.md#single-click-v1)（7段） |
| `docs/development/selected_plane_stitch_v1.md` | 合并后删除 | [当前入口](../changelog/v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](../changelog/v0.5.md#selected-plane-stitch)（24段） |
| `docs/development/pointcloudview_memory_monitor_v1.md` | 合并后删除 | [当前入口](../changelog/v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](../changelog/v0.5.md#process-memory)（15段） |
| `docs/development/module_convergence_v0.4.md` | 合并后删除 | [当前入口](../development/development_plan_v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-24)（20段） |
| `modules/70_roi_template/README.md` | 重整现行内容 | [当前入口](../../modules/70_roi_template/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `examples/jobs/minimal_plane_output_v0.1/README.md` | 重整现行内容 | [当前入口](../../examples/jobs/minimal_plane_output_v0.1/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/30_pointcloud_stitch/README.md` | 重整现行内容 | [当前入口](../../modules/30_pointcloud_stitch/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/20_pointcloud_render/README.md` | 重整现行内容 | [当前入口](../../modules/20_pointcloud_render/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/10_pointcloud_io/README.md` | 重整现行内容 | [当前入口](../../modules/10_pointcloud_io/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/60_geometry_workpiece/README.md` | 重整现行内容 | [当前入口](../../modules/60_geometry_workpiece/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `src/README.md` | 重整现行内容 | [当前入口](../../src/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `modules/40_pointcloud_matching/README.md` | 重整现行内容 | [当前入口](../../modules/40_pointcloud_matching/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `apps/pointcloudstitch/README.md` | 重整现行内容 | [当前入口](../../apps/pointcloudstitch/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/modules/90_interferenceplane.md` | 合并后删除 | [当前入口](../development/roadmap_v0.5.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-25) / [来源段](../changelog/v0.5.md#supplement-source-25)（8段） |
| `docs/modules/pointcloudcli.md` | 重整现行内容 | [当前入口](../modules/pointcloudcli.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-26) / [来源段](../changelog/v0.5.md#supplement-source-26)（15段） |
| `modules/50_coordinate_conversion/README.md` | 重整现行内容 | [当前入口](../../modules/50_coordinate_conversion/README.md) | 去重复历史，按实际实现更新 | [整理记录](../changelog/v0.5.md#documentation-audit)；原当前说明已整合 |
| `docs/pointcloudview_ply_line_handoff_2026-09-20.txt` | 合并后删除 | [当前入口](../user-guide/POINTCLOUDVIEW_REQUIREMENTS.md) | 重复/被替代，独有内容先迁移 | [来源段](history_pre_v0.5.md#source-27)（49段） |
| `docs/development/build-windows.md` | 重整现行内容 | [当前入口](../development/build-windows.md) | 去重复历史，按实际实现更新 | [来源段](history_pre_v0.5.md#source-28) / [来源段](../changelog/v0.5.md#supplement-source-28)（39段） |

## 新增入口

需求、整体架构、v0.5协同、当前数据契约、开发方案、路线图、版本日志、本历史档案与迁移清单为9份版本文档；另新增apps/stitching_ui/README.md。其余模块/应用README使用稳定路径，不为版本号重复创建。

## 实施前工作区

下列状态属于本次文档整理开始之前，不能归为本次源码修改。最终任务增量需结合非文档SHA256对照判断，不能只看最终git status。

```text
M AGENTS.md
 M CMakeLists.txt
 M apps/pointcloudstitch/CMakeLists.txt
 M apps/pointcloudstitch/README.md
 M apps/pointcloudstitch/main.cpp
 M apps/pointcloudstitch/stitchingwindow.cpp
 M apps/pointcloudstitch/stitchingwindow.h
 M apps/pointcloudview/CMakeLists.txt
 M apps/pointcloudview/mainwindow.cpp
 M apps/pointcloudview/mainwindow.h
 M apps/pointcloudview/pointcloudprocessor.cpp
 M apps/pointcloudview/pointcloudprocessor.h
 M docs/README.md
 M docs/architecture/module_ui_integration_v0.4.md
 M docs/modules/30_pointcloud_stitch.md
 M docs/modules/60_geometry_workpiece.md
 M docs/requirements/pointcloudview_v0.3.md
 M modules/20_pointcloud_render/include/pcv/render/pointcloud_canvas.h
 M modules/20_pointcloud_render/src/pointcloud_canvas.cpp
 M modules/30_pointcloud_stitch/CMakeLists.txt
 M modules/30_pointcloud_stitch/include/pcv/interface/stitching_interface.h
 M modules/30_pointcloud_stitch/src/multiframe_registration.cpp
 M modules/30_pointcloud_stitch/src/stitching_interface.cpp
 M modules/60_geometry_workpiece/CMakeLists.txt
 M modules/60_geometry_workpiece/src/plane_fitting.cpp
 M target.md
 M tests/CMakeLists.txt
?? apps/pointcloudview/README.md
?? apps/pointcloudview/process_memory_usage_v1.cpp
?? apps/pointcloudview/process_memory_usage_v1.h
?? apps/pointcloudview/selected_plane_launcher.h
?? apps/pointcloudview/selectedplanedialog.cpp
?? apps/pointcloudview/selectedplanedialog.h
?? apps/stitching_ui/
?? docs/development/pointcloudview_memory_monitor_v1.md
?? docs/development/selected_plane_stitch_v1.md
?? docs/development/single_click_plane_selection_v1.md
?? docs/development/single_click_plane_selection_v2.md
?? docs/development/stitching_ui_integration_v1.md
?? modules/30_pointcloud_stitch/include/pcv/stitching/selected_plane_stitch.h
?? modules/30_pointcloud_stitch/src/selected_plane_refinement.h
?? modules/30_pointcloud_stitch/src/selected_plane_stitch.cpp
?? modules/60_geometry_workpiece/include/pcv/planefitting/single_click_plane_selection_v1.h
?? modules/60_geometry_workpiece/src/plane_connectivity_v2.h
?? modules/60_geometry_workpiece/src/plane_eigen_v1.h
?? modules/60_geometry_workpiece/src/single_click_plane_selection_v1.cpp
?? tests/unit/planefitting/single_click_plane_selection_tests_v1.cpp
?? tests/unit/planefitting/single_click_plane_selection_tests_v2.cpp
?? tests/unit/pointcloudview/process_memory_usage_tests_v1.cpp
?? tests/unit/pointcloudview/selected_plane_dialog_tests_v1.cpp
?? tests/unit/pointcloudview/stitching_ui_tests_v1.cpp
?? tests/unit/registration/selected_plane_stitch_tests_v1.cpp
```

## 验证记录

执行本地链接/锚点、标题、围栏、替换字符/UTF-8、空文档、旧路径和记录迁移检查；原文件到证据段覆盖与非文档SHA256必须一致，再完成删除。本轮不运行构建、CTest、GUI或性能测试。[最终静态结果](../changelog/v0.5.md#documentation-audit)。
