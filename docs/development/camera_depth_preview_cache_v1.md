# 相机深度图主缓存与悬停选面 v1

日期：2026-10-23；owner：主线/Codex。用户授权实施、源码树外Debug/Release构建和测试；保留此前相机/纹理/CAD/STEP/工序JSON修改，不提交、push、上传或迁移旧会话。

## 数据、接口和兼容

真实SDK整图向导默认RangeImage/C16：回调校验并深拷贝深度、原始亮度、逐行曝光时间戳/编码器、scale/offset、reserved、帧号/时间戳，不立即展开XYZ。最新帧槽允许跳帧，正式记录队列有界；异常停止软件接收并排空/join，不控制机器人、门控或光源。

`CaptureConfiguration::storeRawDepth`显式启用新契约，旧调用默认false；固定行数、ABC32f/Mock兼容入口保留旧格式。`CameraFrame`新增owned辅助元数据、SDK版本及配置指纹。模块10的`materializeCameraFrame()`统一SDK引用计数，不打开设备，进程级转换锁覆盖输出消费；点序、原始栅格下标及已有无效点策略不变，不使用Mock近似公式替代SDK。

SDK1.3.4.2的`MapDepthToPointCloud`需要亮度缓冲，而且亮度值影响输出点有效性。必须保存实际原始`conversion_intensity.mono8`，正式转换校验并读取；不以全零或其他填充值替代。用户可见`intensity.png/intensity.mono8`仍仅由同步亮度开关控制，事后损坏不影响独立转换辅助数据；辅助数据本身损坏阻止几何转换。

新会话`pcv-camera-session-v3`，逐帧`pcv-camera-frame-v2`。ArtifactTransaction提交`depth.c16`、`frame.json`、存在的`exposure.i64/encoder.i32`、转换辅助亮度，以及启用时的PNG/Mono8。深度/逐行数据明确Little Endian，校验长度及SHA-256。新格式不强制生成PLY/逐点64位下标/有效点数，列表显示“按需转换”。旧session-v1/v2、frame-v1不改写。

CaptureFrameReadMode保留WithIntensity/GeometryOnly，新增PreviewOnly和GeometryPreview；新格式两种Preview不调用SDK、不生成XYZ/下标。前者额外验证用户亮度，后者不访问用户图片；两者都校验必要转换辅助数据。审核、几何校验和纹理准备使用Preview；30实际拼接时按需转换，来源记录真实depth.c16，50数学及完整合并语义不变。

SDK版本及转换配置指纹不一致时阻止正式转换，显示/日志配置不计入指纹；要求保留空点栅格及mm单位。无SDK构建仍可读写/预览原始深度，正式转换明确报错。没有原始深度的历史会话继续PLY预览并提示。

## 显示、选面和发布

20新增SDK无关`pcv::render::DepthImageDisplayV1/DepthImageCanvasV1`：有符号C16直接生成8位伪彩显示，-32768无效值暗背景；独立亮度切换/掩码覆盖，不改原始数据；滚轮缩放、右键平移，显示坐标回译原像素。20不依赖10/60或SDK。

应用`CameraDepthPreviewV1`接入高级实时预览、最近已保存采集帧及历史审核。进入选面固定当前完整帧，新采集继续；100ms防抖、单后台任务、最新鼠标请求优先。同候选区域复用掩码；首次按需转换完整XYZ并复用60原单击选面索引/算法，surfaceToleranceMm=0.4、maximumGapMm=3，最多4计算worker，共享256MiB可选优化预算，不降采样/补点。

左键锁定候选、按钮确认正式掩码/模型；重新选面解除锁定，退出恢复实时。审核另一帧取消并join旧任务；旧revision/token结果不发布。关闭先取消/等待，再释放点云、索引及帧。仅一个活动帧持有完整XYZ/索引，历史列表不缓存多帧完整XYZ。

选区保存到新`camera_plane_selection_v1_<UTC时间戳>`目录：`mask.png`为Grayscale8 0/255，`selection.json`记录完整模型、参数和源深度/逐行及转换元数据指纹，不覆盖采集帧。完整帧/已确认平面支持主窗口发布或独立版本PLY导出，取完整实测点，标记CloudCoordinateFrameV1::Camera、camera/mm；输出只在源码树外。本轮不接入六分屏/所选平面拼接。

## 验证和性能门槛

真实SDK前置验证：SDK1.3.4.2，MV-DP3020-01H/Vir6663020，用户断开3DMVS占用后采集2714×2033虚拟帧。owned完整输入在断开设备后、Finalize/Initialize后转换都与回调基准ABC32f逐字节一致，原始深度未变。证据：`D:/pcv-build/camera-depth-gate-v1-20261023/sdk-gate-results.json`。不代表真实DP2240、编码器运动或物理精度通过。

新增depth_capture_tests_v1覆盖无损读写、逐行辅助元数据、Unicode、SHA校验、取消、用户亮度损坏隔离、队列排空、SDK配置不兼容及浏览不物化；camera_depth_preview_tests_v1覆盖固定帧、悬停/锁定/确认、选面发布、释放和关闭join，支持`--fixture <目录> --repeats <次数>`原生测量/截图。旧采集、SDK替身、亮度、拼接和选面套件继续回归。

正式门槛保持：当前电脑Release、真实2100×3000，从已显示深度帧的鼠标稳定到完整高亮绘制，30次无索引缓存均≤2000ms，包含防抖、转换、索引、完整选面及GUI绘制；有索引的新候选目标≤300ms。磁盘/SDK初始化另测，不以合成模块计时代替。现有虚拟样例552万采样首次原生测量约12.5s，2秒要求未通过；分阶段和最终证据追加如下，不缩减正式数据冒充达标。

