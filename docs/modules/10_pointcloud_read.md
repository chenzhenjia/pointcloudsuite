# 10_pointcloud_io：点云读写、相机输入与事务输出

公共接口 `pcv::detail::io::inspectPlyHeader()` 仅读取 PLY header 至 `end_header`，返回格式、版本、vertex 数量和 header 错误，不解析点数据；供 `pointcloudcli inspect ply` 和 `registration_diagnostic inspect` 使用。`pcv::detail::io::readPly()` 供 `pointcloudcli read ply` 输出完整点云摘要，保留 ASCII、binary little-endian/big-endian、Bounds、intensity 和取消语义。

状态：已实现。规范实现位于 `modules/10_pointcloud_io/{include,src}`，模块 target 为 `pcv_m10_pointcloud_io`；旧 target `pcv_m10_pointcloud_read`、`pcv_m10_pointcloudread`、`pcv_m05_camerainput`、`pcv_io`、`pcv_filtering` 和 `pcv_m80_plane_output` 为迁移期兼容入口（仅转发）。本文件文件名保留旧名称以避免历史链接失效。

相机能力（原 05 已并入）：2026-09-06 起原 `05_camerainput` 并入本模块，历史记录见 [05_camerainput.md](05_camerainput.md)。并入内容含相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现；相机为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力，无 SDK 仍可构建纯函数转换与 Mock 源，真实 Mv3dLp 编译需 SDK。相机取流不属于 v0.4 稳定发布基线。

职责：兼容读取 PLY ASCII/binary LE/BE，并在成功读取后强制生成 `binary_little_endian` canonical PLY；缓存、诊断和后续落盘 PLY 统一为 Binary LE。canonical 写入或回读校验失败时主流程失败。过滤、PNG/PLY/JSON writer 和 `pcv::io::ArtifactTransaction` 也归属本模块；30/60 不直接提交最终文件。

`pointcloudcli inspect ply` 仅调用 Header 检查接口，不读取 payload；`pointcloudcli read ply` 调用完整读取接口并输出点数、Bounds、intensity 和取消状态摘要。CLI 的缓存根目录由应用层通过 `pcv::runtime::configureDataDirectory()` 注入，GUI 仍使用自身既有数据目录配置。

接口/测试：`pcv::detail::io::readPly`、`readPlyCached`；`ply_reader_tests`、`cloud_cache_tests`。相机帧转换/最新帧槽/Mock/组帧接口与测试（`frame_converter_tests`、`frame_source_tests`）历史见 `05_camerainput.md`。

亮度图验证：`CameraFrame::intensityData` 保存与深度/点云栅格一一对应的 `Mono8` 数据；`extractIntensityImage()` 仅接受有效完整扫描并严格校验 `width * height` 字节。独立验证 GUI `intensity_viewer` 在实验相机开关开启时构建，使用 `Range_Image=7`、`MV3D_LP_RegisterImageDataCallBack`，实时预览并手动保存 PNG；不提供 Mock，不保存高度文件。

相机内置激光：`Mv3dLpCamera::requestLaserEnabled()` / `activeLaserEnabled()` 通过 SDK 参数 `LaserEnable`（`ParamType_Bool`）在 `start()` 内、`MV3D_LP_StartMeasure` 之前完成 GetParam 探测 → SetParam → GetParam 读回；默认请求开启，读回值与请求不一致时 `start()` 失败并清理设备。`intensity_viewer` 的 `chk_camera_laser` 默认勾选，关闭后为外设激光模式（外设由外部控制器或人工控制，本程序不控制外设），停止/断开/退出不恢复设备激光状态。

最后核对日期：2026-09-18。当前状态以源码、CMake 和测试为准。本文件历史正文中的旧目录名/旧 target 为当时名称，仅用于追溯。

### 2026-09-18（相机内置激光开关 LaserEnable）

