# 扫描与几何选择契约适配 v1

日期：2026-11-05。owner：主线 / Codex。业务范围为 03 的扫描数据处理与候选几何交接；网页只作参考。软件版本、旧 sidecar 和模板 schema 不升级。

## 当前交付与使用

`scan_geometry_demo_v1` 的“导入数据”新增可选“输出候选几何契约”配置，默认关闭以兼容既有离线任务。启用时填写绝对 `runtime_data_root`、`job_id/run_id/step_id/workpiece_id/controller_id`、可选相对 `scan_path_output_ref`、掩码像素尺寸（默认 0.25 mm）。业务身份来自上游，不自动伪造；当前 WObj 槽仅支持 1。配置属于本次 C++ 应用会话，不向网页或设备写配置。

继续使用原有 PLY / 扫描 JSON / SDK深度帧、手眼 XML、Start/End 配置。`scan_setup.json` 是 01 的扫描准备输入，不能代替 02 实际执行成果，本次不增加独立解析器或扫描执行功能。几何确认调用模块 60 建 WObj，随后用一个 `ArtifactTransaction` 提交内部成果与正式交接文件。

```text
<runtime_data_root>/
  system/temp/<job_id>/<run_id>/selected_plane_<time>_<uuid>/  # 原有拼接中间结果
  jobs/<job_id>/runs/<run_id>/03-point-cloud/
    point_cloud_result.json          # 最后提交，消费者仅以它作为发布入口
    stitched_base.ply                # 所选平面完整拼接点，未修剪；不代表整场景点云
    template_candidate.ply           # 完整拼接按已确认修剪派生，点序保留
    plane_mask.png                   # Grayscale8，0/128/255
    raw/scan_001.ply ...              # 实际使用的原始相机坐标点云快照
    source/handeye.xml               # 实际标定原始XML快照，不包装成JSON
    source/processing_evidence_v1.json
    source/depth_001/...              # SDK深度输入存在时，保留原始帧包
    selected_plane_geometry_v1.json  # 内部sidecar，旧字段/robot_base保持
    selected_plane_robot_base.ply/.pcvmap
    selected_plane_trimmed.ply/.pcvmap
    selected_plane_manifest.json
    selected_plane_report.json
    diagnostics/<uuid>/failure_v1.json # 仅导出失败时记录，不是正式成功结果
```

同一 run 正式资产不可覆盖，改参/修剪后重新发布须新 `run_id` 并重新处理。普通失败和取消撤销本事务暂存；已独立提交的模块 30 中间目录可能保留。崩溃/断电不承诺整目录文件系统级原子性；正式 JSON 最后发布，消费方不能把临时可见资产当成功结果。取消恰在提交后到达时，沿用已提交文件留存、GUI 不发布过期结果的生命周期规则。

## 上下游与字段映射

| owner / 输入输出 | 本阶段行为 |
|---|---|
| 01 业务身份、运行根目录 | C++ 配置显式接收，ID 限制为 1～96 个 ASCII 字母、数字、`_`、`-`，不生成 Job/Run |
| 02 `scan_path_output.json` | 可选读取，校验 `sr2026.scan-path-output` v1、Job/Run/Step、global_scan、completed、base、数量、端点、时间和来源计划身份；不自行创建 |
| 10 原始 PLY / SDK深度帧 | 重新读取并核对确认时来源；归档原始点云/深度帧包；深度 XYZ 仍通过官方 SDK，缺依赖时拒绝导出 |
| 50 XML / Start-End | 复用模块50转换校验；XML与选面时标定矩阵一致；同一结果只接受相同XML，不静默混合标定 |
| 30 完整所选平面结果 | `outputs.base_cloud_ref`，输出仍是同一 robot base；来源映射/点序不变 |
| 修剪后候选平面 | `outputs.template_geometry.point_cloud_ref`，三维坐标为 base/mm |
| 60 `PlaneMaskMeshV1` | 从完整最终保留点复用 `buildPlaneMaskV1` 重建占用网格，光栅化为 `plane_mask`，防止复用显示子集或修剪前网格 |
| 60 WObj1 | `outputs.wobj_candidate`，检查正交刚体矩阵、轴、逆矩阵和 XYZABC 一致性；method=point_cloud_fit |
| 后续01审核/探针/发布 | 本期不创建 probe_targets/probe_result、workpiece_template/current，不写机器人 |
| 04/规划编译消费 | 按任务需求读取候选资产；不能凭 completed 当作模板已发布、精度合格或执行许可 |

