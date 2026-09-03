# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
最后核对日期：2026-09-01
状态：核心模块稳定，相机扩展实验中，发布验收待完成

## 1. 目标和事实来源

v0.3 是 PointCloudSuite 当前唯一统筹基线。项目按十个固定编号模块治理；源码、CMake 和测试优先于历史文档。

当前未实现模块为 `90_interferenceplane` 和 `100_qualityreport`。本版本不为这两个模块建立功能 target，也不报告虚假成功。

## 2. 模块基线

| 模块 | 职责 | 规范 target | 状态 |
|---|---|---|---|
| `10_pointcloudread` | PLY 读写、缓存、校验 | `pcv_m10_pointcloudread` | 已实现 |
| `20_pointcloudrender` | Qt/OpenGL 画布、VBO、点选 | `pcv_m20_pointcloudrender` | 已实现，桌面验收待完成 |
| `30_pointcloudstitch` | 拼接流程编排、结果管理 | `pcv_m30_pointcloudstitch` | 已实现 |
| `40_pointcloudregistration` | ICP、结构校验、seam fusion | `pcv_m40_pointcloudregistration` | 已实现 |
| `50_coordinateconversion` | 手眼标定、位姿插值、坐标转换 | `pcv_m50_coordinateconversion` | 已实现 |
| `60_planefitting` | 三点/n 点平面算法和诊断 | `pcv_m60_planefitting` | 已实现 |
| `70_roi_template` | ROI、模板、临时工件接口 | `pcv_m70_roi_template` | 已实现，部分 UI 编排保留 |
| `80_planeoutput` | PNG、PLY、JSON 成套输出 | `pcv_m80_planeoutput` | 已实现 |
| `05_camerainput` | 海康相机取流（实验扩展） | `pcv_m05_camerainput` | 实验中，不纳入 v0.3 发布基线 |
| `90_interferenceplane` | 干涉平面检查 | 无 | 未实现 |
| `100_qualityreport` | 质量报告 | 无 | 未实现 |

迁移期兼容 target：`pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`。兼容层在所有调用方迁移并完成独立审计前保留。

## 3. 数据流程

单帧流程：输入校验 → `10` 读取/缓存 → `20` 渲染和点选 → `50` 坐标转换 → `60` 平面拟合 → `70` ROI/模板 → `80` 成套输出。

多帧流程：`30` 组织输入和输出，调用 `50` 转换、`40` 配准与 seam fusion，成功后提交正式 PLY。取消或失败时不得发布部分结果或复用旧正式输出。

## 4. 稳定契约

- 长度单位为 mm，角度单位为 deg。
- 位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 平面输出为 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- PNG、PLY、JSON 先写入 staging 目录，全部成功后成套提交；任一失败必须回滚。
- 共享库不弹窗、不访问 QWidget；应用负责提示和线程生命周期。
- `PointCloudCanvas` 实现位于 `modules/20_pointcloudrender/src/pointcloud_canvas.cpp`。
- 诊断工具通过 `pcv::interface::stitchRawLineProfiles` 使用统一拼接接口。

详细字段和错误码以模块公共头、模块文档和测试源码为准。

## 5. 开发和验收

每次需求登记主模块编号，修改公共头、CMake 或跨模块接口时列出影响模块和回归测试。模块文档追加日期、需求、文件、行为、验证和风险。

验证顺序：模块测试 → 直接依赖构建 → 应用构建 → Debug/Release 全量 CTest → GUI 或真实夹具验收 → 文档和 Git 检查。

当前自动化测试用于验证共享算法和接口。Windows 桌面点选、矩形选区、旋转/平移/缩放、关闭生命周期以及真实生产拼接夹具仍需单独验收。

## 6. 兼容层清理条件

只有在所有公共 include 和生产调用方切换到规范模块路径、诊断工具仅使用模块 API、应用不再包含重复算法、Debug/Release 构建和全量 CTest 通过，并完成 GUI/夹具验收后，才能单独删除兼容 target 和 forwarding header。

## 7. 发布状态

当前状态为“迁移完成，发布验收待完成”。未完成真实 GUI 验收和生产夹具逐字段比对前，不标记为 Release Candidate。

## 8. 变更记录

### 2026-09（相机取流扩展，风险收敛）

