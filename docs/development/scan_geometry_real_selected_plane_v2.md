# 当前 GUI 接入真实所选平面拼接 v2

日期：2026-10-26。Owner：Codex（实现），用户（构建与验收）。文档基线 v0.5，软件版本、JSON/PLY/PCVMAP schema 均未升级。入口仍为 `scan_geometry_demo_v1`，原 `pointcloudview` 保留。本记录描述本轮源码，不声明构建或验收通过。

## 交付与架构

```
scan_geometry_window_v1.ui / input_config_dialog_v2.ui
    → ScanGeometrySessionV2（GUI 线程状态、审核、生命周期）
    → pcv_selected_plane_tasks_v2（应用层任务）
    → 10 读取 → 50 LineProfileXz/robot_base → 60 索引/人工选面/参考匹配
    → 全部人工审核 + 批量确认 → 30 stitchSelectedPlanes → 10 ArtifactTransaction
    → ScanGeometryCanvasV2 → 20 PointCloudCanvas
```

`apps/stitching_ui/selected_plane_tasks_v2.*` 从旧对话框提取读取/坐标转换、可配置上限的展示抽样、完整索引与选面快照。共享默认及旧 `SelectedPlaneDialog` 上限为 300000；新 GUI 显式请求每帧最多 1000000。没有隐藏旧窗口或访问其私有控件；领域模块不反向依赖应用。

`pcv_selected_plane_tasks_v2` 链接 10/30/50/60、Qt Core/Gui/Concurrent；`pcv_scan_geometry_session_v2` 组织新流程；原 `pcv_scan_geometry_demo_view_v1` target 加载真实 session/dialog/canvas。`demo_cloud_v1`、旧演示模型/画布与 v1 测试源码保留为历史，不在正式窗口运行链路中。旧演示模型测试仍可构建，旧模拟窗口测试不再注册，正式窗口由 `scan_geometry_view_tests_v2` 覆盖。

## 输入、审核与调度

- PLY 列表有序，任意不少于两帧，每轮至多六帧；逐帧 Start/End 与共用 XML 输入保留原契约。新输入行位姿留空，必须输入有限数值。输出目录要求源码树外绝对路径。
- 六帧窗口保留当前轮完整源数据与索引；跨轮释放源点、展示、索引及展示预算租约，保留完整所选平面业务快照与来源映射。切回帧调用共用加载，按已转换完整点/原始来源索引/scan ratio 的 SHA256 摘要复核。摘要为当前进程加载一致性，不是原始文件字节签名或持久 schema。
- 最多两个帧级加载任务，各最多两个计算 worker；人工选面和自动匹配串行派发、单任务最多四个 worker，自动匹配按帧有序。新 GUI 共享 512 MiB 预算，展示点、完整业务数据、历史快照、显示配色及撤销历史不因此受硬上限保护。
- 首个未跳过帧人工确认参考面；此后参考固定。自动候选不自动审核、确认或拼接；每帧需显示并点击确认当前候选。全部有效帧审核后再次批量确认，至少两帧方可手动拼接。跳过/候选变更使批量确认失效。
- 匹配异常暂停派发并定位帧；人工修正确认或明确跳过后继续。主页面可以切换三点备用，无需破坏既有输入。候选与干涉点重叠要求用户处理，不静默删工件点。
- 输入 revision、参考 revision、稳定帧 ID、取消 token 共同门控后台结果。过期结果只回收；取消后等待全部活动任务结束再接受重试。线程池有界，关闭先取消/join。拼接 progress 通过 queued invoke 发布；无后端百分比则显示阶段，不制造百分比。

## 模块 60 新 API

头：`pcv/planefitting/reference_plane_match_v2.h`；入口：`pcv::planefitting::matchReferencePlaneV2`。实现位于已有 `single_click_plane_selection_v1.cpp`，直接复用不可变索引及其局部模型，调用 `selectPlaneAtPoint` 完整扩展，不增加 RANSAC/PCA 实现，不属于模块 40 的位置确认。