正式 JSON 所有文件引用相对 runtime root；`robot_base` 是内部既有语义，适配层核验后对同一实际参考系输出契约名称 `base`，不转换为 `workpiece`。二维图从 WObj 平面 XY 投影，不能用 base XY 代替。正式模板发布需要后续按最终 WObj 逆变换重新导出，当前候选 PLY 不能直接改标签发布。

**固定掩码约定**：WObj XY 原点为图像物理矩形中心，图像右方为 +X、上方为 +Y，像素按中心采样，实体宽高=`pixel_size_px * mm_per_pixel`。保持矩形关于原点对称，向上取整以覆盖最终网格。模板为128，背景为0，已确认干涉网格按各帧累计拼接修正后投影为255并覆盖模板；不做可达性或碰撞结论。修剪后工件网格重建，干涉标注不参与工件拼接。最大16M像素、单轴16384；超限要求增大像素尺寸，不静默降低精度。PNG编码后以 `stageBytes` 提交，因为既有 `stageGrayscalePng` 会二值化，旧行为保持。

`inputs.calibration_ref` 指向本run保留的真实原始XML；raw清单只包含实际使用帧，path_index保留原扫描顺序，跳过不会重新编号。深度帧导出的raw PLY是官方SDK物化的相机坐标点集，原始深度与栅格证据另留存，不把压缩后的PLY下标当原始栅格下标。`source/processing_evidence_v1.json` 记录采用的Start/End、layout、确认源digest、选面参数、result revision与固定掩码约定；已有manifest/PCVMAP保留详细处理与逐点来源。

## 状态、校验和页面

- 无实际扫描成果时省略 `inputs.scan_path_output_ref`，输出 `partial` 和 `scan_path_output_not_available`；不能填写不存在的引用或用扫描输入冒充。显式给出但无效的扫描成果会拒绝本次导出。
- 有实际扫描成果且全部输入使用、几何和资产校验通过时为 `completed`。它只表示本次约定的候选几何处理完成。
- 不生成探针目标并保留 `probe_targets_not_available` 警告；`stitch_quality_warn_only` 显式保留，不能解释为空误差或严格通过。
- 选面/拼接未完成时原有会话门禁禁止保存；导出失败不发布本轮point_cloud_result，单独保留failure证据。取消不伪装成failed成功记录。
- 正式JSON采用 `sr2026.point-cloud-result`、schema_version=1、首次建模模板引用均null。`relocate_result.json` 与 registration 不属于本次功能，模板相邻帧参考面匹配不能冒充工件重定位。

前端参考映射：扫描/处理阶段和百分比来自真实会话（无比例为-1），候选WObj显示六元位姿，掩码预览读取正式PNG，错误/警告显示validation；“探针测量”“发布模板”维持未接入语义。`index.html` 与其脚本未修改；页面模拟状态不写生产JSON。

可执行结构约束在 `docs/contracts/point_cloud_result_v1.schema.json`，它是当前首次建模候选子集，不宣称覆盖外部主契约所有业务。跨字段比例、唯一path_index、文件存在/边界、PLY内容与PNG实际像素不能仅靠JSON Schema判断：应用出口和独立工具继续检查。

```powershell
python tools/validate_point_cloud_result_v1.py examples/configs/point_cloud_result_partial_v1.example.json
python tools/validate_point_cloud_result_v1.py <result.json> --runtime-root <runtime_data_root>
```

工具只读，依赖 `jsonschema` 和 `Pillow`；无runtime root只验证格式，示例不代表资产存在或真实扫描。本期新增合成契约测试、真实会话保存/失效测试与配置对话框定向测试。构建/CTest的实际结果见本文后续验证记录，不沿用历史通过结论。

## 风险与回滚

真实相机/SDK深度完整链、生产大点云、持久化吞吐、Unicode现场样例、探针与物理精度仍需现场验收。正式输出依赖原始输入仍可读取；确认后输入变化会阻断导出。为了保留证据和校验，导出阶段会重新物化完整源帧，峰值内存和耗时需要实测。标定源、任务上下文与控制器身份的真实性仍由上游负责。既有WObj算法在平面法向平行base X时投影建轴退化并拒绝保存，本次不改算法；其他可建轴的垂直平面已作合成导出验证。