- 登记实验模块 `05_camerainput`（`pcv_m05_camerainput`）：`PCV_BUILD_EXPERIMENTAL_CAMERA` 默认 `ON`，默认构建包含相机扩展，仍可显式关闭；SDK 存在时链接真实 `Mv3dLp`，否则保留 Mock 演示路径；
- `20_pointcloudrender` 新增 `PointCloudCanvas::setCloudStreaming`（实时流式刷新，保留覆盖层）；
- `apps/pointcloudview` 新增 `CameraDialog`（相机实时取流页面）与 `live_pipeline`（JSON pipeline 执行器，camera.nextFrame/filter.voxel/canvas.publish）；
- 实时路径仍为实验链路；`live_pipeline` 尚未接入生产相机链；
- 验证：实验开关 OFF 的 Debug CTest `13/13`，开关 ON 且 SDK 存在的 Debug CTest `17/17`；完整 SHA-256 缓存回归通过；
- 未验证项：真实相机 GUI、`SDKCoordinateType` 坐标映射、实机软触发/重连、关闭时序和 130 万点性能，均为发布阻断项。

### 2026-09（采集工作台 Designer UI）

- `apps/pointcloudview` 使用 Qt Designer 增加顶部工具栏、中央画布工作区及采集、清理、平面、输出四步导航；既有算法和输出契约不变；
- 实验相机对话框显示当前 `DeviceInfo` 字段和 Mock 源，未实现的相机参数以禁用控件显式提示，不纳入 v0.3 稳定能力；
- 图标资源仅服务 UI，不引入共享模块依赖。相机 GUI 使用 SDK 回调取流，默认请求 `Range_Image=7`；实时帧仅预览，冻结有效帧后进入既有点云处理和输出流程。

### 2026-09（全界面浅色主题补强）

- 主窗口与相机对话框补充嵌套面板、菜单、弹出列表、滚动条、SpinBox 和复选框的浅色样式，避免系统调色板导致深色残留；
- OpenGL 画布继续使用浅灰白清屏色和深色叠加文字，不改变渲染公共接口。

### 2026-09（浅色 UI + 深色 OpenGL 画布）

- 主窗口、工具栏、面板和对话框保持浅色视觉基线；中央 OpenGL 画布采用深色背景，以提高彩色点云、边缘和坐标轴的识别度；
- 画布叠加文字同步使用浅色高对比度，不改变渲染公共接口或数据契约。

### 2026-09-01（当前差异风险收敛）

- P0：帧槽通知改为加锁 setter/clear；FrameSource 默认拒绝不支持能力；转换单飞、仅保留最新待处理帧，按 generation 丢弃旧结果；关闭先清通知并等待转换和 SDK callback；
- P0：真实异常回调映射 `FrameKind::Exception`、异常类型和描述；callback 注册返回码检查；SDK lease 保证一次 Initialize 对应一次 Finalize；
- P0：PLY 缓存恢复完整 SHA-256，缓存版本升至 3；同尺寸同时间戳的未采样区域修改必须 cache miss；
- P0：移除 `CMAKE_DEPENDS_USE_COMPILER OFF`，统一 `VSLANG=1033` 并对 MSVC ABI/64 位探测失败硬失败；
- P1：生产链统一 `PipelineRunner`、canonical warning 透传、PLY 完整回读、换帧清除点索引覆盖层；P2：DP2240 实测坐标/软触发/重连/性能后再定帧率目标。

### 2026-09-01（P1 生产链与契约补强）

- `CameraDialog` 的实时帧统一经 `PipelineRunner` 执行，内置链为 `camera.nextFrame → filter.voxel（可选） → canvas.publish`；生产链携带 `frameNumber`、时间戳和 stream generation，旧 generation 不得发布。
- 外部 pipeline 使用 `pointcloudview-live-pipeline-v1`，支持命令行/`PCV_LIVE_PIPELINE_FILE`/应用目录配置文件；解析、顺序、重复 ID、节点类型和参数任一失败时回退内置链并记录 warning。
- `CachedCloudResult`/`LoadResult` 增加 warnings；canonical PLY 写入失败保持主读取成功，但 warning 透传到日志和状态栏。
- binary PLY writer 增加 `Point3D` 标准布局、可平凡复制、字段偏移和大小约束；非 little-endian 使用逐字段编码。
- 新增 pipeline 严格校验、预转换帧透传、warning 及 binary writer 回读回归；实验相机 Debug CTest `17/17` 通过。

## 9. 风险与验收门槛

### 2026-09-02（registration_diagnostic 通用 CLI）

