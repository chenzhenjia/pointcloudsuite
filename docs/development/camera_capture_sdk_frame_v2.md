# 连续采集 v2：SDK 完整图像与自动行数

日期：2026-10-16；owner：主线/Codex。主责模块10与CameraDialog，复用30/50既有采集位姿直拼。用户确认全程门控、固定行频/匀速、3DMVS在扫描结束后显示一整幅、机器人不能等待；本轮不接PLC、不新增ICP。

## 操作与完整扫描边界

新采集入口默认“SDK完整图像（自动行数）”，无需填3000之类的固定行数。操作员每次开始会话前确认当前设备/触发配置在3DMVS中“一次门控扫描对应一幅完整SDK图像”，并确认门控无效、下一次扫描尚未开始。两个确认在启动后清除；它们是人工确认，不是程序已验证真实硬件边界。

停止旧测量后清理应用预览槽和SDK缓存，StartMeasure后持续接收；不逐帧重启、不改写TriggerMode/TriggerSource/TriggerSelector、不发软触发。能读取的实际触发参数进入运行日志，无法读取记unknown。旧预览与“固定行数（兼容）”仍沿用原默认行为；兼容模式才显示固定行数输入。

驱动仅允许已确认边界的二维DepthC16/PointCloudABC32f图像；ProfileABC16及未知类型明确拒绝。归一化后仍检查有效性、宽高、完整载荷及既有256MiB XYZ单帧上限。`bValid`、完整矩形载荷及历史默认`completeScan=true`本身均不足以确立扫描边界；驱动显式写入`ScanBoundarySource::SdkImageFrame`。此路径绕过固定行数累加器，下一幅不继承前一幅高度，不依赖SDK序号按+1或+height增长来猜边界。

每幅`nHeight`自动作为本帧实际行数。停止前先结束门控，确认界面本帧已接收；停止只保证已收到的完整SDK图像排空保存，不把未到达/部分扫描凑成完整图像，不靠空闲超时猜结束。用户必须复核Start/End对应真实第一/最后曝光行，而非含加减速的整段运动端点。

## 接口、回放和点序

- 公共`CaptureConfiguration`携带`CaptureBoundaryMode::{FixedRows,SdkImageFrame}`、fixedRows和sdkImageBoundaryVerified。默认配置/旧configureCaptureRows保持原路径；新入口显式选择SDK模式。FrameSource增加configureCapture；真实源和Mock都实现，未知源不能假称支持。
- CameraFrame尾部增加边界来源、原始SDK类型及receiveWorkNs/sdkConversionNs。SDK转换仍串行，并将转换输出的深拷贝纳入同一锁，避免SDK输出内存复用；没有迁移SDK转换线程。
- 新SDK会话是`capture_v2_*`/`pcv-camera-session-v2`，session.json记录boundary_mode=sdk_image和boundary_verification=operator_confirmed_device_configuration，没有统一rows要求。v1仍写/读原schema及固定rows检查，不重写历史目录。
- 逐帧仍复用pcv-camera-frame-v1、完整有效camera.ply、原始栅格raster_indices.bin，追加boundary_source/raw_frame_type/receive_work_ns/sdk_conversion_ns诊断；保存前过滤无效点但不改变其他点原行号。仅SDK模式的边界来源为sdk_image，旧帧不要求补新字段。
- 模块30无需新算法：逐帧读取各自width/height，经50既有`(sourceIndex/width)/(height-1)`插值一次。不同图像高度独立处理；原始Y不参与行进度，方向仍由Start/End决定。不执行ICP/seam/去重/投影；旧正式PLY/PCVMAP/manifest schema与来源顺序不变。
- 会话可重开、改排序/勾选/位姿并回放，XML快照和事务提交沿用v1。完整主界面发布、token/revision和join语义不变；选面不改。

## 过载和计时

默认待保存队列2帧/512MiB，处理帧/SDK内存/转换/事务/预览额外占内存，不是进程RSS上限。预览允许覆盖；正式采集队列满/坏图/转换或写盘失败锁首次错误并停止本轮软件接收，已保存帧保留。界面明确“软件已停止接收，机器人可能仍在运行”；没有机器人通信，不声称能暂停机器人。

队列统计接收帧/字节、当前/峰值队列帧数与字节、累计回调工作、SDK转换和等待时间。保存统计有效点转换、写盘（含映射序列化/事务提交）、实际文件字节和从记录器启动到采样的墙钟时间；平均保存MiB/s包含扫描空闲，不能误作磁盘纯写带宽。SDK转换是receiveWork子集，不可重复相加。界面显示实际行数、SDK号/设备时间戳、有效点、队列峰值与吞吐，停采日志写完整分阶段统计。

2100×3000且全部有效时，XYZ约75.6MB，64位映射50.4MB，基础保存约126MB/帧（十进制，不等于MiB）；1帧/s需要至少这一量级持续写入能力并留开销。增加缓存不能弥补长期采集速度大于写盘速度。

## 验证命令与现场验收

全新源码外目录，使用NMake与匹配Qt/MSVC。以下命令为本轮执行目标，实际通过结果在[版本日志](../changelog/v0.5.md#camera-capture-sdk-frame-v2)记录，不以列出命令视作通过：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -ExperimentalCamera -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-sdk-frame-v2-debug -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
.\scripts\build_windows.ps1 -Config Release -BuildTests -ExperimentalCamera -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-sdk-frame-v2-release -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-sdk-frame-v2-nocamera-debug -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-sdk-frame-v2-debug -C Debug --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-sdk-frame-v2-release -C Release --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-sdk-frame-v2-nocamera-debug -C Debug --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-sdk-frame-v2-debug -C Debug -R "capture_sdk_frame_tests_v2|capture_pose_tests_v1|camera_capture_panel_tests_v1|frame_source_tests|handeye_transform_tests|stitching_interface_tests" --repeat until-fail:100 --output-on-failure
```

自动测试覆盖不同高度一图一帧、未确认边界/轮廓拒绝、旧固定行数、首错/满队列/断线/磁盘失败、停止并发发布后join、v1/v2往返、不同高度下标/行进度/30°反向位姿、额外Qt共享与串行1/2/4输出映射等价、取消及Mock面板确认/直拼发布。触发保留策略使用纯函数回归加源码分支审计；Mock不能证明真实SDK/固件没有改写参数。

现场至少3次不同长度门控扫描：记录门控次数与SDK原始类型/序号/时间戳/尺寸，核对每次扫描、SDK整图和保存帧一一对应，验证行数/首末位置与3DMVS一致；按最高实际节拍持续接收，检查队列/写盘/内存和保存回放。若实际仍是分片且无可靠边界，本模式不得验收，不通过超时/固定行数/静默漏帧替代。速度不均匀、触发延迟及手眼/机器人误差仍可能造成拉伸、错位和双层。

回滚只人工撤回本轮边界配置/SDK保留触发/队列统计/v2回放/面板与对应回归文档增量，保留v1及其他已有未提交工作。未提交/推送/上传，不删除旧构建或原始会话；新schema不改变旧manifest。
