# 海康 3D 激光轮廓传感器点云取流直连 OpenGL 画布方案（调研 + 实施记录）

日期：2026-09
状态：实验实现；核心单测与构建通过，尚未接入生产 JSON pipeline，真实相机验收待完成。

## 1. 调研结论（开发指南 V1.3.2，Python 版 CHM 为权威流程参考）

> 项目基线为 C++17/Qt6/MSVC 且不依赖 Python，最终实现以 C++ 直接链接 Mv3dLp.lib。Python 版指南与 C 版 API 完全同构（ctypes 封装同一 Mv3dLp.dll），用于权威确认回调流程、数据结构与解析公式。

### 1.1 回调取图流程（指南"回调取图流程"章节）

1. MV3D_LP_Initialize() 初始化运行环境
2. MV3D_LP_GetDeviceNumber() / MV3D_LP_GetDeviceList() 枚举设备
3. MV3D_LP_OpenDeviceBySN()（或 OpenDeviceByIP()）打开设备
4. （可选）MV3D_LP_GetParam() / MV3D_LP_SetParam() 参数读写
5. （可选）MV3D_LP_RegisterExceptionCallBack() 异常回调
6. MV3D_LP_RegisterImageDataCallBack(cb, pUser) 注册图像数据回调（关键）
7. MV3D_LP_StartMeasure() 开始测量，回调持续触发
8. MV3D_LP_StopMeasure() 停止测量
9. MV3D_LP_CloseDevice() 关闭设备
10. MV3D_LP_Finalize() 释放运行环境

指南注解：图像数据回调函数内不建议再调用其他 SDK 接口，只用于取数据。

### 1.2 帧数据结构 MV3D_LP_IMAGE_DATA（只处理 PointCloud 类型）

- enImageType：ImageType_PointCloud=0x026000C0（ABC32f，每点 3×float32，12 字节/点）；
- nWidth/nHeight、pData/nDataLen（点数=nDataLen/12，可含空点）；
- bValid：丢包时 0，必须检查；
- pIntensityData/nIntensityDataLen：亮度图（Mono8），可选透传；
- fXScale/fYScale/fZScale、nXOffset/nYOffset/nZOffset：点云模式坐标已是真实 mm，仅透传；
- pExposureTimeStamp/pEncoder：每行曝光时间戳/编码器（个数=nHeight）。

### 1.3 图像模式与要点

- ImageMode_PointCloud_Image=4 相机直出点云（推荐）；Range_Image=7 深度图（需 MV3D_LP_MapDepthToPointCloud）；Origin_Image=1；
- 空点由 SDKEmptyPoint 控制（V1.3.2 支持无效点过滤），转换时 std::isfinite 过滤；
- 部署：Mv3dLp.dll（含依赖）在 C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64，需随 exe 分发。

## 2. 目标与范围

- 按帧类型自适应取流（点云 ABC32f / 深度图 C16 / 轮廓 ABC16），去掉 PLY 落盘、磁盘缓存、重新解析的中间步骤；
- 相机帧直接送入现有 PointCloudCanvas 实时渲染（新增轻量 setCloudStreaming）；
- `live_pipeline` 已接入 `CameraDialog` 生产实验链；默认链为 `camera.nextFrame → filter.voxel（可选） → canvas.publish`。真实相机验收仍未完成。
- 无相机环境用 mock 帧源跑通同一转换/渲染路径。

## 3. 架构

实时链路：
Mv3dLp 回调线程：回调到点 → 校验/必要的 SDK 权威转换 → 深拷贝 ABC32f → 完整扫描组帧 → LatestFrameSlot（latest-frame-wins）
GUI 线程：LatestFrameSlot 发布通知（QueuedConnection）→ take() → QtConcurrent 转换 ABC32f→Point3D → setCloudStreaming → 画布渲染 → 按 JSON pipeline 触发下游节点

- 新模块 05_camerainput（pcv_m05_camerainput）：Mv3dLpCamera（pimpl）+ LatestFrameSlot + convertPointCloudToPoints + MockFrameSource；
- 20_pointcloudrender：新增 setCloudStreaming（仅刷新点云+深度色域，保留选区/坐标系/轮廓覆盖层，不 resetView）；
- apps/pointcloudview：CameraDialog（枚举/连接/启停/帧统计，注入画布发布回调）+ live_pipeline（JSON pipeline 执行器，可单测）。

