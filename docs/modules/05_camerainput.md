# 05_camerainput：海康 Mv3dLp 点云取流模块

状态：实验中（2026-09）；默认构建，仍需实机验收后才能纳入 v0.3 发布基线。
需求登记：启用 3DMVS 回调取流；默认请求 `Range_Image=7`，按实际帧类型转换为点云。

## 1. 职责

- 封装海康 3D 激光轮廓传感器 SDK（`Mv3dLpApi.h` / `Mv3dLpDefine.h` / `Mv3dLpImgProc.h`，链接 `Mv3dLp.lib`）；
- 按开发指南 V1.3.2 **回调取图流程**取流：`Initialize → 枚举 → OpenDeviceBySN/IP → （可选参数）→ （可选异常回调）→ RegisterImageDataCallBack → StartMeasure → StopMeasure → CloseDevice → Finalize`；
- **按帧类型自适应**：点云（ABC32f）直读；深度图（C16）经 `MV3D_LP_MapDepthToPointCloud` 转换；轮廓（ABC16）经 `MV3D_LP_ImageConvert` 转换。手写 scale/offset 仅供 Mock/离线测试，不得作为真机静默 fallback；
- **默认 ImageMode=7（Range_Image 深度图）**，与 3DMVS 客户端取 DP 系列的方式一致，可请求点云/自适应模式；
- **事件驱动取帧**：帧槽通过加锁 `setFramePublishedCallback`/`clearFramePublishedCallback` 通知；转换在单个 QtConcurrent watcher 中单飞执行，GUI 零阻塞；
- **异常事件与自动重连**：断线发布 `FrameKind::Exception` 事件，UI 触发自动重连（StopMeasure → CloseDevice → 重试 Open → 重新注册 → StartMeasure）；
- **软触发**：`MV3D_LP_SoftTrigger` 支持软触发模式出图；
- **虚拟相机识别**：3DMVS 虚拟相机（SN 前缀 Vir）按真实设备流程取流，用于无实机验证；
- 回调线程只做数据拷贝 + 原子换帧（最新帧槽，latest-frame-wins 丢帧策略）；
- 帧转换（含空点/非有限值过滤）为纯函数，单测可覆盖三类帧类型；
- 提供 mock 帧源（含异常事件模拟），无相机环境可跑通同一转换/渲染路径。

## 2. 文件与 target

| 文件 | 说明 |
|---|---|
| `include/pcv/camera/frame_data.h` | `CameraFrame`（SDK 帧的拥有式拷贝）、`ImageMode`/`FrameType` 枚举、`DeviceInfo`、`CameraError` |
| `include/pcv/camera/frame_converter.h` + `src/frame_converter.cpp` | `convertPointCloudToPoints`（ABC32f）、`convertDepthToPoints`（C16）、`convertProfileToPoints`（ABC16）→ `QVector<Point3D>`，可单测纯函数 |
| `include/pcv/camera/frame_source.h` + `src/latest_frame_slot.cpp` | `LatestFrameSlot`（生产者/消费者最新帧槽）、`FrameSource` 抽象接口 |
| `include/pcv/camera/mock_frame_source.h` + `src/mock_frame_source.cpp` | 合成点云帧源（正弦曲面），模拟 SDK 回调契约 |
| `include/pcv/camera/mv3d_lp_camera.h` + `src/mv3d_lp_camera.cpp` | 真机封装（pimpl 隔离 SDK 类型）；`convertFrame` 用 SDK 权威转换（MapDepthToPointCloud/ImageConvert），`requestImageMode`/自适应；仅 `PCV_CAMERA_HAVE_HIKSDK` 时编译 |
| `CMakeLists.txt` | 探测 `PCV_HIKSDK_ROOT`，无 SDK 时跳过真机源、mock 单测仍可运行 |

target：`pcv_m05_camerainput`（STATIC，依赖 `pcv_core` + `Qt::Core`；真机源额外链接 `Mv3dLp.lib`）。

## 3. 稳定契约

- 帧单位：真机点云坐标仅以 SDK 转换的真实 mm 值为准；`CameraFrame::xScale/yScale/zScale` 仅供 Mock/离线转换测试使用；
- 空点：`SDKEmptyPoint` 控制的无效点以 NaN/零坐标出现，转换时默认跳过非有限值（`skipNonFinite=true`），零坐标可选跳过（`skipZero`，默认 false）；
- 丢包：`CameraFrame::valid`（`bValid`）为 false 时转换器拒绝该帧；深度/轮廓帧还要求非零尺寸且 payload 样本数严格等于 `width * height`；
- 回调线程安全：`LatestFrameSlot::publish`、callback setter/clear 和 `take()` 均加锁；停止时先清 callback；SDK callback context 通过 `accepting` 与在途计数隔离关闭期回调；
- 异常事件：`FrameKind::Exception` 帧携带 `exceptionDescription`，转换器拒绝该类事件并给出明确错误；
- 重连：`reconnectOnce()` 单次重连尝试，由 UI 定时器按间隔驱动；
- SDK 生命周期：模块内引用计数管理 `Initialize/Finalize`；每次成功 lease 只对应一次 `Finalize`，`stop()` 幂等；启动或重连失败也会先停止回调、等待在途回调，再关闭设备并释放 lease；
- 共享库不弹窗、不访问 QWidget；`Mv3dLpCamera` 不依赖 Qt Widgets/OpenGL。