- `registration_diagnostic` 保留 `pointcloudview` GUI，并支持通用 `--input`/`--input-dir` 多帧输入、`--pose-info` 位姿匹配及 `inspect` PLY header `format` 检查；底层仍统一调用 `pcv::interface::stitchRawLineProfiles`。
- 验证：Windows Debug 工具构建通过；全量 CTest `13/13` 通过。真实 PLY/XML 拼接夹具和 GUI 点击验收未执行。

### 2026-09-02（Unicode 路径与 Binary PLY 门禁）

- 移除构建脚本和 CMake 的 ASCII-only 路径拒绝；保留 MSVC `VSLANG=1033` 与 header dependency 检查。
- 外部输入兼容 ASCII、binary little-endian 和 binary big-endian；所有成功输入必须生成并校验 binary little-endian canonical PLY，失败时主流程停止。正式、预览、诊断、平面和临时工件 PLY 均通过共享 Binary writer 输出。

- 核心门槛：实验开关 OFF/ON 均可配置，Debug/Release 全量 CTest 通过；Windows 使用 `NMake Makefiles` 并验证公共头依赖，JOM/Ninja 不作为当前支持生成器；
- 相机门槛：Mock 连接/首帧/停止/断开/重开、`POINTCLOUDVIEW_SELFTEST_CLOSE=1` smoke 及 DP2240 实机 GUI/坐标/重连证据；当前未完成；
- 不将 `live_pipeline` 单测或 DLL 部署视为生产链路验收。

### 2026-09-01（扫描 JSON 主线路崩溃修复：构建依赖跟踪）