- 完整目标点云及索引必须对应同一缓冲，坐标为 `robot_base/mm`。
- 默认距离 10 mm、法向角 5°、最多 16 个确定性分散种子；法向比较使用绝对点积，正负号等价。按索引稳定 bucket 顺序均匀取种子；先筛局部法向及参考垂距，再对完整选面模型/中心复核。
- 完整候选索引排序后，以交集 / 较小候选点数 ≥ 80% 合并，保留较大候选；发现两个不同合格候选即可返回 `Ambiguous`，避免继续占用完整候选内存。唯一性只相对本次有限种子搜索，不声明全局几何唯一或自动业务对应成功。
- 结果区分 `Unique/Ambiguous/NotFound/Cancelled/InvalidInput/Error`，返回完整索引、模型、成功种子、实际尝试种子序列、角度/距离与候选/搜索计数；取消/错误不发布完整候选半成品。
- `Unique` 通过共用选面快照任务构建已有 `SelectedPlaneFrame`，在现有 `selectionParameters` 记录参考帧 ID/来源、实际种子和搜索参数。最终垂距仍为原 `surfaceToleranceMm`；不翻转、不补点、不压平。

## 输出、修剪与确认

30 接收已在 `robot_base` 的完整所选平面快照，默认受限精配准，保留仅位姿开关、完整实测点序及重叠双份。正式输出沿用 `selected_plane_robot_base.ply/.pcvmap` 和 `selected_plane_manifest.json`。独立 `selected_plane_<timestamp>_<UUID>` 目录，禁止默认覆盖。

正式 `formal_output` 仅为事务提交成功；界面显示 `warn_only`、逐对精配准/回退诊断及产物路径。提交成功后 GUI 取消不会假装删除正式文件。取消/失败不发布半成品 GUI 结果。

原始帧拾取使用帧 ID 与完整点下标；展示抽样仅改变绘制。结果保持不可变完整数据，修剪保存原结果下标集合和撤销栈，GPU 可见点拾取经 display→原结果下标映射。擦除请求同时校验结果 revision，迟到旧选区不能影响新结果。恢复全部也入撤销栈。2026-10-29 起，三个非共线工件点门控会话 WObj1 生成，WObj1 与修剪/重选/重载等状态变化联动失效，并由应用保存为 `selected_plane_geometry_v1.json` 和独立修剪 PLY/PCVMAP；完整磁盘结果仍保留。没有设备控制、IK、碰撞或安全判定。

## 构建与测试命令（供用户执行，本轮未运行）

在配置好 Qt/MSVC 的 x64 开发终端执行；将 Qt 路径替换为本机 Kit 的实际安装路径。构建目录均为新建源码树外目录。

```powershell
$env:VSLANG = '1033'
$qtPrefix = '<Qt 6.8.3 MSVC x64 prefix>'
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/scan-geometry-real-v2-debug -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$qtPrefix" -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON -DPCV_BUILD_POINTCLOUDVIEW=ON -DPCV_BUILD_POINTCLOUDSTITCH=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF -DPCV_BUILD_TESTS=ON
cmake --build D:/pcv-build/scan-geometry-real-v2-debug
ctest --test-dir D:/pcv-build/scan-geometry-real-v2-debug --output-on-failure
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/scan-geometry-real-v2-release -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qtPrefix" -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON -DPCV_BUILD_POINTCLOUDVIEW=ON -DPCV_BUILD_POINTCLOUDSTITCH=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF -DPCV_BUILD_TESTS=ON
cmake --build D:/pcv-build/scan-geometry-real-v2-release
ctest --test-dir D:/pcv-build/scan-geometry-real-v2-release --output-on-failure
```

上述 NMake 命令不请求并行 make；勿切到 JOM。也可使用仓库 `scripts/build_windows.ps1` 设置现有 Kit。单 GUI 依赖另用新的外部目录配置 `PCV_BUILD_POINTCLOUDVIEW=OFF`、`PCV_BUILD_POINTCLOUDSTITCH=OFF`、`PCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON`、`PCV_BUILD_EXPERIMENTAL_CAMERA=OFF`。根 CMake、模块 20/30 条件和共享任务依赖已补齐，但组合尚未实际配置验证。

