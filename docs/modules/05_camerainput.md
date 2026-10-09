# 05_camerainput：海康 Mv3dLp 点云取流模块

> 文档状态：历史与兼容索引。整理日期：2026-10-09（仅静态核对）。规范入口：[10_pointcloud_io.md](10_pointcloud_io.md)；总索引：[docs/README.md](../README.md)。正文和带日期的验证结论保留用于追溯，当前实现请查规范入口。

> 历史模块：2026-09-06 已并入 v0.4 `10_pointcloud_io`（`modules/10_pointcloud_io`，target `pcv_m10_pointcloud_io`）。相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现均随目录迁入；相机为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力，无 SDK 仍可构建纯函数转换与 Mock 源，真实 Mv3dLp 编译需 SDK。旧 target `pcv_m05_camerainput` 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_io`。以下内容为并入前的历史记录，不再作为当前规范；正文中旧 target 和独立模块目录等表述仅用于追溯。当前规范见 [10_pointcloud_io.md](10_pointcloud_io.md)；旧 [10_pointcloud_read.md](10_pointcloud_read.md) 保留历史正文。

状态（并入前）：实验中（2026-09）；根 CMake 默认构建，`scripts/build_windows.ps1` 默认关闭，仍需实机验收后才能纳入发布基线。
需求登记：启用 3DMVS 回调取流；DP2240 默认请求 `Range_Image=7`，将 SDK 原始深度回调转换并组装为完整 ABC32f 扫描。

### DP2240 Height 语义修正（2026-09-03，终版：仅诊断 + 自适应组帧）

- 实机证据：DP2240（Range_Image=7）回调经 SDK 转换后为完整 2100x3000 扫描，而 `GetParam("Height")` 返回 current=max=1060——Height 是传输窗口高度，不是完整扫描行数（完整行数 3000 来自 SDK 内部 `max_scan_rows`）。
- 终版行为：`configureExpectedScanHeight()` 仅读取并记录 `Height` 的 current/max/min/increment 用于诊断（不注入组帧器）；组帧器以 expectedRows=0 自适应启动，首个完整回调确定行数；无活动分片时整幅 chunk 直接发布并动态提升期望行数；仅活动分片期间的高度异常（超当前期望且非完整帧）才拒绝。
- 启动日志记录 `Height` current/max/min/inc、`ImageMode` 与诊断扫描高度，不硬编码 3000；GUI 看门狗区分完全没有回调和已收到回调但未形成完整扫描。

## 1. 职责

- 封装海康 3D 激光轮廓传感器 SDK（`Mv3dLpApi.h` / `Mv3dLpDefine.h` / `Mv3dLpImgProc.h`，链接 `Mv3dLp.lib`）；
- 按开发指南 V1.3.2 **回调取图流程**取流：`Initialize → 枚举 → OpenDeviceBySN/IP → （可选参数）→ （可选异常回调）→ RegisterImageDataCallBack → StartMeasure → StopMeasure → CloseDevice → Finalize`；
- **DP2240 默认 Range_Image=7**：启动和重连均必须 SetParam 成功且 GetParam 回读为 `7`；`Height` 仅作诊断读取（其值如 1060 不代表完整扫描行数），组帧器以自适应模式确定行数；C16 仅在原始 SDK 回调内调用 `MV3D_LP_MapDepthToPointCloud`，成功后深拷贝 ABC32f；
- **完整扫描门禁**：回调完整帧直接发布（无活动分片时即使高度超过初始期望也按实际高度放行并提升期望）；分片帧以首个 `nFrameNum` 锚定并按连续轮廓序号组装至自适应目标行数。该字段是设备运行序号，不要求应用启动后从 0 开始。活动分片期间的宽度变化、跳行、重复、倒序、越界、超高或内存超过 256 MiB 时丢弃当前扫描，不发布残缺线轮廓；
- **PointCloud_Image=4 仅用于显式诊断**：Profile 回调仍可在原始 SDK 回调内调用 `MV3D_LP_ImageConvert` 后交给同一组帧器。若实机确认跨批 Y 坐标不连续，保持明确诊断，不按帧号伪造毫米坐标；所有 SDK 转换均验证 ABC32f 类型、尺寸和长度，手写 scale/offset 只供 Mock/离线测试；
- **事件驱动取帧**：帧槽通过加锁 `setFramePublishedCallback`/`clearFramePublishedCallback` 通知；转换在单个 QtConcurrent watcher 中单飞执行，GUI 零阻塞；
- **异常事件与自动重连**：断线发布 `FrameKind::Exception` 事件，UI 触发自动重连（StopMeasure → CloseDevice → 重试 Open → 重新注册 → StartMeasure）；
- **软触发**：`MV3D_LP_SoftTrigger` 支持软触发模式出图；
- **触发参数兼容**：连续模式优先探测 `TriggerMode=0`；DP2240/部分固件对 `GetParam("TriggerMode")` 返回 `0x80060004 (MV3D_LP_E_PARAMETER)` 或 `MV3D_LP_E_SUPPORT` 时，按 3DMVS 回调示例保留 SDK 默认自由采集并继续启动，不把可选节点当作硬失败；若节点存在但设置 `0` 失败，仅记录兼容警告。软触发模式仍必须成功探测、设置并回读 `TriggerMode=1` 与设备支持的 `TriggerSource`，否则明确报告“不支持软触发”并拒绝启动；
- **虚拟相机识别**：3DMVS 虚拟相机（SN 前缀 Vir）按真实设备流程取流，用于无实机验证；
- 回调线程只做 SDK 原始帧的必要转换、结果深拷贝和完整扫描组帧，再发布到最新帧槽（latest-frame-wins）；不执行 QWidget 或项目点云算法；ABC32f 解析、无效点过滤、预览下采样和画布发布在后台/GUI 后续阶段完成；
- 帧转换（含空点/非有限值过滤）为纯函数，单测可覆盖三类帧类型；
- 提供 mock 帧源（含异常事件模拟），无相机环境可跑通同一转换/渲染路径。

## 2. 文件与 target

| 文件 | 说明 |
|---|---|
| `include/pcv/camera/frame_data.h` | `CameraFrame`（SDK 帧的拥有式拷贝）、`ImageMode`/`FrameType` 枚举、`DeviceInfo`、`CameraError` |
| `include/pcv/camera/frame_converter.h` + `src/frame_converter.cpp` | `convertPointCloudToPoints`（ABC32f）、`convertDepthToPoints`（C16）、`convertProfileToPoints`（ABC16）→ `QVector<Point3D>`，可单测纯函数 |
| `include/pcv/camera/frame_source.h` + `src/latest_frame_slot.cpp` | `LatestFrameSlot`（生产者/消费者最新帧槽）、`FrameSource` 抽象接口 |
| `include/pcv/camera/mock_frame_source.h` + `src/mock_frame_source.cpp` | 合成点云帧源（正弦曲面），模拟 SDK 回调契约 |
| `include/pcv/camera/mv3d_lp_camera.h` + `src/mv3d_lp_camera.cpp` | 真机封装（pimpl 隔离 SDK 类型）；回调内将 Profile/Depth 以 SDK 权威转换归一化为 ABC32f，并只发布完整扫描；后台只解析拥有式点云；仅 `PCV_CAMERA_HAVE_HIKSDK` 时编译 |
| `CMakeLists.txt` | 探测 `PCV_HIKSDK_ROOT`，无 SDK 时跳过真机源、mock 单测仍可运行 |

target（并入前）：`pcv_m05_camerainput` 曾为 STATIC（依赖 `pcv_core` + `Qt::Core`；真机源额外链接 `Mv3dLp.lib`）；2026-09-06 起该 target 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。

## 3. 稳定契约

- 帧单位：真机点云坐标仅以 SDK 转换的真实 mm 值为准；`CameraFrame::xScale/yScale/zScale` 仅供 Mock/离线转换测试使用；
- 空点：`SDKEmptyPoint` 控制的无效点以 NaN/零坐标出现，转换时默认跳过非有限值（`skipNonFinite=true`），零坐标可选跳过（`skipZero`，默认 false）；
- 丢包：`CameraFrame::valid`（`bValid`）为 false 时转换器拒绝该帧；SDK 原始 Profile/Depth 与归一化 ABC32f 都要求非零尺寸且 payload 分别严格等于 `width * height * 6/2/12`；
- 回调线程安全：`LatestFrameSlot::publish`、callback setter/clear 和 `take()` 均加锁；停止时先清 callback；SDK callback context 通过 `accepting` 与在途计数隔离关闭期回调；
- 异常事件：`FrameKind::Exception` 帧携带 `exceptionDescription`，转换器拒绝该类事件并给出明确错误；
- 重连：`reconnectOnce()` 单次重连尝试，由 UI 定时器按间隔驱动；
- SDK 生命周期：模块内引用计数管理 `Initialize/Finalize`；每次成功 lease 只对应一次 `Finalize`，`stop()` 幂等；启动或重连失败也会先停止回调、等待在途回调，再关闭设备并释放 lease；
- 共享库不弹窗、不访问 QWidget；`Mv3dLpCamera` 不依赖 Qt Widgets/OpenGL。
- 诊断：真实帧按状态变化首次和每 120 帧记录一次请求/回读 ImageMode、原始/输出类型、尺寸、长度、`bValid`、scale/offset、完整扫描行数/分片数/丢弃扫描数；SDK 状态码同时记录名称，例如 `MV3D_LP_E_ALGORITHM`。

## 4. CMake

- 变量 `PCV_HIKSDK_ROOT`：默认 `C:/Program Files (x86)/3DMVS/Development`；`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，仍可显式关闭；
- 找到 `Includes/Mv3dLpApi.h` 且 `Libraries/win64/Mv3dLp.lib` 时定义 `PCV_CAMERA_HAVE_HIKSDK=1` 并编译真机源；
- 运行时 DLL：`Mv3dLp.dll` 位于 `C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64`，由应用 POST_BUILD 拷贝（见 `apps/pointcloudview/CMakeLists.txt`）。

