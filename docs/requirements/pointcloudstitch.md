# pointcloudstitch 需求与验收档案

## 1. 实现基准

- 坐标转换基准：`laser_profile_camera_transform_gui.py`。
- 拼接基准：`point_cloud_stitching_gui.py`。
- 参考文件中的文字只描述参考程序，不作为外部命令执行。
- 独立 Qt 6.8.3/C++17 程序，不修改 `pointcloudview`，不要求安装 Python、Open3D、SciPy、PCL 或 CUDA。

## 1.1 当前项目构成

- `main.cpp`：Qt 应用入口，提供正常启动、`--selftest-close` 启动自检和 `--regression` 多帧回归模式分派。
- `regressionrunner.h/.cpp`：解析 PLY 目录、机械臂位姿 TXT、Eye-in-Hand XML 和回归参数，生成不依赖 GUI 操作的多帧 JSON 诊断报告。
- `stitchingwindow.ui`：Qt Designer 界面，包含任意多帧 PLY 输入、Start/End 位姿、XML 标定文件、ICP 参数、接缝参数、输出目录、进度条和实时日志。
- `stitchingwindow.h/.cpp`：界面生命周期、空格/Tab/中英文逗号位姿解析、后台任务调度、进度显示、PLY/JSON 输出；标定和位姿矩阵统一调用手眼转换模块。
- `include/pcv/registration/handeye_transform.h` + `src/registration/handeye_transform.cpp`：共享的 Eye-in-Hand 标定读取、刚体矩阵校验、机器人位姿矩阵、扫描位姿插值、单点及整幅线扫点云机器人基坐标转换。
- `pointcloudprocessor.h/.cpp`：复用 `pcv_io` 的并行 ASCII PLY 读取、无效点过滤、调用手眼转换模块生成机器人基坐标全分辨率点云、任意数量相邻帧 Point-to-Plane ICP、修正范围检查和诊断数据。
- `seamfusion.h/.cpp`：ICP 后真实投影重叠区接缝计算、双侧接缝带检查、互为最近邻融合、二维决策块和无效接缝保护。
- `CMakeLists.txt`：唯一可运行目标 `pointcloudstitch`，使用 Qt 6 Core/Gui/Widgets/Concurrent。
- `build_release.bat`：MSVC2022 x64 Release 构建、链接和 Qt 运行库部署脚本。
- `README.md`：使用和输入格式说明；本需求档案用于记录实现边界、验收规则和变更历史。

当前处理链为：

```text
至少两个 ASCII PLY + 对应 Start/End + Eye-in-Hand XML
→ 过滤非有限点、(0,0,0) 和行程范围外点
→ PLY.Y 作为扫描起点有符号行程
→ [X,0,Z] 经 RTmatDepth2robot 和逐点机器人位姿转换到基座标
→ GUI 正式任务保存每帧 ICP 前全分辨率机器人基坐标 PLY
→ 匹配相邻帧主水平面并执行受限法向预对齐
→ 按输入顺序对所有相邻帧执行三层 Point-to-Plane ICP
→ 10 mm / 1 deg 修正安全检查
→ 根据 ICP 后真实投影重叠区确定接缝
→ 双侧有效时羽化融合，否则完整保留来源点
→ ASCII 正式点云、预览点云和 stitching_report.json
```

命令行回归可额外指定 `--runtime-root`、`--job-id`、`--workpiece-id` 和
`--base-name`，将报告及点云写入任务目录 `jobs/<job_id>/point_cloud/`；报告 schema
为 `pointcloudstitch-regression-v2`，路径字段相对于任务根目录。

手眼坐标转换是完整配准流程不可跳过的前置步骤。界面另提供“仅手眼坐标转换”模式：接受任意 `>=1` 帧，完成相同的逐点手眼坐标链后直接输出，不执行主平面预对齐、ICP、验收或接缝融合。该模式不得把未配准结果命名为 `stitched_robot_base.ply`。