## 4. 实施与验证

- 模块/画布/pipeline/应用全部实现并编译通过（Debug，MSVC x64）；
- 单测：frame_converter_tests、frame_source_tests、live_pipeline_tests、pointcloud_canvas_contract_tests 全部 PASS；
- Mv3dLp.dll 部署验证通过（POST_BUILD 拷贝到 exe 目录）。

## 5. 风险与待办

- 真实相机连接、SDKCoordinateType 坐标系方向与项目 mm 约定比对（实机验收）；
- 回调线程关闭时序（closeEvent 前置 StopMeasure → CloseDevice → Finalize）实机验证；
- 130 万点/帧性能目标 15–30 FPS + 丢帧策略实测；
- 无相机环境测试仅覆盖 mock 路径。

## 6. 对齐 3DMVS 取流方式（2026-09）

- 3DMVS V3.1.4 实测（Mv3dLpLog）：真实相机 DP2240 取流 ImageMode=7（Range_Image 深度图）；虚拟相机（Vir6663020）走同一流程；连接阶段 SDK 内部设置取流缓存节点 100、重发包（SetResend 1/10/50）；
- 用户手册：缓存设置（取流/存图缓存节点防丢帧）、重发包设置、触发模式（自由采集/软触发/行触发）；
- 本实现对齐：默认 ImageMode=7；事件驱动取帧 + 转换线程化；异常事件 + 自动重连；软触发；虚拟相机识别；
- 限制：Mv3dLpApi.h 未公开 SetResend/SetImageNodeNum 接口（SDK 内部默认处理），重发包/缓存节点依赖 Mv3dLpCfg.ini 与 SDK 默认行为，未重复调用。
- 触发兼容：连续模式对 `TriggerMode` 做可选探测；DP2240 返回 `0x80060004 (MV3D_LP_E_PARAMETER)`/`MV3D_LP_E_SUPPORT` 时沿 3DMVS `SimpleView_Callback` 默认自由采集路径继续 `StartMeasure`，不猜测 `TriggerSelector`。软触发仍要求 `TriggerMode=1` 和设备支持的 `TriggerSource` 成功 Set/Get 回读，否则 fail-closed。

### 6.1 DP2240 完整扫描组帧

- DP2240/HFR 设备可能把一次 `Height` 扫描拆成多个 `nHeight` 分片；`nFrameNum` 是设备运行中的轮廓序号，应用开始取流时不保证从 0 开始。
- 首个分片作为序号锚点，后续要求 `nFrameNum == previous.nFrameNum + previous.nHeight`；累计行数达到设备 `Height` 后才发布 ABC32f 扫描。
- 跳行、重复、倒序、宽度变化、累计越界、非法长度或内存上限均丢弃当前扫描；实时画布和冻结流程不接收残缺分片。

### 6.2 DP2240 Height 目标选择（2026-09-03）

依据 3DMVS `BasicDemo_Callback`，`Height.nMax` 表示传感器图像缓冲区/完整扫描容量；HFR 设备的 `nCurValue` 可能只是当前传输批次高度。因此实现优先使用 `nMax`，仅在 `nMax <= 0` 时回退 `nCurValue`，并在日志中记录四项 Height 参数和选择来源。组帧器不根据回调高度动态猜测目标，任何高度不匹配都 fail-closed。

## 7. 实机诊断记录（2026-09：DP2240 无效帧）

- 现象：DP2240（MV-DP2240-01P）取流时对话框报"帧：无效"；
- 根因：SDK 日志确认设备为 HFR 线激光轮廓传感器，ImageMode=4（点云）下实际回传深度图帧（ImageType_Depth C16）——应用此前只实现 ABC32f 直读路径；
- 修复：转换器多帧类型分派；FrameSource::convertFrame 真机 SDK 转换（MapDepthToPointCloud/ImageConvert）；ImageMode 自适应；对话框取流模式选项 + 帧类型日志；
- 验证：单测增补并通过（17/17）；应用构建通过；实机复测待执行。