## 5. 测试

- `tests/unit/camera/frame_converter_tests.cpp`：ABC32f→Point3D、C16 深度→Point3D（scale/offset）、ABC16 轮廓→Point3D、非有限值过滤、skipZero、bValid=false 拒绝、跨类型拒绝、长度校验；
- `tests/unit/camera/frame_source_tests.cpp`：mock 启停、最新帧槽 latest-frame-wins、take 清槽、加锁 callback setter/clear 通知、异常事件（FrameKind/描述）、convertFrame 深度帧分派，以及完整帧/连续分片/末批/跳行/重复/宽度变化/重置/内存上限/整幅高于初始期望放行/自适应首帧的组帧门禁；
- `tests/unit/pointcloudview/live_pipeline_tests.cpp`：JSON pipeline 全链（mock→voxel→publish）、未知节点拒绝、按需节点过滤。

验证：实验开关 ON 的 Debug 构建、`Mv3dLp.dll` 部署和全量 CTest `17/17` 通过；实验开关 OFF 的核心 CTest `13/13` 通过。
已验证（实机 DP2240，2026-09-03）：Range_Image=7 完整扫描组帧（3000/3000 行，2100x3000=630 万点）、Height 诊断适配、连续取流出帧。
未验证项：坐标系方向（`SDKCoordinateType`）、软触发、重连、关闭时序、性能目标与画布完整点数渲染（当前预览 pipeline 默认 maxPoints=300000 截断到 30 万点显示）；`live_pipeline` 尚未接入生产相机链。