当前明确不包含：FPFH/SAC-IA、PCL/Open3D 运行时依赖、自动改变手眼矩阵方向、超过实际覆盖范围的虚拟补面、正式结果点数上限和输出降采样。

## 1.2 当前已知边界

- 当前接缝保护能够避免因错误中垂面造成整片平面被裁掉，但不能恢复两帧都没有扫描到的真实区域。
- 缺口检测、缺口宽度分级、连续平面约束补点和补点来源标记尚未实现；出现中间空带时应优先增加拍摄重合范围。
- 推荐相邻帧实际有效投影重合至少 `30~50 mm` 或工件局部宽度的 `15%~25%`，最终以 `stitching_report.json` 的实际投影区为准。
- Point-to-Plane ICP 是局部精配准，仍依赖手眼矩阵、Start/End 反馈和 PLY 坐标模式提供正确初值；不得通过放宽 `10 mm / 1 deg` 安全范围掩盖坐标错误。
- 当前第一阶段已固定坐标契约：输入线扫点属于 `camera/depth`，手眼矩阵为 `flange_from_depth`，机器人姿态矩阵为 `base_from_flange`，输出点属于 `robot_base`；完整链为 `camera -> flange -> robot_base`。
- 当前第一阶段不建立工件坐标系，不生成工件坐标 PNG，也不发送机器人；这些属于后续阶段。

## 2. 输入契约

- 处理内核接受至少两个 ASCII PLY；顶点必须包含 `x/y/z`，其他标量属性可忽略。
- ASCII 输入通过与 `pointcloudview` 相同的 `pcv::detail::io::readPly()` 路径读取：优先内存映射，扫描换行边界后按原始顶点顺序分块，按文件规模自适应使用 1–4 个解析线程；映射失败时自动回退单线程。读取诊断记录工作线程数、边界扫描、解析和总耗时。
- GUI、`--regression` 模式和处理内核的配准与融合均接受任意 `>=2` 个 PLY；仅手眼坐标转换模式接受任意 `>=1` 个 PLY。
- 每帧包含 Start/End `X Y Z A B C`，其中 A=Rx、B=Ry、C=Rz；姿态单位为度，矩阵顺序为 `Rz(C) * Ry(B) * Rx(A)`。
- 位姿输入支持空格、Tab、英文逗号和中文逗号粘贴。
- 标定矩阵以 XML 的 `RTmatDepth2robot/RotMat/TVec` 为唯一权威来源。
- 线扫模式的 Start/End 必须形成非零扫描行程；Start/End 姿态允许不同，并按采样进度执行四元数 SLERP。

## 3. 坐标转换

- 公共接口定义在 `handeye_transform.h`。`loadHandEyeCalibration(...)` 读取并校验标定，`robotPoseToMatrix(...)` 和 `matrixToRobotPose(...)` 统一位姿约定，`transformPointToRobotBase(...)` 转换单点，`transformLineScanToRobotBase(...)` 转换当前线扫点云。
- 精确矩阵链为 `p_base = T_base_flange(t) * T_flange_depth * p_depth`，其中 `T_flange_depth` 直接来自 XML `RTmatDepth2robot`，禁止求逆或交换乘法顺序。
- 机器人位姿采用毫米和角度，数组顺序为 `[X,Y,Z,A,B,C]`，旋转约定为 `Rz(C) * Ry(B) * Rx(A)`。
- 剔除 `(0,0,0)` 和非有限点。
- 机器人 Start→End 位移绝对值最大的轴决定有符号行程 `signed_travel`。
- `ratio = PLY.Y / signed_travel`；仅接受 `-0.02 <= ratio <= 1.02`，转换时裁剪到 `[0,1]`。
- `profile = [PLY.X, 0, PLY.Z]`。
- Start/End 平移按 `ratio` 线性插值；旋转发生变化时，通过 4096 级查找表执行归一化四元数 SLERP，避免对千万点重复计算插值。
- `point_base = R_base_flange(ratio) * (R_flange_depth * profile + t_flange_depth) + t_base_flange(ratio)`。
- 法向量只应用 `T_base_flange(ratio) * T_flange_depth` 的旋转部分并重新归一化，不叠加平移。
- 输出同时维护转换后的完整点、按 `sample_stride` 取得的配准样本，以及 `current point index -> source PLY point index` 映射；无效点和行程外点分别统计。
- 每帧过滤后的所有点写入 ICP 前 `_robot_base.ply`；每 `sample_stride=8` 个有效点取一个配准样本。
- 标定矩阵、Start/End 矩阵必须通过有限值、齐次末行、单位正交旋转列和行列式接近 `+1` 的校验。空输入、零行程、全部点被过滤和用户取消均返回明确状态，不产生伪结果。