## 风险和回滚

C16为2字节/采样，必要辅助亮度另1字节/采样，加少量逐行元数据，主要原始载荷相对12字节ABC32f约少75%，整个进程降幅须实测；选面临时XYZ/索引、已发布点云和拼接结果仍占业务内存。可见PNG/Mono8与转换辅助数据独立保存，磁盘收益不直接套用内存比例。

回滚只人工撤回本轮新入口、格式分派、控件、测试及文档增量，不整体恢复文件、不删除旧会话或并行修改。旧程序不认识新格式，回退前可显式导出PLY。最终构建/CTest/原生GUI结果和未完成项后续追加；未实际完成的检查不标记通过。

## 2026-10-23 功能基线最终验证与保存

用户明确要求保存当前功能为本地Git回退基线，不制作安装包、运行包或源码压缩包，不推送；后续栅格优化本轮只写方案。基线同时保留工作区已有亮度解耦、CAD/STEP及工序JSON功能，未整体还原共享文件。这里的“功能基线”表示以下配置回归通过，不表示性能、现场精度、所有可选组合或生产部署已经验收。

工具链为Qt6.8.3/MSVC19.51/CMake3.30.5/NMake，两个GUI、Tests及ExperimentalCamera开启，CLI/Tools/STEP导入关闭。此前新建的源码外Debug/Release目录均完成完整构建，最终全目标依赖重编成功；公共头已由可靠依赖跟踪重编，未覆盖旧程序。

| 验证 | 实际结果 | 源码外证据 |
|---|---|---|
| Debug完整CTest | 41/41，45.48s | `ctest-debug-baseline-plan.log` |
| Release最终完整构建/CTest | 构建exit0；41/41，12.22s | `build-release-baseline-final.log`、`ctest-release-baseline-final.log` |
| 无SDK配置 | View及两个深度测试target构建通过；定向2/2，0.38s | `build-nosdk-baseline-final.log`、`ctest-nosdk-baseline-final.log` |
| Debug/Release双GUI原生启动退出 | 共4次exit0；View确认RTX4060/OpenGL3.3 | `native-baseline-final/smoke-verified-results.json`及`runtime-verified/logs/startup.log` |
| Release原生六画布 | `selected_plane_dialog_tests_v1 --native-gl` exit0 | `native-baseline-final/six-canvas.log` |
| Release原生相机向导 | Mock流程、关闭/join及截图exit0 | `native-baseline-final/capture-panel.log`、`camera-wizard-connection.png` |
| 最新Release真实SDK虚拟帧 | 官方基准XYZ/栅格映射一致，无损保存重载后逐点memcmp一致；锁定/确认/释放通过 | `native-depth-baseline-final.log`、`depth-selection-review.png` |

上述文件统一位于 `D:/pcv-build/camera-depth-gate-v1-20261023`。构建目录为 `D:/pcv-build/camera-depth-v1-{debug,release,nosdk}-20261023`。实际Release构建命令为：

```powershell
.\scripts\build_windows.ps1 -Config Release -BuildTests -ExperimentalCamera -Generator 'NMake Makefiles' -QtDir 'C:/Qt/6.8.3/msvc2022_64' -BuildDir 'D:/pcv-build/camera-depth-v1-release-20261023' -CMakePath 'C:/Qt/Tools/CMake_64/bin/cmake.exe'
& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir 'D:/pcv-build/camera-depth-v1-release-20261023' -C Release --output-on-failure
```

无SDK配置通过将`PCV_HIKSDK_ROOT`指向源码外不存在的SDK目录实现，ExperimentalCamera仍开启；仅声明实际构建的View/两个深度target和两项测试通过，不宣称该配置全套CTest通过。STEP开启组合本轮未重新构建，保留其既有实现和独立历史证据。

### 性能与内存未完成项

2714×2033虚拟帧的30次无索引缓存诊断为9525～10912ms，均值10024.9ms；同区域复用3ms，已有索引的完整重新选面9171ms。该组过程中运行过Debug增量构建，仅作瓶颈诊断，不作为独占机器的正式验收数据。最新代码无并行构建的单次原生复核：总9865ms，转换379.842ms、索引151.383ms、完整选面9169.68ms，其中连通处理8528.71ms；同区域复用0ms，已有索引完整重新选面9478ms。包含100ms防抖及同步绘制，不以区域缓存命中冒充完整新候选。**首次≤2秒及已有索引新候选≤300ms均未通过。**不是要求的2100×3000真实现场样例，正式30次现场验收仍未完成。

最新单次进程工作集：浏览67010560 bytes，高亮326582272 bytes，退出后70184960 bytes；私有提交分别46579712/329699328/71155712 bytes。30次诊断退出后的私有提交从约70MiB上升至约157MiB，末段约153～157MiB，不能据此宣称“无持续累积”；完整XYZ/索引引用释放已由测试断言，分配器保留与潜在泄漏仍须后续区分。这里只记录阶段采样，不是整个进程峰值。

确认掩码/模型在同一源帧退出选面后保留，完整XYZ/索引释放；切换源帧后清除，独立版本选区保存绑定深度、真实转换亮度、逐行及配置指纹。保存/重载和选区事务检查已纳入最终41项回归。真实相机持续采集、运动编码器/时间戳、无效点/坐标方向、物理精度和干净机部署未完成验收，不以虚拟设备或Mock替代。
