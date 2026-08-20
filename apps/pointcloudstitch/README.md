# pointcloudstitch

独立 Qt 6/C++17 三帧线激光点云拼接工具。当前处理流程按以下参考实现重新编写：

- `D:\Share\Point_Cloud_Stitching\laser_profile_camera_transform_gui.py`
- `D:\Share\Point_Cloud_Stitching\point_cloud_stitching_gui.py`

参考文件只用于确认算法行为，程序运行时不依赖 Python、Open3D 或 SciPy。

## 固定流程

1. GUI 提供“配准与融合”和“仅手眼坐标转换”两种模式。配准模式输入三个 ASCII PLY；转换模式接受一个或多个 PLY。每帧均填写 Start/End `X Y Z RX RY RZ`。
2. 直接读取 XML 中 `RTmatDepth2robot/RotMat/TVec` 作为 Depth 到法兰矩阵。
3. 扫描期间 Start/End 法兰旋转必须相同。
4. PLY.Y 是沿机器人主运动轴的有符号行程，允许比例范围 `-0.02 .. 1.02`。
5. 激光轮廓按 `[PLY.X, 0, PLY.Z]` 转换，机器人平移按 PLY.Y 线性插值。
6. ASCII PLY 复用 `pointcloudview` 的内存映射分块读取器，大文件自适应使用最多 4 个解析线程；映射不可用时回退单线程。每 8 个有效点取一个配准样本，正式点云不降采样。
7. scan 2 配准到 scan 1，scan 3 配准到已对齐的 scan 2。
8. 相邻帧使用同一三维包围盒重叠区，默认外扩 `5 mm`；ICP 对应只允许位于主平面预对齐后的无外扩真实三维交集。
9. ICP 前匹配相邻帧主水平面，只执行法向平移和围绕源平面质心的倾斜校正；预对齐超过 `6 mm / 0.5 deg` 时拒绝。
10. 先从全部帧提取候选水平面，并在默认 ±5 mm 高度容差内选择覆盖全部帧的同一平面轨迹；相邻对禁止各自切换到不同物理表面。无法覆盖全部帧时回退局部匹配并明确记录。
11. Point-to-Plane ICP 围绕重叠对应点质心求解旋转，并将 Hessian 投影到可观测特征子空间，抑制大平面的平面内退化滑移。

## 仅手眼坐标转换

手眼坐标转换是完整 ICP 配准流程固定的第一步。在“处理模式”中选择“仅坐标转换：手眼转换 → 直接输出 PLY”后，程序在该步骤结束后直接输出：

```text
PLY [X,Y,Z]
→ Y 计算扫描行程比例
→ [X,0,Z] 进入 T_base_flange(t) * T_flange_depth
→ 机器人基坐标点云
```

该模式不执行主平面预对齐、ICP、配准验收或接缝融合，也不会因 ICP 失败阻止输出。输出包括每帧 `*_robot_base.ply`、合并的 `transformed_robot_base.ply`、`transformed_robot_base_preview.ply` 和 `coordinate_transform_report.json`。它用于独立检查 XML 矩阵方向、机器人位姿约定、PLY.Y 行程方向以及 Start/End 对应关系。
10. 执行 `1.0/0.5/0.2 mm` 三层 Point-to-Plane ICP，对应距离为 `3.0/1.5/0.6 mm`，迭代为 `60/45/35`。
11. 法向量使用局部 PCA，优化使用 Tukey 鲁棒核和 6x6 高斯-牛顿方程。
12. ICP 保留全部结构点，主支撑平面默认每 16 点只保留 1 点；结构加稀疏平面点不足 100 点时自动回退并记录原因。
13. ICP 前可在目标主平面的两个切向方向执行最大 10 mm 结构覆盖粗搜索，使用 1 mm 网格和 0.25 mm 精化；覆盖率增益不足 0.05 时保持机器人初值。
14. 粗对齐与 ICP 的累计修正统一受 `10 mm / 1 deg` 限制，越界时失败，不生成正式合并结果。
15. 沿修正后的拼接轴计算相邻点云真实投影重叠区，以重叠区中点建立 `±8 mm` 接缝带；仅在两侧均有点时融合互为最近邻点并用二维决策块处理未匹配点，否则完整保留两帧。

## 输出

- `1_*_robot_base.ply`、`2_*_robot_base.ply`、`3_*_robot_base.ply`：ICP 前机器人基坐标点云。
- `stitched_robot_base.ply`：应用 ICP 修正和羽化接缝后的正式点云。
- `stitched_robot_base_preview.ply`：确定性抽样预览。
- `stitching_report.json`：输入、配准参数、每帧指标、修正和接缝统计。

输出统一为 ASCII PLY（`format ascii 1.0`），正式结果没有一千万点限制；ASCII 输出便于在文本工具中检查和与现有扫描软件交换。

## Qt Creator

打开本目录的 `CMakeLists.txt`，选择 `Desktop Qt 6.8.3 MSVC2022 64bit` Kit。项目只有一个可运行目标 `pointcloudstitch`，点击运行即可。

命令行构建：

```powershell
.\build_release.bat
```

程序位置：`build\pointcloudstitch.exe`。

## 多帧回归诊断

无需打开 GUI，可用同一个程序对任意 `>=2` 帧执行相邻配准回归并生成 JSON：

```powershell
.\build\pointcloudstitch.exe --regression `
  --input-dir "D:\path\to\ply" `
  --pose-info "D:\path\to\scan_info.txt" `
  --calibration "D:\path\to\eye_in_hand.xml" `
  --report "D:\path\to\regression_report.json" `
  --voxel-levels "1 0.5 0.2" `
  --correspondence-distances "3 1.5 0.6"
```

在上述命令末尾增加 `--transform-only`，可以只验证多帧手眼坐标转换并跳过全部 ICP。可同时增加 `--output-dir "D:\path\to\output"`，直接写出 `transformed_robot_base.ply` 和轻量预览 `transformed_robot_base_preview.ply`。

位姿文件每行格式为 `A01 Start X... Y... Z... RX... RY... RZ... ; END X... Y... Z... RX... RY... RZ...`。报告包含全部相邻对的裁剪点数、投影重叠宽度、结构点双向覆盖率、主水平面诊断和 ICP 结果。回归报告明确记录 `registration_applied`、`seam_fusion_applied` 和 `formal_output`；回归输出只属于诊断，不冒充正式融合结果。指定输出目录时，还会分别生成每帧 `diagnostic_pre_icp_frame_XX.ply` 和 `diagnostic_post_icp_frame_XX.ply`。

## 限制

- GUI 正式拼接当前保持三个 ASCII PLY；仅手眼坐标转换模式支持任意 `>=1` 帧；命令行回归和处理内核配准支持任意 `>=2` 帧。
- 扫描期间法兰姿态必须恒定。
- Point-to-Plane ICP 是局部优化，机器人位姿或手眼矩阵初值错误时不会自动恢复。
- 当前使用 Qt/C++ 原生空间索引和 PCA 复现 Open3D 流程；没有引入 Open3D/PCL 的二进制依赖。