### 3.1 第一阶段元数据契约

- 每个转换点在内存中同步保存 `cloudId`、原始 PLY `sourceIndex` 和 `scanRatio`；其中 `cloudId` 对应报告 `frames` 数组下标，`sourceIndex` 是源 PLY 顶点索引，`scanRatio` 为 `PLY.Y / signed_travel` 并限制在 `[0,1]`。
- 每帧报告保存源文件、`start_base_from_flange`、`end_base_from_flange`、`flange_from_depth`、声明点数、转换点数、无效点/行程拒绝数、PLY.Y 范围、主行程轴和有符号行程。
- 正式输出 PLY 旁生成同名 `.pcvmap` 文件。格式为 little-endian `PCVMAP1`：`uint32 magic=0x314D4350`、`uint32 version=1`、`uint64 point_count`，随后每点写入 `int32 cloud_id`、`int64 source_index`、`float scan_ratio`。该文件与正式输出点按顺序一一对应，禁止用于替代 PLY 坐标数据。
- JSON 报告的 `schema` 为 `pointcloudstitch-report-v2`，并记录 `coordinate_contract`、单位、`frames`、`point_mapping` 和 `.pcvmap` 路径；不会把千万级映射数组直接膨胀写入 JSON。
- 接缝融合产生插值点时，几何坐标按融合算法计算，来源帧和来源索引选择实际保留的一侧，`scanRatio` 同步采用该来源点的值；因此映射始终可追溯，但插值点不宣称为新的原始采样点。

## 4. 配准

