# 多段平面拼接模块 30 预览降采样加速 v1

> 历史说明：本方案已由 [当前帧展示加速 v2](current_frame_display_acceleration_v2.md) 替代。按用户纠正，v1 的模块 30 配准缓存、结果预览字段及 manifest 增量已撤回，拼接结果恢复完整显示。下文保留历史，不代表当前实现；现行降采样只用于加载当前帧的展示。

实现日期：2026-10-16。owner：主线/Codex。主入口为模块 30 的 `pcv::stitching::stitchSelectedPlanes()`；画布和应用只消费模块结果。本文配套[当前模块说明](../modules/30_pointcloud_stitch.md)、[数据契约](../contracts/data_contracts_v0.5.md)及[详细更新记录](../changelog/v0.5.md#selected-plane-preview-acceleration-v1)。

## 数据流和实现边界

1. 校验每帧已确认的完整 robot_base 平面快照及来源。
2. 模块 30 为每帧一次准备固定 `0.5 mm` 粗层、`0.2 mm` 精层体素质心，以及完整点云在本帧/前一目标帧平面坐标中的投影边界。私有缓存仅在本次请求内复用，不修改 `SelectedPlaneFrame` 或输入 `samplePoints`；位姿直拼只准备细层显示所需数据。
3. 相邻帧按照完整边界确定重叠窗口，裁剪两级缓存；每层目标索引只建一次。保持原有受限 point-to-plane 求解、对应半径 `3.0/0.6 mm`、迭代 `20/15`、每对 `10 mm/1°` 预算和位姿回退/链锚点规则。
4. 按原相邻 pair 顺序累计修正，再逐帧逐点修正完整点云。完整 `points`、`cloudIds`、`sourceIndices`、`scanRatios` 和事务输出保持原契约，所有确认的实测点继续输出，不压平、补点、平均或去重。
5. 显示从每个细体素的首个真实输入点取代表，直接引用已修正的完整点；`previewPoints[i] == points[previewToFullIndices[i]]`，包含 XYZ 和已有法向。映射指向合并后的完整下标，不是 PLY 来源编号。映射严格递增，长度、越界、重复和总点数在发布前检查。
6. 应用通过 Qt 隐式共享保留完整结果，画布仅上传显示预览。预览不写回确认快照，也不参与下一次选面或拼接。用户需加载原始帧才能重新选择。

采样按原始输入顺序累计，哈希表仅定位体素、不决定输出顺序；缓存质心只用于配准，显示与正式产物都不包含人造质心。固定体素不等于固定显示点数，本版没有 300,000 点上限。稀疏点云可能保留几乎全部显示点。

## 接口、状态和输出

`SelectedPlaneStitchResult` 尾部新增 `previewPoints`、`previewToFullIndices`、`fullPointCount`、`previewPreparationMs`、`previewRegistrationMs`、`fullMergeMs`、`previewVoxelCoarseMm=0.5f` 和 `previewVoxelFineMm=0.2f`；原 `points` 始终为完整正式结果。

模块 20 完成既有未提交的 `setDisplayCloud(QVector<Point3D>, QVector<int>, qsizetype)` 声明实现。输入校验通过才替换画布；点拾取/框选返回完整下标，选中点/平面/边缘高亮及纹理会映射到实际显示点，未显示点不伪造。`setCloud()`/`setCloudStreaming()` 清除映射，保持普通全量显示兼容。画布使用 `int` 拾取索引；超过该范围时应用保留旧显示并记录已保存的正式文件路径，不截断正式输出。

缓存生成、裁剪、目标索引和对应搜索有取消检查；并行搜索只读共享数据，按分片顺序 join/归并。失败/取消清空完整点及预览点，不返回半成品。应用继续使用关闭等待和 revision 门禁，worker 进度通过 queued 调用写 GUI 日志。正式事务完成后的文件不会因 GUI 结果被丢弃而删除。

正式产物仍只有 `selected_plane_robot_base.ply`、`selected_plane_robot_base.pcvmap` 和 `selected_plane_manifest.json`，不新增 preview PLY。PLY/PCVMAP 使用完整结果，manifest 的原字段含义不变，增加：

| field | meaning |
|---|---|
| `preview_point_count` / `full_point_count` | 显示点数 / 完整正式点数 |
| `preview_to_full_mapping_count` | 与显示点一一对应的完整索引数 |
| `preview_voxel_coarse_mm` / `preview_voxel_fine_mm` | 固定两级体素 0.5 / 0.2 mm |
| `preview_preparation_ms` | 校验之后的缓存/完整投影边界准备，不含输入校验 |
| `preview_registration_ms` | 从缓存裁剪、建立层索引到受限 pair 求解及 pair 诊断 |
| `full_merge_ms` | 完整点修正/来源合并及显示映射生成 |
| `preview_timing_scope` | 上述计时边界，明确不含 GUI |
| `preview_is_display_only` | 固定 `true`，返回预览只用于显示 |
| `preview_registration_representative` | `centroid`，内部配准代表 |
| `preview_display_representative` | `first_input_point`，真实显示代表 |

`frames[]` 另记录两级缓存点数及该帧准备时间；位姿直拼的粗层点数为 0。比较速度时应计入准备、配准、合并和原有输出阶段，不能只取配准时间。显示首帧和旋转流畅度由 GUI 实测，不用模块计时替代。

## 用户验收步骤

本任务由用户执行构建、自动测试、真实数据、GUI 和性能验收。开发侧只执行静态源码/差异/文档检查；新增回归断言尚未运行，不能视为通过。公共类型有新增字段，测试前应在源码树外重新完整构建，避免运行旧二进制。构建方法沿用 [Windows 构建说明](build-windows.md)，默认不修改原始点云。

1. 准备至少两段水平/倾斜/垂直面，按原流程确认每帧完整候选，记录各帧确认点数。
2. 启动“拼接所选平面”，日志应出现“模块30：准备第…段平面预览缓存”，以及粗/精层迭代信息。成功后显示“显示预览点 / 完整点”和三项模块计时。
3. 画布统计显示“显示降采样 / 完整点数”，检查旋转、平移、缩放及点坐标；显示代表坐标应是修正后的真实点。显示稀疏不表示正式点被删除。
4. 核对 manifest 的 `point_count == full_point_count == 所有帧确认点数之和`；独立读回 PLY，确认相同点数，并按既有 PCVMAP 契约核对来源和顺序。应无新增 preview PLY。
5. 持续比较旧版和本版在相同硬件、构建配置、输入、帧序和参数下的总耗时。记录每帧完整/缓存点数、三项新计时、输出时间、总时间、首帧等待和 GUI 表现。
6. 检查小重叠、无重叠、噪声、孔洞、密度变化、窄条带、预算回退、单击/三点混合、重排帧和位姿直拼对照；在缓存准备、配准及合并期间分别取消并关闭窗口。
7. 用户可运行现有 `selected_plane_stitch_tests_v1`、`selected_plane_dialog_tests_v1`、`pointcloud_canvas_contract_tests` 及全量 CTest。选面拼接测试新增稠密真实代表/完整重复点保留、映射坐标/法向/顺序、manifest、确定性和取消断言；既有串行及 1/2/4 任务、额外 Qt 共享副本回归继续保留，不代表新增 GUI 自动覆盖。

## 未验收风险

全帧先体素化与旧版先裁剪再体素化在重叠边缘可能产生小差异，需要检查窄重叠和预算回退。缓存预处理/额外内存可能抵消小数据或小重叠的收益；固定 0.2 mm 也未必大幅减少稀疏点。完整点变换与磁盘输出耗时仍随总点数增长。未运行真实 GUI，不承诺 2 秒或任何固定加速比例；本版也未修改选面前原始帧加载、单击 v2 算法或主窗口亮度/纹理图预览。