- 现象：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`，主线路无法跑通；
- 根因：Qt Creator 的 `NMake Makefiles JOM` 构建树未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节），`modules/10_pointcloudread` 的 `ply_reader.h` 扩展了 `PlyReadOptions`/`PlyReadResult` 后，`cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 未重新编译，与新编译的 `ply_reader.cpp.obj` 按不同结构体布局链接，`readPly()` 返回值写穿了 `readPlyCached` 的栈槽；
- 影响面：扫描 JSON 线路（`prepareTempWorkpiece` → `readPlyCached`）、普通"打开点云"与 `pointcloudstitch` 的读取路径；单元测试因可执行文件在头文件变更之后重建而全部通过，无法暴露该问题；
- 历史处置已被 2026-09-01 的 `VSLANG=1033` 策略替代；不再使用 `CMAKE_DEPENDS_USE_COMPILER OFF`，以恢复 ABI 探测并保持 `/showIncludes` 依赖解析；
- 验证：新建 `NMake Makefiles` 构建目录 Debug 构建通过，`.obj.d` 记录 `ply_reader.h`，全量 CTest 13/13 通过；相机开关 ON 的独立构建和 CTest 17/17 通过。`NMake Makefiles JOM` 实测生成空 `.obj.d`，脚本现已拒绝该生成器；Ninja 在中文 MSVC 下依赖同样失效。
- 未验证项：GUI 人工复测（打开扫描 JSON 与打开点云）需在重建后的可执行文件上执行；用户现有的 `build/Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 目录需重新配置并重新构建后才恢复正常。

### 2026-09（对齐 3DMVS 取流方式）

- 依据 3DMVS V3.1.4 实测日志与用户手册：真实相机 DP2240 取流 ImageMode=7（深度图）+ SDK 深度图转点云；虚拟相机走同一流程；缓存节点/重发包为 SDK 内部默认；
- 实现：默认 ImageMode=7；事件驱动取帧（onFramePublished）；转换移出 GUI 线程（QtConcurrent）；异常事件（FrameKind::Exception）+ 自动重连；软触发（SoftTrigger）；虚拟相机识别；
- 验证：frame_source_tests 增补通知/异常/深度分派用例，全量 17/17 PASS；应用构建通过；
- 未验证项：真实相机软触发/重连/深度图实机复测待执行。

### 2026-09（DP2240 实机取流修复）

- 现象：DP2240（MV-DP2240-01P）接入取流返回"无效帧"；
- 根因：SDK 日志确认设备为 HFR 线激光轮廓传感器，ImageMode=4（点云）下实际回传深度图帧（ImageType_Depth C16）；原转换器只接受 ABC32f；
- 修复：转换器新增 C16/ABC16 分派；FrameSource::convertFrame 真机 SDK 权威转换（MapDepthToPointCloud/ImageConvert）回退手写公式；ImageMode 自适应（不强制）；对话框取流模式选项 + 帧类型诊断日志；
- 验证：frame_converter_tests 增补 C16/ABC16 用例，全量 17/17 PASS；应用构建通过；实机 DP2240 复测待执行。

### 2026-09-02（DP2240 无效帧：ABC32f 默认链路）

- 诊断：`startup.log` 的 Range C16 回调为有效帧（`ImageType_Depth=0x011000b8`、`2100x3000`、`12600000` 字节），但深拷贝后调用 `MV3D_LP_MapDepthToPointCloud` 返回 `0x8006000c (MV3D_LP_E_ALGORITHM)`；SDK 日志 `TO_PROFILE_S32 config[0] [0,0,0,0,0,0]` 后内部错误，不能以手写 scale/offset 推导替代；
- 实现：`Mv3dLpCamera` 和 `CameraDialog` 默认请求 `ImageMode_PointCloud_Image=4`；SetParam、GetParam 和回读模式必须全部成功，否则按回调关闭、等待、StopMeasure、CloseDevice、Finalize 路径清理。模式 4 仅接收 `ImageType_PointCloud=0x026000c0` 的 ABC32f；C16/Profile 和非法 payload/bValid=false 直接拒绝；
- 保留：`Range_Image=7` 是显式诊断选项，仍仅可调用 SDK 转换且失败 fail-closed；实时预览、latest-frame-wins、后台转换、冻结后处理和正式输出契约不变；
- 验证边界：自动化覆盖模式/类型策略与 ABC32f 长度/尺寸门禁。`DB1723975 / MV-DP2240-01P` 仍须现场确认模式 `4`、类型 `0x026000c0`、`width * height * 12`、持续预览、冻结、软触发、断线重连、关闭时序和目标帧率；若模式 4 仍输出 C16，保持失败并单独评估原始 SDK 回调内转换方案。

### 2026-09-02（DP2240 完整扫描回调链）

- 根因：实机 `ImageMode=4` 的 `Profile -> PointCloud` 转换成功，但每个回调仅为 `3200x24~30` 行，且 `nFrameNum` 与分片高度连续组成一次约 3000 行扫描；最新帧槽逐批覆盖造成画布只显示线轮廓；
- 实现：默认恢复 3DMVS `Range_Image=7`，严格 Set/Get 回读后读取设备 `Height`。SDK 原始回调内完成 C16/ABC16 到 ABC32f 的权威转换；内部组帧器以首批 `nFrameNum` 作为运行序号锚点，仅在后续编号连续、宽度一致且累计高度等于 `Height` 时发布完整扫描；不假定首批编号为 0；
- 边界：组帧失败、丢包、停止、断线与重连均清空未完成扫描。`LatestFrameSlot`、实时 canvas、冻结及既有输出只接收完整 ABC32f；冻结按钮拒绝残缺扫描；
- 验证：自动化覆盖完整帧、连续/末批、跳行、重复、宽度变化、重置和内存上限。DP2240 仍须现场确认 `ImageMode=7` C16 回调、`MV3D_LP_MapDepthToPointCloud=MV3D_LP_OK`、约 `2100x3000` 完整扫描、冻结、停止、重连及坐标方向。

### 2026-09-02（DP2240 Profile 原始回调转换）

- 新证据：`DB1723975 / MV-DP2240-01P` 已确认请求/回读 `ImageMode=4`，但稳定回传有效 `ImageType_Profile=0x023000b9`，例如 `3200x26x6=499200` 字节；SDK 定义明确支持 `ImageType_Profile -> ImageType_PointCloud`，且帧 scale/offset 为零，禁止手写坐标换算；
- 实现：在 `Mv3dLpCamera` SDK 回调内对原始 Profile 调用 `MV3D_LP_ImageConvert`，对 Range 深度调用 `MV3D_LP_MapDepthToPointCloud`，成功后立即复制 ABC32f 到 `LatestFrameSlot`。后台、实时预览、冻结和正式输出只处理拥有式 ABC32f；
- 门禁与风险：状态必须为 `MV3D_LP_OK`，输出类型 `0x026000c0` 且长度 `width * height * 12`；失败 fail-closed。实机仍需确认回调内 SDK 转换无重入/关闭死锁、坐标方向和性能；若直接 Profile 转换失败，后续仅评估 SDK 两步 `Profile -> Profile_ABC32 -> PointCloud`，不自动启用近似算法。

### 2026-08-31

- 完成 v0.3 模块目录、规范 target、UI 独立文件和模块文档治理。
- 完成共享 seam fusion、平面拟合和渲染组件实现迁移。
- 完成 `registration_diagnostic` 到模块接口的迁移。
- Debug/Release 构建和全量 CTest 已执行；GUI 人工验收、生产夹具验收和兼容层删除仍待完成。