- 顺序固定为所有相邻关系 `scan N -> scan N-1`，不存在跨帧回环约束。
- 每个相邻对在各自未修正的机器人基坐标样本上独立求局部修正，避免上一对结果污染下一对的主平面和 ICP 验收；正式全分辨率输出使用 `C_global[N] = C_global[N-1] * C_local[N]` 组合修正。
- 某一对失败后，后续相邻对仍以可用机器人初值继续诊断，确保报告覆盖整组数据；只要任一对失败，整组正式配准仍判定失败。
- ICP 前分别构建 `0.5 mm` 高度直方图，源帧选择密度最高主水平面，目标帧从有效高度峰中选择最接近的对应平面；每侧在峰值 `+-1.5 mm` 范围内使用 PCA 拟合法向。
- 主平面预对齐只允许法向平移和法向倾斜校正，不允许平面内平移；旋转必须围绕源平面质心执行。
- 默认主平面预对齐限制为 `6 mm / 0.5 deg`。检测到对应平面但修正超限时立即拒绝；没有可靠水平面时保持机器人初值并继续记录诊断。
- 配准前必须从全部帧提取候选水平面，并在默认 ±5 mm 高度容差内选择能连续覆盖全部帧的同一平面身份轨迹；每个相邻对使用各自跟踪高度，不得因局部峰值更强而切换到另一物理表面。无法覆盖全部帧时回退原相邻匹配并记录原因。
- 主平面法向采用高度候选带和残差内点迭代拟合，默认高度带 ±3 mm、残差内点 ±0.8 mm、3 次迭代；该步骤只提高法向估计稳定性，不改变 `0.5 deg` 预对齐安全阈值。
- 预对齐报告记录修正向量、修正角、修正后平面残差和法向夹角，以及修正前后的最近距离分位数与覆盖率。
- 两侧样本使用相同的包围盒交集，默认向外扩展 `5 mm`；主平面检测保留外扩上下文，ICP 对应必须同时位于主平面预对齐后整幅相邻帧的无外扩真实三维 AABB 交集内。
- 体素层级：`1.0 / 0.5 / 0.2 mm`。
- 对应距离：`3.0 / 1.5 / 0.6 mm`。
- 最大迭代：`60 / 45 / 35`。
- 目标法向量搜索半径：`max(voxel*4, correspondence*1.5)`，最多使用 50 个邻点进行 PCA。
- 最小化 `sum(((R*s+t-d) dot n)^2)`，使用 Tukey 核和退化感知高斯-牛顿方程。旋转雅可比围绕当前重叠对应点质心构建，并按对应点 RMS 半径与平移列统一尺度，避免机器人基坐标原点引起旋转/平移耦合。
- 对归一化 6x6 对称 Hessian 做 Jacobi 特征分解；相对最大特征值低于 `1e-3` 的方向视为不可观测并从增量中移除。报告记录 `observable_dof`、`hessian_condition_ratio`、是否退化及投影迭代次数。
- 每层以仅应用主平面预对齐时的实际重叠源点为基准；每次增量至少保留 `85%`，不满足时依次尝试 `1/2、1/4、1/8` 增量，仍不满足则拒绝。
- 相对 Fitness 与 RMSE 变化同时不大于 `1e-7` 时允许提前收敛。
- 每轮累计修正必须不超过默认 `10 mm / 1 deg`；越界或对应不足即失败。
- ICP 增量采用安全边界信赖域：候选增量将超过 `10 mm / 1 deg` 时依次尝试 `1/2、1/4、1/8`；均越界则停在最后安全解并继续结构覆盖验收，不得应用越界矩阵。
- ICP 默认保留全部结构点，并将主支撑平面降为每 16 点保留 1 点；结构加稀疏平面点任一侧少于 100 点时自动回退原重叠点集，并记录 `structural_icp_used` 和原因。
- ICP 前执行有界结构覆盖粗搜索：目标主平面的两个切向平移默认半径 10 mm、粗步长 1 mm、精化步长 0.25 mm，并绕平面法向搜索 ±1 deg、步长 0.25 deg；候选评分距离 5 mm，双向覆盖率增益低于 0.05 时不得应用。最终验收仍使用 3 mm，粗旋转和平移计入总 `10 mm / 1 deg` 安全范围。
- 所有相邻配准均执行结构点双向覆盖验收：源到目标和目标到源的 3 mm 覆盖率取较小值，低于 0.65 时拒绝；不得因 Hessian 可观测自由度较多而跳过。该阈值和点数门槛写入报告。
- 每对额外执行只读大范围结构诊断：默认平面内 ±120 mm（10 mm 粗步长、2 mm 精化）和绕法向 ±3 deg（0.5 deg 步长），记录最佳 5 mm 双向覆盖率和候选位姿。该候选不得写入修正矩阵或正式输出。
- 每对在 ICP 前记录两侧裁剪点数、沿拼接轴投影重叠宽度、最近距离 `P50/P90/P95`、`3/5/10 mm` 覆盖率以及主水平面 PCA 的法向夹角和带符号平面差。

## 5. 羽化接缝