- 登记主模块 10_pointcloud_io，配套应用 `apps/intensity_viewer`；只改相机封装、亮度图验证 GUI、相机测试和文档，未改动回调、亮度深拷贝、`ScanFrameAssembler`、`LatestFrameSlot` 及输出契约。
- `Mv3dLpCamera` 新增 `void requestLaserEnabled(bool)` 与 `std::optional<bool> activeLaserEnabled() const`：请求默认开启，只在 `start()` 前生效（取流中调用被忽略并记录警告，运行期不修改设备）；`activeLaserEnabled()` 返回本次取流会话中 SDK 成功读回并确认的 `LaserEnable` 状态，未启动、启动失败或停止后为空 `optional`。
- 启动顺序：`MV3D_LP_Initialize` → `MV3D_LP_OpenDeviceBySN` → `LaserEnable` 配置 → `ImageMode=Range_Image(7)` → `Height/Trigger` → `MV3D_LP_RegisterImageDataCallBack` → `MV3D_LP_RegisterExceptionCallBack` → `MV3D_LP_StartMeasure`。
- `Impl::configureLaserEnabled()` 使用 `MV3D_LP_PARAM{}`、`enParamType=ParamType_Bool` 和固定键 `"LaserEnable"`，完成 GetParam 探测 → SetParam → GetParam 读回；任一步失败或读回值与请求不一致时 `start()` 返回失败并复用既有 `shutdown()` 清理设备，不进入取流。`LaserEnable` 只在 GUI 主线程配置，回调线程不访问该参数。
- 错误信息包含 `LaserEnable`、请求值、实际值和 SDK `statusText`；文本与判定规则由纯逻辑策略 `modules/10_pointcloud_io/src/laser_enable_policy.h` 提供（`kLaserEnableKey`、`kDefaultLaserEnabled`、`laserRequestApplicable`、`laserEnableReadbackMatches`、`laserEnableProbeError`/`laserEnableSetError`/`laserEnableReadbackError`）。
- `intensity_viewer` 新增 `chk_camera_laser`（默认勾选）、`lbl_laser_mode` 和说明文字「关闭后使用外设激光（外设由外部控制）」：未连接禁用、已连接未取流可修改、取流中禁用；`toggleStreaming()` 在 `start()` 前把勾选值传给 `requestLaserEnabled()`，启动成功后用 `activeLaserEnabled()` 显示 SDK 实际读回值；启动失败时保持非取流状态、显示明确错误并保留用户选择；停止/断开只清除读回显示，不恢复设备状态。
- 边界：外设激光由人工或现有外部控制器控制，本程序不接 PLC、串口或 GPIO；第一版仍只输出 `Mono8` 亮度图和 PNG，不增加高度文件，也不把高度有效性作为验收条件；不提供 Mock SDK，SDK 未编译时维持现有不可用状态。
- 验证（构建目录 `D:\pcv-build-intensity-debug`，Debug / NMake Makefiles / Qt 6.8.3 / MSVC 19.51.36252.0，`PCV_BUILD_TESTS=ON`、`PCV_BUILD_EXPERIMENTAL_CAMERA=ON`、`PCV_CAMERA_HAVE_HIKSDK=1`）：
  - `cmake --build D:\pcv-build-intensity-debug --config Debug --target intensity_viewer` 退出码 0；全量 Debug 构建退出码 0。
  - `ctest --test-dir D:\pcv-build-intensity-debug -C Debug --output-on-failure` → 20/20 通过，0 失败（含新增激光策略断言的 `frame_source_tests`）。
  - `QT_QPA_PLATFORM=offscreen` 启动 `intensity_viewer.exe` 烟测通过：进程运行 6 秒无崩溃、无 stderr 输出。
  - 3DMVS 虚拟 DP 系列设备（`Vir6663020`，`MV-DP3020-01H`）端到端验证（临时探针驱动真实 `Mv3dLpCamera`，不进入仓库）：`requestLaserEnabled(false)` 后 `start()` 成功且 `activeLaserEnabled()==false`；取流中调用 `requestLaserEnabled(true)` 被忽略并记录警告，下一次 `start()` 仍写入 `false`；`requestLaserEnabled(true)` 后读回 `true`（设备恢复默认开启）；停止后 `activeLaserEnabled()` 为空。SDK 侧 `GetParam/SetParam/GetParam` 均返回 `MV3D_LP_OK`，`enParamType=1 (ParamType_Bool)`。
  - DP2240 真实设备现场验收通过：`LaserEnable=true/false` 均可设置并读回；连续模式下 `Range_Image=7` 回调取流正常；`Mono8` 亮度图可实时预览并保存为 PNG；关闭相机内置激光后，外设激光模式仍可完成亮度图取流验证；停止、断开和退出不自动恢复设备激光状态。
  - 现场验收范围不包含高度文件、点云文件或外设激光器通信控制；外设激光由人工或现有外部控制器控制。