## 6. 变更记录

### 2026-09-02（DP2240 完整扫描组装）

- 根因：`ImageMode=4` 的实际 Profile 回调为 `3200x24~30` 分片，`nFrameNum` 按分片高度递增，最后 `2976 + 24 = 3000`；逐分片发布到 `LatestFrameSlot` 只会渲染最后一批线轮廓。实机日志进一步确认首批序号可非 0，因此首批只作为连续序列锚点；
- 实现：真实设备默认恢复 `Range_Image=7`，启动和重连读取 `Height`；回调中的原始 C16 先由 SDK 转为 ABC32f，再由内部组帧器按 `nFrameNum/nHeight` 合成完整扫描，只有完整扫描进入 `LatestFrameSlot`、后台转换、画布和冻结；
- 门禁：分片缺失、重复、倒序、宽度变化、超高或超过 256 MiB 时清空当前扫描并发布明确错误；断线和停止同样清空未完成扫描；
- GUI：默认选择 Range；完整扫描显示行数、分片数和丢弃扫描数；冻结仅接受完整扫描；
- 验证：组帧单测覆盖完整帧、连续分片、末批、跳行、重复、宽度变化、重置和内存上限；DP2240/虚拟相机完整扫描、坐标方向、软触发和重连仍需现场验收。

### 2026-09（实时预览 MVP）