## 4. CMake

- 变量 `PCV_HIKSDK_ROOT`：默认 `C:/Program Files (x86)/3DMVS/Development`；`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，仍可显式关闭；
- 找到 `Includes/Mv3dLpApi.h` 且 `Libraries/win64/Mv3dLp.lib` 时定义 `PCV_CAMERA_HAVE_HIKSDK=1` 并编译真机源；
- 运行时 DLL：`Mv3dLp.dll` 位于 `C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64`，由应用 POST_BUILD 拷贝（见 `apps/pointcloudview/CMakeLists.txt`）。

## 5. 测试

- `tests/unit/camera/frame_converter_tests.cpp`：ABC32f→Point3D、C16 深度→Point3D（scale/offset）、ABC16 轮廓→Point3D、非有限值过滤、skipZero、bValid=false 拒绝、跨类型拒绝、长度校验；
- `tests/unit/camera/frame_source_tests.cpp`：mock 启停、最新帧槽 latest-frame-wins、take 清槽、加锁 callback setter/clear 通知、异常事件（FrameKind/描述）、convertFrame 深度帧分派；
- `tests/unit/pointcloudview/live_pipeline_tests.cpp`：JSON pipeline 全链（mock→voxel→publish）、未知节点拒绝、按需节点过滤。

验证：实验开关 ON 的 Debug 构建、`Mv3dLp.dll` 部署和全量 CTest `17/17` 通过；实验开关 OFF 的核心 CTest `13/13` 通过。
未验证项：真实相机连接、坐标系方向（`SDKCoordinateType`）、软触发、重连、关闭时序和性能；`live_pipeline` 尚未接入生产相机链。

## 6. 变更记录

### 2026-09（实时预览 MVP）

- `LatestFrameSlot` 增加发布/丢帧统计和复位接口，保持 latest-frame-wins 与线程安全语义；
- `CameraDialog` 增加“冻结当前帧”，冻结结果显式交给主窗口普通点云处理缓存，取流帧仍只用于预览；
- 实时 pipeline 增加 `filter.voxel.maxPoints`（默认 300000）显示预算，超限时确定性截断；
- 真实 C16 深度帧继续只允许 SDK `MV3D_LP_MapDepthToPointCloud` 转换，SDK 失败 fail-closed；
- 未验证：真实 DP2240 坐标方向、软触发、重连和 15 FPS 性能。

### 2026-09（本模块创建）

- 需求：相机点云图取流直连 OpenGL 画布，减少 PLY 落盘/磁盘缓存中间步骤，处理链以 JSON pipeline 编排；
- 文件：本模块全部文件；`apps/pointcloudview`（cameradialog、live_pipeline、mainwindow 相机菜单）；`modules/20_pointcloudrender`（`setCloudStreaming`）；
- 行为：回调帧 → 最新帧槽 → GUI 拉帧 → 后台转换 → `LiveFrame` → `PipelineRunner` → `setCloudStreaming` 渲染；
- 验证：实验开关 ON 的 Debug 构建和 CTest `17/17` PASS；生产链使用 Mock 的回归已覆盖，真实 DP2240 仍未验证；
- 风险：真实相机验收、坐标系映射、回调生命周期关闭时序待实机确认。

### 2026-09（DP2240 无效帧修复）

- 现象：DP2240（MV-DP2240-01P）接入后对话框显示"帧：无效"；
- 诊断：SDK 日志（Mv3dLpLog）显示设备为 HFR 线激光轮廓传感器，`LSLRIOutImageType=2` 且 HFR 管线缓冲为深度图+亮度图——点云模式下实际回传深度图帧（ImageType_Depth C16），而转换器此前只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 路径；`FrameSource::convertFrame` 真机走 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应不强制；对话框加取流模式选项与帧类型诊断日志（startup.log）；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 PASS，应用构建通过；
- 风险：实机 DP2240 复测待执行；深度/轮廓手写公式为近似（SDK 转换优先）。

### 2026-09（对齐 3DMVS 取流方式）

- 依据：3DMVS V3.1.4 日志（Mv3dLpLog）确认真实相机取流 ImageMode=7（Range_Image）、虚拟相机（Vir 前缀）走同一流程；用户手册确认重发包/缓存节点/触发模式；
- 行为：默认 ImageMode=7；事件驱动取帧（callback setter/clear）；转换移出 GUI 线程（QtConcurrent）；异常事件 + 自动重连（reconnectOnce + 2s 定时器）；软触发（SoftTrigger）；虚拟相机识别与标记；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 PASS；应用构建通过；
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