新增自动测试源码：

| target | source_scope |
|---|---|
| `reference_plane_match_tests_v2` | 唯一候选、反号法向、多密度分离共面区、超门限、索引错配、取消、1/2/4 worker 等价、展示抽样及来源摘要 |
| `scan_geometry_session_tests_v2` | 2/6/7/13 帧加载、真实 PLY/XML→参考选面→匹配审核→输出、PCVMAP 对齐、取消重试/迟到结果、错误 XML/非法位姿、审核/跳过/参考失效、修剪/恢复/撤销及磁盘不变 |
| `scan_geometry_view_tests_v2` | 原生窗口控件/初始门禁、输入 Designer 表格、最小尺寸设置、默认外观兼容、GPU 可见点遮挡及不连续完整下标映射 |

旧 `selected_plane_dialog_tests_v1`、渲染契约和所选平面拼接测试继续注册。新增测试使用源码树外临时目录与合成数据；测试通过也不能替代现场数据和人工交互验收。

## 用户验收清单

所有条目当前均为 **未执行**，由用户记录实际命令、结果和截图，不沿用 v1/历史结果。

- 输入：2/6/7/13 帧排序/移除/逐行位姿、Unicode 路径、非法/缺失 XML、零行程或非有限位姿、文件失效、加载取消和重试。
- 匹配：相同面/法向反号/不同密度/超门限/多共面区域；固定首个参考不漂移；失败暂停、人工单击/三点修正、明确跳过、候选全部逐帧预览后批量确认。
- 门禁：未确认参考、存在未审核/未解决异常、少于两帧、批量确认后再重选或跳过时禁止拼接；参考改变使自动候选和结果失效。
- 重载：跨轮保留完整候选，重载同一来源不自动确认；改变源文件/位姿后旧候选失效，显示抽样不改变完整点数、点序和来源。
- 输出：完整实测点与重叠双份、PCVMAP逐点对应、精配准/仅位姿/回退及 `warn_only` 说明、独立目录不覆盖。干涉标注和会话修剪不进入正式产物。
- 编辑：空框、小范围误拖、旋转/平移/缩放/适配后旧框取消、遮挡不穿透、修剪后映射正确、多次撤销、恢复可撤销、全擦除禁确认、非共线校验、关闭丢失提示。
- 生命周期：读/索引/fit/匹配/拼接各阶段取消，迟到输入/参考/token 结果不发布，关闭 join；事务恰已提交时正确保留并报告路径。
- 兼容/展示：旧选面对话框完整操作回归及原黑底默认外观；新 GUI 浅灰画布；Qt Creator 编辑两份 `.ui` 并运行新 target；1024×640 与 1280×800，100%/150% DPI，工具/禁用/焦点/长错误/长路径/滚动。
- 视觉：与原型对应区域同状态并排比较。保留浅色结构、蓝工件/黄人工标注；删除模拟扫描/急停、改为真实输入/匹配审核是本轮已批准差异。没有本轮截图，不宣称像素一致。

## 风险与回滚

本轮不执行任何构建或验收，因此编译/API、原生 OpenGL、DPI、真实平面搜索成功率和大数据内存/耗时仍待用户确认。有限种子可能漏检小候选；三个非共线点仅为会话确认门槛，不是机器人可执行性或严格配准质量保证。业务快照和撤销历史随帧数/修改次数增长，需要真实规模评估。

工作区原有修改已保留，未提交/推送/上传。本轮前文本基线位于 `D:/pcv-build/scan-geometry-real-v2-20261026/baseline-files.json`、`baseline.diff` 与 `baseline-status.txt`；仅用于识别本轮增量，回滚时逐块对照，不整体覆盖当前文件或回退其他任务。原 ZIP、截图和 v1 历史 QA 均保留。

