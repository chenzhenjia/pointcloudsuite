# 10_pointcloud_io：IO、预处理与事务输出

2026-10-30 扫描几何深度按需处理：`scan_geometry_demo_v1` 轮次浏览使用 `readCapturedFrame(...PreviewOnly)` 读取/校验原始C16及必要亮度，不在预览阶段调用SDK转换；用户开始当前帧处理时复用 `materializeCameraFrame()`。SDK输出仍需传给模块50按源像素行转换，模块10没有改变会话格式或SDK映射契约。源帧由应用会话保留，活动点云由应用在审核确认后释放。[开发记录](../development/scan_geometry_demo_v1.md#2026-10-30-sdk深度缓存与按需物化)。

2026-10-23 亮度保存/纹理解耦 v2：`readCapturedFrame()` 新增 `CaptureFrameReadMode`，默认 `WithIntensity` 兼容旧调用，显式 `GeometryOnly` 严格读取几何/来源映射且不访问亮度载荷。亮度采集、缺图停采、逐帧 PNG/Mono8 事务保存不变；事后图片损坏不影响几何专用读取。[当前说明](../development/camera_capture_intensity_decoupling_v2.md)。

2026-10-16 启动顺序修复v1：SDK整图在StartMeasure成功后清缓存，再开放内部图像门禁；清缓存失败停止测量并关闭释放，异常回调在启动期间仍可上报。预览/固定行数启动行为及公共接口不变；增加阶段/原始错误码诊断和真实驱动SDK替身回归。[验证与现场步骤](../development/camera_startup_order_fix_v1.md)。

2026-10-16 连续采集v2：新入口显式SdkImageFrame，保留设备触发配置、SDK完整图像一图一帧、实际高度独立；拒绝未确认边界/轮廓分片，v2会话支持不同高度，v1固定行数保留。增加队列/转换/保存统计；物理门控关系仍须现场验收。[当前说明](../development/camera_capture_sdk_frame_v2.md)。

2026-10-16 连续采集增量：capture_session_v1提供独立有界完整帧队列、逐帧事务保存/离线读取；转换结果保留原始栅格下标，最新帧预览默认不变。显式固定行数禁自适应猜测，异常停止且保留已保存帧。[实现与验收](../development/camera_capture_pose_stitch_v1.md)。

版本 v0.5，核对日期 2026-10-15。target：`pcv_m10_pointcloud_io`；依赖 pcv_core、pcv_infrastructure、Qt Core/Gui，不依赖 apps/QWidget。[架构](../architecture/software_architecture_v0.5.md) · [契约](../contracts/data_contracts_v0.5.md) · [日志](../changelog/v0.5.md)。

## 当前 API

| header | namespace | capability |
|---|---|---|
| pcv/io/ply_reader.h、cloud_cache.h、ply_writer.h | pcv::detail::io | inspectPlyHeader/readPly/readPlyCached/writeBinaryPly |
| pcv/processing/downsample.h、statistical_filter.h | pcv::detail::filtering | 降采样与统计过滤 |
| pcv/io/artifact_transaction.h | pcv::io | ArtifactTransaction |
| pcv/io/plane_output.h | pcv::output | writePlaneOutput 旧三件套入口 |
| pcv/camera/* | pcv::camera | 帧转换、Mock、最新帧槽、组帧、SDK 可选 |

PLY 支持 ASCII/LE/BE，按属性名读取坐标/法向，保留点序、source index、bounds、取消与非有限统计。readPly 只读；缓存入口默认生成/验证 canonical Binary LE，写入或回读失败为失败，不沿用历史仅警告说法。

## 输出与相机边界

ArtifactTransaction 提供 stageBinaryPly/stageGrayscalePng/stageJson/stageBytes/commit/abort，默认不覆盖，校验输出根及相对路径，使用 staging、备份和失败回滚。Mask writer 转 Grayscale8 后非零变 255；JSON bytes 不自动做 schema 校验。旧 writePlaneOutput 仍有自己的事务实现，在 10 内但未复用该类，后续 P2 收敛。

相机为 PCV_BUILD_EXPERIMENTAL_CAMERA 控制的实验能力，根默认 ON、构建脚本默认 OFF；SDK 头/库存在才编译真机源，无 SDK 保留纯函数与 Mock。LatestFrameSlot 为 latest-frame-wins；SDK 数据须复制到受控生命周期，真机 C16/ABC16 SDK 转换失败不发布近似坐标，完整扫描门禁及停止/重连需现场验收。实时预览历史 300000 点显示预算不代表完整生产点云。

## 兼容与验收

旧 05、10 read、pcv_io/filtering、80/output target/header 保留转发，禁止重复实现。当前 namespace 门面迁移未完成。

测试归属：ply_reader、cloud_cache、artifact_transaction、downsample、statistical_filter、plane_output；相机另有 frame_converter/frame_source/intensity_image 及应用 pipeline 测试，注册依构建选项。后续覆盖 LE/BE/ASCII、法向/强度、缓存失败、路径拒绝、覆盖/取消/回滚。历史 DP2240 试验见[历史归档](../archive/history_pre_v0.5.md)，不能替代当前现场验收。本轮未构建或运行测试。

批量选面增量（2026-10-16）：`CloudCachePolicy::prepareCanonicalPly` 默认true，四参 `readPlyCached()` 可显式false以跳过canonical生成/读回比较；原三参入口默认行为不变。始终保留完整SHA-256/头/点数/载荷校验，指纹和缓存读写每1 MiB检查取消；新增fingerprintMs/cacheReadMs/cacheWriteMs诊断。见[整合说明](../development/batch_plane_selection_template_integration_v1.md)，回归源码已补但未运行。


2026-10-16 整合v2：批量调用继续显式CloudCachePolicy{false}，保留canonicalReady=false、完整SHA-256与缓存/头校验；应用会话补记既有parseElapsedMs，不新增正式产物字段。见[当前整合](../development/batch_plane_selection_template_integration_v2.md)，本轮未运行动态验证。

2026-10-22 连续采集纹理点云 v1：`CameraFrame` 在 SDK 回调中深拷贝 `pIntensityData` 及原始栅格尺寸；`CaptureConfiguration::requireIntensity` 仅由连续采集向导开启，队列严格检查 Mono8 载荷长度和点云栅格一致性。采集帧事务增加 `intensity.png`、`intensity.mono8` 及可选 `frame.json` 亮度元数据，`readCapturedFrame` 校验并回读两种格式；旧 v1/v2 会话仍可读取且显示无亮度状态。高级预览、固定行数兼容入口和相机数据契约保持不变。[实现说明](../development/camera_capture_texture_v1.md)


2026-10-23 深度缓存v1：owned逐行元数据、SDK版本/配置指纹、materializeCameraFrame设备无关官方转换和进程转换锁；C16及必要辅助亮度保存session-v3/frame-v2，PreviewOnly/GeometryPreview不物化XYZ，旧接口兼容。[说明](../development/camera_depth_preview_cache_v1.md)。