- 未验证项：`PCV_BUILD_EXPERIMENTAL_CAMERA=ON` 但无 SDK 的配置本轮未重新构建；本次文档不扩展高度输出或外部设备通信协议的验收结论。

### 2026-09-09（亮度图取流验证 GUI）

- `ScanFrameAssembler` 同步拼接几何与 `intensityData`，拒绝亮度缺失、长度不匹配或不完整扫描。
- 新增 `extractIntensityImage()` 纯函数和 `intensity_image_tests`，覆盖 Mono8 提取、无效/短负载拒绝及 PNG 回读。
- 新增独立 `intensity_viewer`，复用 `Mv3dLpCamera` 生命周期、回调和 `LatestFrameSlot`，支持设备刷新、连接、Range_Image 取流、预览和非覆盖 PNG 保存。
- Debug 验证：`intensity_viewer` 构建成功，全量 CTest `18/18` 通过；无设备 GUI offscreen 启动烟测通过。真实设备亮度帧、多批次同步和坐标/高度定义仍待现场验收。

## 变更记录

### 2026-09-06（模块重命名与相机能力并入）

- 目录重命名：`modules/10_pointcloud_read`，target 改为 `pcv_m10_pointcloud_read`；旧 `pcv_m10_pointcloudread` 保留为兼容 INTERFACE，转发到新 target。
- 原 `05_camerainput` 并入本模块：相机公共头 `pcv/camera/*`、帧转换、`LatestFrameSlot`、Mock 源、扫描组帧与可选 Mv3dLp 实现均随目录迁入；相机作为 `PCV_BUILD_EXPERIMENTAL_CAMERA` 可选实验能力保留，无 SDK 时纯函数转换与 Mock 源仍可构建，真实 Mv3dLp 编译需 SDK。`pcv_m05_camerainput` 保留为兼容 INTERFACE，转发到 `pcv_m10_pointcloud_read`。相机详细历史与验收记录见 `05_camerainput.md`。

### 2026-09-01

- binary 读取并行快速路径泛化：原 compactFloatXyz 仅支持"恰好 3 个 float 且顺序为 x,y,z"，现扩展为"全部属性固定宽度 + x/y/z 为 float32"即按固定 stride mmap + 多线程按列偏移解码，覆盖 4-float（x,y,z,intensity）、6-float（x,y,z,nx,ny,nz，含 canonical 缓存文件）与乱序属性，worker 数沿用自适应选择（上限 4 核），并支持 PlyReadOptions::binaryWorkerCount 显式指定。
- 新增可选强度通道：PlyReadOptions::readIntensity 开启且文件声明 intensity 属性时，PlyReadResult::intensity（与 points 等长的 QVector<float>）被填充；ASCII 与 binary（快速/慢速）路径语义一致，uchar/ushort/double 等类型归一化为 float。未开启或文件无 intensity 时保持空数组，不改变 Point3D 布局与既有结果结构。
- PlyReadResult::binaryWorkerCount 记录 binary 快速路径实际 worker 数（慢速路径为 1）。
- 测试：ply_reader_tests 新增 4-float LE/BE、乱序属性、6-float 法向、uchar/ASCII intensity 与显式/自适应多线程 worker 用例。

性能优化（同日）：