- 机器人轨迹只用于确定相邻扫描的排序和拼接轴方向，不再直接用轨迹中点作为接缝位置。
- 对已完成坐标转换及 ICP 修正的相邻真实点，沿拼接轴分别计算投影范围；接缝取真实重叠区 `[max(minA,minB), min(maxA,maxB)]` 的中点。
- 仅当真实重叠区有效且接缝 `+-8 mm` 带内两侧均有点时，才执行半空间裁剪和羽化融合。
- 若投影区无重叠或接缝带只有单侧点，禁止裁剪并完整保留两帧，报告原因为 `seam_outside_actual_overlap`。
- 扫描必须沿一个拼接方向严格排序，否则拒绝重叠裁剪。
- 默认接缝半宽 `8 mm`、互为最近邻距离 `0.6 mm`、二维决策块 `0.5 mm`。
- 互为最近邻点按其接缝带位置进行线性插值。
- 未匹配点投影到“中垂面法向距离 + 扫描行程方向”二维格；同一格只选择一侧来源，选择概率随接缝位置渐变并由坐标哈希确定。
- 接缝带之外只在接缝通过双侧有效性检查后保留对应扫描的核心区域点。

## 6. 输出与界面

- 输出 PLY 统一为 ASCII 1.0，仅包含 `float x/y/z`；正式点云和预览点云均可直接用文本工具查看及交换。
- 仅手眼坐标转换模式输出每帧 `*_robot_base.ply`、合并的 `transformed_robot_base.ply`、`transformed_robot_base_preview.ply` 和 `coordinate_transform_report.json`；所有 ICP 修正矩阵必须为单位矩阵。
- 正式点云不进行点数封顶或输出降采样；预览允许独立确定性抽样。
- GUI 实时显示读取、转换、各相邻帧配准、三层 ICP、接缝、写文件和报告进度。
- `stitching_report.json` 记录实际体素、对应距离、抽样步长、重叠边距、安全范围、每层指标和最终修正。
- `--regression` 在指定路径生成 `pointcloudstitch-regression-v2` JSON，记录处理、接缝和正式输出状态，适用于参数和算法回归。
- GUI 和回归报告必须明确记录 `registration_applied`、`seam_fusion_applied`、`formal_output`；未通过验收时只能生成诊断文件，不得使用正式拼接文件名。
- 配准模式分别输出每帧 ICP 前后确定性抽样诊断 PLY，便于逐帧对比机器人初值和实际修正；诊断点云不替代全分辨率正式输出。
- 接缝报告记录 `projected_range_a/b`、`actual_overlap_interval`、`seam_projection`、两侧带点数及是否实际应用裁剪。
- Qt Creator 打开 `CMakeLists.txt` 后必须只有一个可运行目标 `pointcloudstitch`。

## 7. 验收

- Release 必须通过 MSVC2022 64-bit + Qt 6.8.3 构建。
- GUI 配准输入少于两帧，或任一入口不满足 ASCII、恒姿态、有符号行程约束时必须给出明确错误。
- 仅手眼坐标转换模式允许一个或多个有效 PLY；转换成功即允许输出，不得因 ICP 门限或相邻覆盖率拒绝。
- 任一相邻帧 ICP 失败时不得生成 `stitched_robot_base.ply`，可输出诊断点云和报告。
- 失败运行必须移除输出目录内同名旧 `stitched_robot_base` 正式文件，避免历史成功结果被误认为当前输出。
- 每帧 `_robot_base.ply` 必须是 ICP 前坐标；正式合并结果必须应用对应修正。
- 参考数据回归需覆盖全部相邻对，并对比每帧有效点数、裁剪点数、距离分位数、实际投影重叠、平面诊断、每层 Fitness/RMSE/对应数和修正矩阵。

## 8. 变更记录