- `LatestFrameSlot` 增加发布/丢帧统计和复位接口，保持 latest-frame-wins 与线程安全语义；
- `CameraDialog` 增加“冻结当前帧”，冻结结果显式交给主窗口普通点云处理缓存，取流帧仍只用于预览；
- 实时 pipeline 增加 `filter.voxel.maxPoints`（默认 300000）显示预算，超限时确定性截断；
- 真实 C16 深度帧继续只允许 SDK `MV3D_LP_MapDepthToPointCloud` 转换，SDK 失败 fail-closed；
- 未验证：真实 DP2240 坐标方向、软触发、重连和 15 FPS 性能。

### 2026-09-02（DP2240 ABC32f 回调修复）

- 诊断：`DB1723975 / MV-DP2240-01P` 的 Range C16 回调有效（`2100x3000`、`12600000` 字节），但深拷贝后调用 `MV3D_LP_MapDepthToPointCloud` 返回 `0x8006000c (MV3D_LP_E_ALGORITHM)`；SDK 日志中的 `TO_PROFILE_S32 config[0] [0,0,0,0,0,0]` 表明转换上下文不完整；
- 行为：真实设备默认 `ImageMode_PointCloud_Image=4`，严格回读；仅 ABC32f 进入实时预览和冻结后的既有处理。Range 保留为显式 SDK 转换诊断，失败不发布近似坐标；
- 测试：模式 4 的 ABC32f 接收、C16/Profile 明确拒绝、ABC32f 的 `bValid=false` 与长度/尺寸门禁；
- 未验证：实机需确认模式回读为 `4`、回调为 `0x026000c0`、长度为 `width * height * 12`，以及预览、冻结、软触发、断线重连和关闭时序。

### 2026-09-02（DP2240 Profile 原始回调转换）

- 现场证据：模式请求和回读均为 `4`，但 `DB1723975 / MV-DP2240-01P` 回传有效 `ImageType_Profile=0x023000b9`；例如 `3200x26`、`499200` 字节，严格等于 `width * height * 6`，且 scale/offset 为零；
- 修复：`MV3D_LP_ImageConvert(rawProfile, PointCloud)` 移入 SDK 回调，原始结构有效期间完成后立即深拷贝输出 ABC32f；Range 深度诊断同样在回调内调用 `MV3D_LP_MapDepthToPointCloud`。后台不再重建 SDK 图像结构或调用 SDK 转换；
- 门禁：转换状态必须为 `MV3D_LP_OK`，输出类型为 `0x026000c0`，输出长度为 `width * height * 12`。失败发布无效数据帧供 GUI 显示原因，不触发断线重连，也不发布近似坐标；
- 验证：单测覆盖原始帧动作分派和既有 ABC32f 门禁；真实设备仍需验证 `Profile -> PointCloud` SDK 调用的成功状态、坐标范围、持续帧率、冻结、停止和重连。

### 2026-09（本模块创建）

- 需求：相机点云图取流直连 OpenGL 画布，减少 PLY 落盘/磁盘缓存中间步骤，处理链以 JSON pipeline 编排；
- 文件：本模块全部文件；`apps/pointcloudview`（cameradialog、live_pipeline、mainwindow 相机菜单）；`modules/20_pointcloudrender`（`setCloudStreaming`）；
- 行为：回调帧 → 最新帧槽 → GUI 拉帧 → 后台转换 → `LiveFrame` → `PipelineRunner` → `setCloudStreaming` 渲染；
- 验证：实验开关 ON 的 Debug 构建和 CTest `17/17` 通过；生产链使用 Mock 的回归已覆盖，真实 DP2240 仍未验证；
- 风险：真实相机验收、坐标系映射、回调生命周期关闭时序待实机确认。

### 2026-09（DP2240 无效帧修复）

- 现象：DP2240（MV-DP2240-01P）接入后对话框显示"帧：无效"；
- 诊断：SDK 日志（Mv3dLpLog）显示设备为 HFR 线激光轮廓传感器，`LSLRIOutImageType=2` 且 HFR 管线缓冲为深度图+亮度图——点云模式下实际回传深度图帧（ImageType_Depth C16），而转换器此前只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 路径；`FrameSource::convertFrame` 真机走 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应不强制；对话框加取流模式选项与帧类型诊断日志（startup.log）；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 通过，应用构建通过；
- 风险：实机 DP2240 复测待执行；深度/轮廓手写公式为近似（SDK 转换优先）。

### 2026-09（DP2240 实机 scan-height 无效帧修复）