- P0：PlyReadResult 新增 fastPath 标记，明确区分并行固定 stride 路径与串行保底路径，便于诊断是否走快速路径。
- P1：selectWorkerCount 阈值放宽——点数 >= 10 万或 payload >= 4MB 直接使用全部 4 核；>= 2 万点或 >= 1MB 使用 2 核；低于阈值仍单线程避免线程启动开销。
- P2：decodeBinaryFloat 内联；连续 float32 x,y,z 且 little-endian、stride 4 对齐时按 float[3] 块直接读取（去逐列调用）；标量/intensity 解码改为按整数 kind 预分派的 switch（去除每点 QByteArray 字符串比较）；并行 chunk 的进度原子计数从每点一次降为每 1 万点一次（尾部余数补计）。
- P3：固定宽度属性统一走并行解码——mmap 失败时改为整块读入后仍按 stride 分块并行，非 float32 x/y/z（如 double）同样并行；仅 list 等非常规布局保留串行保底。
- 测试：ply_reader_tests 为各快速路径用例新增 fastPath 断言，并新增 double x,y,z 并行解码用例。

缓存读取优化（同日）：

- P0：sourceFingerprint 使用完整文件 SHA-256；`readPlyCached()` 单次生成 `SourceIdentity` 并复用到路径、校验和写入，缓存版本升至 3，避免未采样区域修改返回旧点云。
- P1：writeBinaryPly 使用整块写；`Point3D` 增加 standard-layout/六个连续 `float` 的 `static_assert`，带/不带 normals 的完整字段回读测试列入后续路线图。
- P2b：readPlyCached 中 canonical PLY 写失败不再清空结果/整体失败；结果保持 `ok=true`，并通过 `CachedCloudResult.warnings` 透传到应用层，同时记录 `qWarning`。
- 测试：cloud_cache_tests 覆盖首次解析、canonical 生成、二次缓存命中和完整指纹失效；PLY writer 回归覆盖 binary little-endian 带/不带 normals 的逐字段回读。

ABI 与事故记录（同日）：

- 本次对 `PlyReadOptions`（新增 binaryWorkerCount、readIntensity）和 `PlyReadResult`（新增 intensity、binaryWorkerCount、fastPath）的扩展改变了结构体尺寸，属于**破坏性 ABI 变更**：仍按旧布局编译的调用方目标文件与新的 `ply_reader.cpp.obj` 链接在一起时，`readPly()` 按新尺寸写入返回值，会越过调用方为返回值预留的栈槽。
- 实际故障：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`。`canonical` 是 `cloud_cache.cpp` 中 `readPlyCached` 的局部变量，位于返回值槽相邻位置；调用链为 `openTempScanningInfo` → `prepareTempWorkpiece` → `readPlyCached` → `readPly`。
- 根因不在算法逻辑，而在构建树：Qt Creator 的 `NMake Makefiles JOM` 目录未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`），`ply_reader.h` 变更后 `cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 没有重新编译，与新编译的 `ply_reader.cpp.obj` 混装进同一个 `.lib`。单元测试可执行文件在头文件变更之后重建，自身布局自洽，因此全部通过、无法暴露该问题。
- 历史处置已被 `VSLANG=1033` 策略替代：配置和构建阶段统一固定 MSVC `/showIncludes` 语言，并保留依赖数据库护栏。详见 `docs/development/build-windows.md`。
- 验证：新建 `NMake Makefiles` 构建目录后，`.obj.d` 中出现 `ply_reader.h`；基线全量 CTest 13/13 通过，相机开关 ON 的全量 CTest 17/17 通过。`NMake Makefiles JOM` 在当前环境生成空 `.obj.d`，不作为支持生成器。
- 约束：今后扩展 `PlyReadOptions`/`PlyReadResult` 必须把新字段追加在结构体末尾，并在本文件变更记录中显式标注"需要全量重建"。

### 2026-08-31

- 建立模块边界和 `pcv_m10_pointcloudread` 兼容入口；源码暂留 `src/io`，未移动用户未提交修改。
- M2：PLY 读写、缓存和 writer 已迁移到 `modules/10_pointcloudread/{include,src}`，旧 `pcv_io` 与头文件作为兼容层保留。
