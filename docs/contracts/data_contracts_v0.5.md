# 当前数据与接口契约 v0.5

日期：2026-10-15。v0.5 是文档版本，不升级实际 schema 或格式。依据源码、公共头和测试；[需求](../requirements/pointcloudview_v0.5.md) · [架构](../architecture/software_architecture_v0.5.md) · [更新记录](../changelog/v0.5.md)。

## 坐标、单位与来源

长度 mm、角度 deg。控制器位姿为 `[X,Y,Z,A,B,C]`，`A=Rx/B=Ry/C=Rz`，旋转 `Rz(C)*Ry(B)*Rx(A)`。手眼 XML 的 `RTmatDepth2robot=T_flange_depth`；`T_base_depth=T_base_flange*T_flange_depth`。不得默认求逆或增加固定 Z 翻转/镜像/180° 补丁。

LineProfileXz 相机点为 `[X,0,Z]`，输入 Y 仅表示 `ratio=y/signedTravel`；负行程合法，方向由 Start/End 决定。FullXyz 保留 XYZ；点和法向共用旋转，法向不平移。所选平面公共请求已在 robot_base，绝不再次手眼；原始纹理实验保持输入 XYZ，不声明 robot_base。

PLY 按属性名读 XYZ/法向，支持 ASCII、binary little-endian、binary big-endian；正式点云写 binary little-endian。保留原点序及 sourceIndices，读取过滤统计、bounds、非有限与取消行为由 reader 定义。readPly 不写新文件；readPlyCached 的 canonical Binary LE 写入/回读失败导致缓存入口失败。

## 产物与版本目录

| workflow | files | actual_version_marker |
|---|---|---|
| 扫描 JSON 输入 | 点云/标定/Start-End 描述 | schema_version=sr2026-temp-scanning-info-mvp-2；kind=single_frame_scanning_info |
| 临时工件 | baseline_robot_base.ply、roi_template_robot_base.ply、plane_mask.png、temp_workpiece_info.json | schema_version=sr2026-temp-workpiece-info-mvp-2；kind=single_frame_workpiece_roi |
| 三维工件模板 | workpiece_template_robot_base.ply、workpiece_template_info.json | schema_version=pcv-workpiece-template-v1；kind=workpiece_template_3d |
| 旧平面 writer | base.png、base.json、base_plane_robot_base.ply | schema_version=sr2026-temp-workpiece-info-mvp-2 |
| 整帧拼接/仅转换 | stitched_robot_base.ply、stitched_robot_base_preview.ply、stitched_robot_base.pcvmap、stitching_manifest.json | schema=pcv-stitching-manifest-v0.4 |
| 所选平面 | selected_plane_robot_base.ply、selected_plane_robot_base.pcvmap、selected_plane_manifest.json | processing_mode=selected_plane_stitch；当前没有独立 schema/schema_version 字段 |
| 项目扫描轨迹 | scan_trajectory.json | schema_version=scan_trajectory_v1 |
| InoRobot 扫描文件 | P.json | schema_version=sr2026-p-json-mvp-2 |
| CLI 临时工件 Job | job.json | schema_version=pointcloudcli-temp-workpiece-job-v1；kind=temp_workpiece_job |

临时扫描 JSON 要求合法 created_at、camera 源、robot_base 目标与 LineProfileXz，当前解析的文件路径必须绝对。PLY 入口由 UI 构建内存描述符并手工录入 Start/End；CLI Job 的相对路径相对 Job 文件目录解析并拒绝穿越，不能与扫描描述符的路径规则混用。

临时工件 JSON 的顶层为 schema_version、kind、created_at、plane、image、roi、outputs；created_at 沿用扫描输入。plane.equation 是 WObj1 的 `[X,Y,Z,A,B,C]`，不是四参数平面方程，plane.wobj_num=1。image 记录 name、width_px/height_px、width_mm/height_mm、pixel_size_mm（0.05）；roi=rectangle；outputs 的 robot_base_point_cloud、roi_point_cloud、plane_mask 是绝对产物路径。

旧 writePlaneOutput 同 schema 的 plane 另含 name=WObj1，created_at 使用写出时间；默认目录 runtime_data/jobs/job_id/point_cloud/plane，destinationDirectory 可指定直接目录。旧 API 与临时工件 JSON 的字段差别必须保留，不能因共用 schema 名称合并字段或新增诊断。