- 现象：DP2240（ImageMode=7 Range_Image）StartMeasure 成功后回调被装配器丢弃，UI 显示 帧：无效；
- 日志：configured scan Height=1060（GetParam(Height) nMax=1060），回调经 MapDepthToPointCloud 输出完整 2100x3000 扫描（3000 行），装配器报 scan chunk height 3000 exceeds configured Height 1060；
- 根因：DP 系列 HFR 的 Height 参数（1060）是传输窗口高，非完整扫描行数（3000，来自 SDK 内部 max_scan_rows）；深度图模式回调本就是整幅扫描，无需行拼接；
- 修复：
  - scan_frame_assembler.h：无活动分片时整幅 chunk（高度大于 configured）按实际高度直接发布并动态提升 expectedRows；仅活动分片期间的高度异常才拒绝；
  - mv3d_lp_camera.cpp：configureExpectedScanHeight 改为仅诊断（Height 不注入装配器），装配器以 expectedRows=0 自适应，首帧确定行数；
- 验证：frame_source_tests 更新整幅放行/活动分片拒绝/自适应用例，全量 17/17 通过；应用构建通过；
- 实机确认（startup.log 2026-09-03 11:40）：complete scan rows=3000/3000 fragments=1 discarded=0 size=2100x3000，不再出现 valid=false 空帧；完整扫描 2100x3000=630 万点进入后台转换；
- 已知限制（预览显示预算）：实时 pipeline 的 filter.voxel 默认 maxPoints=300000，超限确定性截断到 30 万点显示；如需画布复现完整 630 万点，需提高/移除该上限并评估 OpenGL VBO 与渲染性能。

### 2026-09（对齐 3DMVS 取流方式）

- 依据：3DMVS V3.1.4 日志（Mv3dLpLog）确认真实相机取流 ImageMode=7（Range_Image）、虚拟相机（Vir 前缀）走同一流程；用户手册确认重发包/缓存节点/触发模式；
- 行为：默认 ImageMode=7；事件驱动取帧（callback setter/clear）；转换移出 GUI 线程（QtConcurrent）；异常事件 + 自动重连（reconnectOnce + 2s 定时器）；软触发（SoftTrigger）；虚拟相机识别与标记；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 通过；应用构建通过；
- 未验证：真实相机软触发/重连/深度图取流实机复测待执行；重发包为 SDK 内部默认（Mv3dLpApi 未公开 SetResend 接口，已在文档记录）。

### 2026-09（DP2240 安全回归）

- 真机 C16/ABC16 SDK 转换失败改为 fail-closed，禁止静默发布近似坐标；Mock/离线路径仍保留纯函数换算用于自动化测试。
- 深度/轮廓帧新增非零尺寸及 `payload == width * height` 校验；停止流程清除帧通知后再等待回调和释放 SDK。
- 清洁 `NMake Makefiles` Debug/Release 构建验证：SDK ON 全量 CTest `17/17`，SDK OFF 基线全量 CTest `13/13`；实机 DP2240 GUI/坐标/断线验收待现场执行。

### 2026-09（Designer 采集工作台升级）

- `CameraDialog` 改为 Designer 驱动的设备详情页：设备选择会显示型号、序列号、IP 与真实/虚拟设备状态，并保留 Mock 合成点云源用于无相机演示；
- 主窗口增加 Designer 工具栏和四步向导导航（采集、清理、平面、输出），工具栏仅转发到既有取流、扫描 JSON、文件输入与画布重置入口；
- 激光器、ROI、采集帧率和灵敏度以置灰控件明确标记“SDK 参数接口待接入”，不保存本地假状态、不下发设备；
- 未改变 `DeviceInfo`、`FrameSource`、转换器、实时 Pipeline 或共享模块 API；真实相机 GUI、软触发、重连和坐标仍须现场验收。

### 2026-09-06（并入 10_pointcloud_read）

- 2026-09-06 模块重命名：`05_camerainput` 并入 `10_pointcloud_read`（`modules/10_pointcloud_read`，target `pcv_m10_pointcloud_read`），本模块不再作为独立模块。
- 相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现迁入该模块；相机为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力，无 SDK 仍可构建纯函数转换与 Mock 源，真实 Mv3dLp 编译需 SDK。
- 旧 target `pcv_m05_camerainput` 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。本文档转为历史记录，不再作为当前规范；既有验证结论与历史 CTest 计数仅用于追溯。