交付补充：点击工件控制点开始重选就失效旧候选/批量确认/结果，未完成三点选面不能继续沿用旧确认；重选参考同时清除其自动候选。“重载 / 重试当前帧”会重新读取完整源并核对摘要，摘要还包含来源路径、声明点数、Start/End/手眼矩阵与 signedTravel，防止点序相同但来源契约改变而复用旧快照。读取徽标区分已完成读取帧数与当前驻留帧数，真实阶段仍显示在处理卡。关闭 join 期间恰已提交的产物会在关闭前单独提示路径。上述均为源码行为说明，本轮未实际运行。
任务门控补充：LoadDisplay 的读取/转换结果与参考面无关，只校验输入 revision/token/稳定帧 ID；Fit/Match/Stitch 还校验参考 revision。同轮重载发现参考源变化时不会误丢其他在读帧。未完成控制点选择会阻止启动/继续匹配和批量确认，Esc 或明确跳过可结束控制点选择。此处为实现说明，未作为运行验证结果。

## 2026-10-30 转换预览与高侧平面提醒

- 原始转换帧采用 pointcloudview 默认彩色高度模式、点大小 2、叠加 1.0，色阶由完整 robot_base 点云的有限 Z 范围计算。高度色为未标注点底色；工件候选/确认点蓝色、人工干涉预览/确认点红色使用模块 20 独立 marker 通道，以放大点径和深/浅双层边缘提高对比；不改变旧调用方默认外观。
- 模块 60 的 `pcv/planefitting/high_side_plane_candidates_v1.h` / `findHighSidePlaneCandidatesV1` 复用不可变完整索引和 `selectPlaneAtPoint`，按确定性桶种子搜索，返回 seed、点数和候选中心 Z 减工件面中心 Z 的高度差；不返回或长期缓存所有候选点集，也不计算平面法向。候选支持按索引重合去重，并排除与已选工件/干涉点集高度重合区域。
- 应用默认世界 Z 高差门限为 2 mm、100 点，最大种子数复用参考匹配设置（默认 16），前两项在 Designer 高级输入对话框调整。检测在选面及自动匹配候选形成后、同帧重载后运行；任务依赖输入/参考 revision 与取消 token。候选以数量/高度/点数显示。“标注为干涉面”运行完整现有选面并生成待确认红色预览；人工确认后才写入人工标注。忽略仅记录当前帧/工件候选状态，不影响拼接门禁。更换工件候选或输入来源后清理忽略状态。
- 新增 `high_side_plane_candidates_tests_v1` 源码和会话/UI 断言，覆盖高侧/反号法向、门限、点数、排除、取消、软提醒不阻止拼接及 Designer 控件。构建与测试由用户执行，本轮未运行 CMake、编译、CTest、GUI、截图或性能验收。
- 用户执行定向测试时可使用：`ctest --test-dir <build目录> --output-on-failure -R "high_side_plane_candidates_tests_v1|scan_geometry_session_tests_v2|scan_geometry_view_tests_v2"`。还需检查不同高度/倾斜参考面真实数据、超过 16 个局部候选的漏检、100%/150% DPI 及旧 pointcloudview 默认外观。
- 标注增强后追加检查：运行 `scan_geometry_view_tests_v2`，确认 color mode 0 中 marker 映射到稀疏显示点，放大点及明暗边缘在不同高度底色和 100%/150% DPI 下可辨；旧纹理 overlay 和 pointcloudview 默认外观无回归。

## 2026-10-30 拟合平面掩码叠加显示