## 三维工件模板 v1

模板包独立于临时工件四件套；完整保存当前全部点与已有法向，不代表全表面覆盖或几何质量验收。schema_version=pcv-workpiece-template-v1、kind=workpiece_template_3d，coordinate_frame=robot_base、units=mm；名称 name 保留用户原文，version_id 为 UTC 保存时间戳及可选冲突序号，created_at 为 ISO 时间（写出使用 UTC）。格式版本与工件版本分别记录，不升级旧 schema 或软件版本。

point_count 为正整数；bounds_mm={minimum:[x,y,z],maximum:[x,y,z]}，单位 mm，读取时与实际点云核对。point_cloud_file 为包内相对路径，默认 workpiece_template_robot_base.ply；source 为文件级来源对象，应用写入 entry、files、manifest、processing_steps，原来源不存在仍可打开模板。无逐点映射、WObj1、Mask、纹理或匹配结果。

v1 PLY 固定为 Binary Little Endian，字段顺序为 float x/y/z/nx/ny/nz；全零法向合法，不新增估计或归一化。拒绝非有限数据、未知版本/坐标/单位、非法包内路径及越界链接、缺失/截断/字段不符、点数或包围盒不一致，不静默过滤/补点。文件夹整体搬迁后按 JSON 所在目录解析 PLY。

保存目录为 `<规范化工件名>_v1_<UTC时间戳>`，冲突追加序号；新建版本默认拒绝覆盖，运行/导出目录必须源码树外。保存和读取复用稳定 PCV_CONTRACT_001、PCV_INPUT_001/002、PCV_OUTPUT_001/002；cancelled 单独判断。普通失败/commit 前取消不发布部分包，commit 已成功后仍返回保存成功。进程崩溃/断电原子性不新增承诺。

## 整帧与所选平面拼接

整帧请求按 input/registration/seam/artifacts 分组，sampleStride=8、parallelEnabled=true、maxParallelTasks=0、collectDiagnosticFrames=true；seam.enabled=false、halfWidthMm=8、mutualDistanceMm=0.6、decisionCellSizeMm=0.5；previewRatio=0.1。其余 ICP 默认值以 multiframe_registration.h 为准。

新请求 allowOverwrite=false，旧 StitchingOptions=true；映射时显式保留差别。正式四件产物和可选 source/pre/post ICP 文件在同一事务；可选文件名为 diagnostic_pre_icp_frame_NN.ply、diagnostic_post_icp_frame_NN.ply、序号_输入名_robot_base.ply。整帧 manifest 保留既有 schema、coordinate_frame、formal_ply、preview_ply、mapping_file、success、diagnostics；不套用选面字段。

PCVMAP 用 LittleEndian、SinglePrecision：quint32 magic=0x314D4350、quint32 version=1、quint64 point_count；随后每点 qint32 cloudId、qint64 sourceIndex、float32 scanRatio，顺序与 PLY 一致。整帧和所选平面复用此格式。

所选平面输入为有序 SelectedPlaneFrame，包含 robot_base cloud/provenance、模型 normal/center、RMS、种子原始索引和 selectionParameters。selection_method 为 single_click 或 three_point，对应 1 或 3 个唯一合法源索引，单击种子必须在所选点集内。应用输出目录为独立 selected_plane_v1_时间戳 子目录，存在时追加版本号，默认拒绝覆盖。

受限求解只开放法向位移与两个倾角，锁定切向位移/绕法向旋转；两层体素 0.5/0.2 mm、对应距离 3.0/0.6 mm、迭代 20/15，每层至少 100 对应，每对预算 10 mm/1°。原始相邻 pair 顺序累计；无重叠/无解/超预算弃用该对修正、当前累计重置单位并警告继续。保留实测点与重叠双份，不 seam、不压平、不补点、不平均、不去重。

选面 manifest 报告 quality_policy=warn_only、strict_validation_status=not_run；formal_output 表示事务成功，旧 accepted/结构覆盖未运行用 null。帧记录模型、来源、selection_method/selection_parameters；pair 记录可观测自由度、修正、回退、锚点重置、残差与累计漂移。不能将 degenerate（单平面未观测完整 6DoF）直接解释为严格验收失败，也不能解释为达标。