本轮开始已有19个已修改和5个未跟踪文件；保留其内容。本任务代码只增加应用私有适配、几何保存与配置接入、CMake注册和相关测试，不改10/30/50/60算法或网页。回滚可先关闭契约配置恢复原离线模式；代码回滚须逐块撤销本任务增量，不能整体恢复已有修改的tests/CMakeLists或共享文档。未提交、推送、上传或删除原文件。

## 2026-11-05 实际验证

Qt6.8.3/MSVC19.51/NMake，Tests/Tools ON、Camera/STEP/CLI OFF。Debug、Release均从全新源码外目录完成全目标构建；最终增量也成功。目录分别为 `D:/pcv-build/scan-contract-v1-debug-20261105`、`D:/pcv-build/scan-contract-v1-release-20261105`，最终构建日志为同级 `*-build-final-v3.log`。使用命令：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -BuildTools -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir D:\pcv-build\scan-contract-v1-debug-20261105 -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
# Release以Release配置及独立release目录运行同一命令
ctest --test-dir D:\pcv-build\scan-contract-v1-debug-20261105 --output-on-failure
ctest --test-dir D:\pcv-build\scan-contract-v1-release-20261105 --output-on-failure
```

| 验证 | 实际结果 |
|---|---|
| 新契约定向测试 | Debug/Release均8/8（包含init/cleanup）；覆盖完整/partial、来源与端点错误、取消/不覆盖、灰度/比例及有效垂直平面 |
| 几何会话测试（含本次保存/失效增量） | 单独运行Debug/Release均21/21；Debug全量存在间歇失败，根因未收口 |
| 新配置对话框定向测试 | 原生Debug/Release均3/3；Debug截图`contract-config-v1.png`已打开检查，配置区可滚动，未验收完整DPI矩阵 |
| 最终全量CTest | Debug34/37，Release35/37；见各构建目录`ctest-debug-v3.log` / `ctest-release-v3.log` |
| 独立JSON校验 | 格式样例通过；两份真实合成导出目录经`--runtime-root`验证PLY有限值/点数、PNG灰度/尺寸、引用存在通过 |
| Release原生启动/关闭 | `PCV_SCAN_GEOMETRY_SELFTEST_CLOSE=1`，退出码0 |

剩余失败：`plane_mask_tests_v1`两配置均退出码8，对应既有各向异性掩码断言；`scan_geometry_view_tests_v2`有旧断言`colorLegend`按QPushButton查找失败及`markers.at(0).w()`实际1/期望0。Debug `scan_geometry_session_tests_v2`全量偶发无日志失败，独立带`-o <file>,txt`及单项CTest通过；不能据此宣称全量已通过。失败涉及当前页面/掩码验收，后续应另行定位，不在本次契约任务里修改已有算法或断言掩盖问题。

中间曾因新增垂直平面测试选择了既有WObj退化法向而失败，已修正夹具为支持方向并保留退化限制说明。上述表仅记录最终代码对应结果；历史测试和中间失败不充当最终通过。Release windeployqt仍提示本机Visual Studio Release redistributable缺失，干净机器部署未验证。

### 当前工作区集成复验

共享工作区随后加入灰色点云、marker及相关视图测试修改，本任务保留这些增量。对当前源码再次执行Debug/Release全目标构建均exit0，日志为 `D:/pcv-build/scan-contract-v1-{debug,release}-20261105-build-integrated.log`。构建前记录的50份应用、模块20及测试源码哈希在本次测试后全部一致，复验期间未发现这些源码再次变化。

最终全量CTest：Debug **34/37**（52.37 s），Release **35/37**（10.27 s），日志分别为各构建目录的 `ctest-debug-integrated.log`、`ctest-release-integrated.log`。新增契约目标两配置均通过；Debug原生视图详细运行中 `contractInputConfiguration` 通过，完整视图为8通过/1失败，当前仅剩 `colorLegend` 的QPushButton查找断言，先前marker断言在当前源码已通过。`plane_mask_tests_v1`两配置仍exit8，对应各向异性网格断言。Debug会话全量仍失败，随后独立运行21/21通过（含本次契约保存与失效测试）；根因未确定，不宣称全量通过。

独立工具再次验证格式样例及两个保留的Unicode路径合成输出，实际PLY/PNG/引用检查均通过。`git diff --check`通过，仅有LF/CRLF提示。当前工作区已有及并行修改均保留，无删除、暂存、提交、推送或上传；详细诊断为Debug目录 `session-integrated-details.txt`、`view-integrated-native-details.txt`。手动用offscreen启动原生视图的诊断尝试未产生有效结果，已停止；上述视图结论来自与CTest一致的windows平台运行。