- 工件面拟合候选/确认状态显示蓝色半透明平面与闭合边界；干涉面待确认/已确认状态显示红色半透明平面与闭合边界。点云原色、高度色、选择快照、拼接输入和磁盘结果保持不变。候选和确认使用相同颜色，替换/清除时由会话发布对应当前掩码。
- 模块 60 新增 `buildPlaneMaskV1()`：完整拟合点索引投影到拟合模型平面，以选面结果的 U/V 采样间距和 U 轴基向量建立占用网格，生成贴合平面的三角形及闭合边界环；孔洞单独保留，凹边界不以凸包填平。三点选面缺少采样轴时使用确定性平面基；无可用采样间距时从完整点集投影密度估算。生成支持取消、完整下标校验和共享处理预算；失败清空部分结果，拟合成功本身仍可供预览确认。
- 模块 20 新增 `PlanarMaskOverlay` GPU 层，25% alpha 面片使用 EBO 绘制，外环/孔洞环使用 DPR 缩放宽度的 line strip。替换完整/显示点云时清除来源相关旧 overlay；黄色 `Contour` 通道、原始点色和既有调用方默认行为不变。应用将工件面、干涉面及待确认干涉面掩码分别保存在帧状态，掩码错误按类别在操作提示 tooltip 报告。
- 新增 `plane_mask_tests_v1` 源码覆盖孔洞环、倾斜平面贴合、闭合边界、取消、越界索引和处理预算不足；Canvas contract 测试覆盖闭合轮廓要求；GUI view 测试覆盖蓝/红覆盖、显示映射及更换点云清理旧掩码。源码树外验证建议：`cmake --build <build目录> --target plane_mask_tests_v1 pointcloud_canvas_contract_tests scan_geometry_view_tests_v2`，随后 `ctest --test-dir <build目录> --output-on-failure -R "plane_mask_tests_v1|pointcloud_canvas_contract_tests|scan_geometry_session_tests_v2|scan_geometry_view_tests_v2"`。
- 按此前分工，本轮未运行配置、构建、CTest、GUI、截图或 DPI 验收。用户侧重点：Qt Creator 编辑 `.ui` 并运行 `scan_geometry_demo_v1`；分别确认工件/干涉选面拟合后显示蓝/红贴面区域，凹边与孔洞可见，换帧/重载/清除无旧网格残留；检查密集点云、预算失败提示、100%/150% DPI 和旧 pointcloudview 黄色轮廓默认外观。风险：占用网格边缘精度受 U/V 采样间距影响；全帧长期缓存的掩码会增加内存，有限预算可能拒绝生成但不影响拟合/拼接。

## 2026-10-30 标注可见性与世界 Z 高侧提醒

- 修正 GUI 蓝/红颜色覆盖未进入 color mode 0 显示通道的问题。模块 20 新增逐点 marker buffer，GPU 绘制大点、深色外缘、浅色内缘和颜色核心；点径随设备像素比缩放。原纹理颜色覆盖与默认渲染保持原行为。
- 高侧提醒高度定义为候选完整点集包围盒中心 Z 减已选工件面中心 Z。门限仍为默认 2 mm、候选最少 100 点、最多 16 个确定性种子；候选识别、去重、排除和取消沿用现有流程。倾斜工件按世界 Z 中心高度近似解释。
- 新增 marker channel 视图测试断言与世界 Z 候选测试；按用户分工未执行构建、CTest、GUI 或 DPI 验收。命令沿用上方定向测试列表。

## 2026-10-30 画布浅灰与百万点显示

- 新 GUI 的画布背景、宿主和外框改为 `#eef1f4`，空状态文字改为深灰；PointCloudCanvas 和旧应用的默认黑底不变。
- 共享 `prepareDisplayCloudV2()` / `loadSelectedPlaneFrameV2()` 增加可选 `displayPointLimit`，默认仍是 300000。新 GUI 显式使用每帧 1000000 点，旧选面对话框维持默认。均匀抽样仍确定性，完整点云、来源数据和显示下标映射保持不变。
- 新 GUI 会话预算从 256 MiB 提高至 512 MiB；六帧百万点映射预算申请共 384 MB（约 366 MiB），余额供既有可选优化缓冲使用。该预算不限制完整业务数据、GPU 资源或进程 RSS。
- 新增回归源码覆盖低于/等于/超过百万点上限、默认 300000 兼容、重复抽样映射、六帧展示预算容量及浅灰画布。按用户安排本轮不运行构建、CTest、Qt Creator 或 DPI/性能验收。用户侧验收需特别观察百万点上传/绘制耗时、两轮切换后预算释放，以及 100%/150% DPI 下浅底文字和点云可见性。
- 定向源码外验证命令：`cmake --build <build目录> --target reference_plane_match_tests_v2 scan_geometry_session_tests_v2 scan_geometry_view_tests_v2`；`ctest --test-dir <build目录> --output-on-failure -R "reference_plane_match_tests_v2|scan_geometry_session_tests_v2|scan_geometry_view_tests_v2"`。