单击 v2 的 selection_parameters 记录 algorithm_version=single_click_plane_selection_v2、surface_tolerance_mm、maximum_gap_mm、sampling_spacing_u_mm/v_mm、connectivity_radius_u_mm/v_mm、merged_region_count、refinement_iterations、refinement_converged、gap_merge_ms 等现有诊断。默认 maximumGapMm=3，范围 0～50；0 关闭额外跨带，正常采样连接仍保留。gap_merge_ms 已包含在 connectivity_ms，不重复相加。

持久选面报告 output_stage_ms 只计 PLY/map staging，total_precommit_ms 截止 manifest 提交前，timing_scope 明示不含 manifest staging/commit。返回 report 另有 output_commit_wall_ms/total_wall_ms；磁盘 JSON 不在事务后重写。selection_ms 不含人工思考、拾取、排队和 GUI 绘制，不当作点击至完整高亮指标。

## 扫描轨迹与纹理

扫描支持 Global（拟合平面）/LocalRoi（当前 ROI，不重拟合），UI 默认 LocalRoi。范围由 WObj1 XY 包围盒确定；scanHeightMm/cameraWidthMm/overlapMm/travelExtensionMm 默认 50/100/50/50，线序 -X→+X，行程 +Y→-Y，两端各延长 50 mm。宽度不超过 cameraWidthMm+0.1 mm 时为单线；记录 single_line_width_tolerance_mm，不截断原始宽度。单线的重叠字段是兼容值，不表示有相邻带。

内部 P0/P1 等构成独立段；InoRobot 从 P[10] 开始，最多 9990 点，wobj_num=1、source_workpiece_info=temp_workpiece_info.json，XYZ 为 WObj1 局部坐标，姿态顺序 `[X,Y,Z,RZ,RY,RX]`。不写控制器，不证明可达性/碰撞安全。

纹理输入原始 PLY declaredPointCount 必须等于图像像素数。sourceIndex 从左上 row-major：pixelX=sourceIndex%width、pixelY=sourceIndex/width，仅 planeIndices 着色。拒绝非法 provenance、重复/越界索引、点数变化、过滤/降采样/拼接等不支持来源。转 Grayscale8、忽略 alpha，无裁剪/缩放/翻转/配准/对比度增强；不依赖 WObj1。显示亮度 0～255，无新增彩色 PLY/网格/UV/纹理文件或项目 JSON。

## 事务、错误与取消

新正式业务使用 10 的 pcv::io::ArtifactTransaction：stageBinaryPly、stageGrayscalePng、stageJson、stageBytes、commit/abort；校验输出根与相对路径，默认拒绝覆盖，取消/写入失败通过 staging 和备份回滚。stageGrayscalePng 会将非零变 255，属于二值 Mask writer，不能保存保真亮度；stageJson 只写 bytes，不验证 schema。

旧 pcv::output::writePlaneOutput 位于 10，仍使用自身 staging/backup/rollback，未完全收敛到 ArtifactTransaction；本次只登记，不修改实现。进程崩溃/断电级文件系统原子性不在普通失败回滚承诺内。

稳定错误族为 PCV_CONTRACT_001、PCV_INPUT_001/002、PCV_TRANSFORM_001/002、PCV_STITCH_001、PCV_PLANE_001、PCV_FRAME_001、PCV_IMAGE_001、PCV_OUTPUT_001/002。选面保持自身 selected_plane_* 错误码；单击返回 Success/InvalidInput/InsufficientSupport/Ambiguous/Cancelled。不为“统一”改变错误码；cancelled 标记或对应状态是判断取消的依据，不把取消当成功发布。

CLI 退出码 0/2/10/20/30/40/130 对应成功、契约、输入、转换、平面/工件、输出、取消。PLY/业务阶段失败 JSON 写 stderr，成功摘要 stdout；基础用法错误仍为错误文本和用法。数据目录只用于 temp-workpiece run，顺序为 --data-dir、PCV_DATA_DIR、exe 同级 data、LOCALAPPDATA/PointCloudSuite/data。

外部高点/刀路 DTO、schema 和页面仍待冻结，本文件不新增正式占位协议。本轮未运行动态验证。