- 2026-08-18：删除原独立版全部旧点云处理器及从 `pointcloudview` 遗留的平面、圆孔、补全、降噪和旧配准代码。
- 2026-08-18：按两个 Python 参考文件重写三帧坐标转换、Tukey Point-to-Plane ICP、相邻配准顺序和二维羽化接缝流程。
- 2026-08-18：标定改为直接读取 `RTmatDepth2robot/RotMat/TVec`；来源点云恢复为 ICP 前机器人基坐标语义。
- 2026-08-18：ASCII PLY 读取继续在配准后台任务中执行，按 Header 顶点数预分配，并使用复用的 64 KiB 行缓冲减少逐点临时分配；超长顶点行明确失败。
- 2026-08-20：GUI 和命令行回归输出统一改为 ASCII PLY（`format ascii 1.0`），保留全分辨率正式输出和独立抽样预览，不再生成 binary little-endian PLY。
- 2026-08-20：`pointcloudstitch` 删除独立的单线程逐行 ASCII 解析路径，改为直接复用 `pointcloudview` 的 `pcv_io` 文件映射并行读取器；保持顶点顺序、源索引、过滤和坐标转换语义不变。
- 2026-08-18：确认手眼转换继续严格采用 Python 的 `T_base_flange(Y) * RTmatDepth2robot * [X,0,Z,1]`，不反转 XML 矩阵；定位三段断开来自轨迹中垂面偏离真实点云重叠区。
- 2026-08-18：接缝改为 ICP 后真实投影重叠区中点，并加入双侧接缝带有效性保护；无有效双侧数据时禁止破坏性裁剪。
- 2026-08-18：完成新方案第一阶段：处理内核和接缝支持任意 `>=2` 帧，相邻帧失败后继续诊断后续对，新增距离分位数、覆盖率、投影重叠及主水平面诊断。
- 2026-08-18：同一可执行程序新增 `--regression` 模式，可直接读取 PLY 目录、位姿 TXT 和标定 XML 并生成四帧及更多帧的回归 JSON；Qt Creator 仍只有一个运行目标。
- 2026-08-18：新增独立 `handeye_transform` 模块，将 XML 标定解析、`Rz*Ry*Rx` 位姿约定、单点/线扫点云坐标转换从界面和处理器重复实现中收敛为公共 API；处理链统一使用 `T_base_flange(t) * T_flange_depth * p_depth`。
- 2026-08-18：Start/End 姿态变化改用 4096 级四元数 SLERP 查找表，完整保留源点索引、过滤计数和取消状态；新增独立测试覆盖 XML、矩阵链、扫描始末点、旋转约定、旋转插值、法向量转换和索引映射。
- 2026-08-18：MSVC x64 Release 完整目标和 `handeye_transform_tests` 构建成功；最终 CTest 结果为 `1/1 passed`。
- 2026-08-19：完成近期开发第一阶段：统一 `camera -> flange -> robot_base` 坐标契约；转换结果新增扫描进度映射；报告升级为 `pointcloudstitch-report-v2`，记录每帧矩阵、来源文件和过滤统计；正式 PLY 新增同名 `PCVMAP1` 二进制来源映射旁车文件。
- 2026-08-19：新增测试覆盖 `scanRatio` 起止值、源点索引和旋转插值；MSVC x64 Release 构建及 CTest 通过（`1/1 passed`）。
- 2026-08-19：加入实际相邻帧重叠区域约束和退化感知 ICP；旋转改为围绕重叠对应点质心求解，Hessian 经尺度归一化后只在可观测特征子空间求解，抑制大平面退化滑移。
- 2026-08-20：完成第二阶段结构优先 ICP；保留全部结构点并对支撑平面进行 1/16 确定性抽样，避免纯结构硬切除失去高度约束；点数不足时安全回退并保留可审计诊断字段。
- 2026-08-20：完成验收可信化第一阶段：回归模式改为 `registration_only`，报告增加实际处理/接缝/正式输出状态，生成 ICP 前后逐帧诊断 PLY，并以结构点双向 3 mm 覆盖率拒绝大平面退化下的虚假成功。
- 2026-08-20：完成第三阶段有界初值改进：新增目标平面切向的结构覆盖粗搜索，以双向覆盖增益决定是否应用，并将粗对齐和 ICP 修正纳入同一安全范围。A01-A04 回归中 A01-A02 增益不足而保持初值，后两对在 10 mm 半径内仍无结构覆盖，确认不得继续放宽 ICP 伪造成功。
- 2026-08-20：新增跨帧主平面身份跟踪，使用全组候选高度选择连续平面轨迹，避免同一帧在前后相邻对中切换到不同高度的物理表面；跟踪状态、共识高度和逐帧高度写入报告。A01-A04 回归统一跟踪到约 1019.25 mm；平面切换已消除，但 A01-A02 法向差 0.6268 deg，后两对仍需 10.61/11.66 mm 修正，因此继续拒绝正式输出。
- 2026-08-20：进入下一阶段鲁棒平面拟合：在跟踪平面高度附近扩大候选带，并通过平面残差内点迭代重新估计法向；保持原有预对齐角度安全阈值不变。
- 2026-08-20：鲁棒平面拟合在 A01-A04 回归中将三组平面法向差降至 0.059/0.056/0.079 deg，验证平面法向问题已改善；剩余失败来自 ICP 总修正超限 7.13 mm/1.056 deg、10.48 mm/1.021 deg 和 10.31 mm/0.661 deg，正式输出继续拒绝。
- 2026-08-20：反向 Start/End 对照导致每帧约 463 万至 512 万点因行程符号相反被过滤，确认负 PLY.Y 对应当前 150 到 -150 mm 轨迹方向；粗搜索新增平面内 ±1 deg 旋转自由度，且不改变最终安全阈值。
- 2026-08-20：ICP 修正限制改为安全边界信赖域，避免有效解因单次增量从边界内轻微过冲而直接丢弃；所有已应用修正仍严格位于 10 mm / 1 deg 内，最终是否通过继续由结构双向覆盖决定。
- 2026-08-20：信赖域回归暴露非退化对绕过结构验收的假成功，已将双向 3 mm 结构覆盖提升为所有相邻对的硬门槛，并在失败时清除同名旧正式输出。
- 2026-08-20：修复后 A01-A04 信赖域回归分别停在 6.96 mm/0.995 deg、9.99 mm/0.967 deg、6.23 mm/0.131 deg，结构双向 3 mm 覆盖为 0.1098/0/0；三对全部正确拒绝，报告为 `success=false`、`formal_output=false`，仅保留诊断文件。
- 2026-08-20：完成只读大范围结构诊断。搜索扩展至平面内 ±120 mm、绕法向 ±3 deg 后，A01-A02、A02-A03、A03-A04 的最佳 5 mm 双向覆盖仅 0.2335、0.0798、0.0420，候选平移仍约为 -49.8 mm 而非继续到 -100 mm；排除简单 X 位姿整段偏移，后两对缺少足够共同工件结构。
- 2026-08-18：完成新方案第二阶段：新增对应主水平面峰匹配、围绕源平面质心的受限法向预对齐及 `6 mm / 0.5 deg` 独立安全检查；不引入任何平面内位移。
- 2026-08-18：新增可切换的“仅手眼坐标转换”模式，允许一个或多个 PLY 按 `T_base_flange(t) * T_flange_depth * [X,0,Z]` 输出机器人基坐标点云；该模式明确跳过预对齐、ICP、验收和接缝融合，并提供独立合并、预览及 JSON 报告文件。
- 2026-08-20：GUI 配准模式取消固定三帧限制，改为接受任意 `>=2` 个 PLY；文件和文件夹添加均不再截断数量，单帧配准明确拒绝，仅手眼转换仍允许单帧。
- 2026-08-24：`pointcloudstitch` 和 `pcv_interface` 统一改用 `pcv_registration`；应用 CMake 不再直接编译旧 `handeye_transform.cpp`，处理器、窗口和测试改为引用公共头并链接共享库。迁移未改变 GUI、回归参数、ICP 和接缝行为，但仍需重新执行 `handeye_transform_tests`、应用构建和完整拼接回归。
