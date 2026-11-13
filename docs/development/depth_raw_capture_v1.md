# 深度图取流与 RAW 导出 v1

日期：2026-10-30；owner：主线/Codex。SDK基线为本机1.3.4.2，V1.3.2 CHM只作API参考。

## 功能和文件

pointcloudview实验相机工作台新增`深度图取流`页，与原`相机采集`向导分开。页面枚举SDK设备，选择源码树外输出目录及可选厂商标定附件，开始后保留设备触发配置并逐完整SDK RangeImage自动保存；伪彩预览按最新帧更新，正式帧由有界`CaptureFrameQueue`独立接收。停止时停止相机、排空队列并join保存线程；溢出、丢帧边界、无效输入和I/O错误停止接收并显示首错。页面销毁会停止并等待。

会话目录名为`depth_capture_v1_<UTC时间戳>`，冲突时附加序号。`session.json`标记`pcv-depth-raw-session-v1`；每个`frame_*`中由ArtifactTransaction提交`depth.raw`、`depth.json`、真实转换辅助`conversion_intensity.mono8`以及SDK提供时的`exposure.i64`和`encoder.i32`。RAW为无头Little Endian有符号C16 row-major，header/offset为0。帧JSON保存宽高/stride、RAW字节数及SHA-256、帧号/时间字符串、SDK版本/转换配置/指纹、原始scale/offset、无效C16值-32768、SDK保留字节、设备型号/序列号及标定状态。

当前SDK回调不提供相机内部标定参数，状态记为`not_exposed`。用户可附带厂商文件；附件原样复制，记载文件名和SHA-256并标记`user_supplied_unverified`。它不能替代或自动关联未经确认的标定数据。现有手眼XML也不是相机内参。

`depth_raw_to_ply_v1 --input <frame/depth.json> --output <新建PLY路径>`读取并验证RAW和辅助亮度后，要求本机SDK版本、配置内容和指纹一致，再调用模块10既有`materializeCameraFrame()`并以事务写camera/mm PLY及row-major原始栅格下标`<PLY>.source_indices.u64`（Little Endian uint64）。该target在`PCV_BUILD_TOOLS=ON`且SDK可用时生成；拒绝覆盖输出。使用`scripts/build_windows.ps1 -BuildTools`可启用该构建选项；无SDK构建不生成该工具。

2026-10-30 Debug构建使用Qt6.8.3/MSVC19.51/NMake、ExperimentalCamera/Tests/Tools ON，点云GUI、测试目标、RAW转换工具全部构建成功。48项CTest中46项通过；`plane_mask_tests_v1`返回8（各向异性网格边界用例），`scan_geometry_view_tests_v2`在本机Qt平台环境失败。这两项不涉及本轮改动，需基线环境单独排查。新增RAW导出、旧深度会话、SDK启动替身和相机采集面板四项定向CTest均通过。pointcloudview `POINTCLOUDVIEW_SELFTEST_CLOSE=1`启动退出码0。Debug证据目录为`D:/pcv-build/depth-raw-v1-debug-20261030`。Release结果待本轮完成后追加。无SDK配置查询显式返回SDK不可用，点云数据读写与既有无SDK构建保持独立。

## 验证和边界

`depth_raw_export_tests_v1`检查纯C16逐字节写入、schema、零header、宽高、-32768约定、设备信息、SDK配置和强制辅助文件。既有`depth_capture_tests_v1`继续负责session-v3读写和SDK roundtrip。构建时应使用源码外全新Debug和Release目录，启用`PCV_BUILD_TESTS`、`PCV_BUILD_EXPERIMENTAL_CAMERA`和`PCV_BUILD_TOOLS`，分别运行完整CTest并验证新工具target。虚拟SDK端到端应覆盖连续多帧、排空、超载停止及离线转换。

本轮可自动验证项及命令/结果由交付报告记录；若没有虚拟相机可用，不把模拟帧测试描述成端到端SDK验证。真机门控/编码器、持续吞吐、物理坐标与精度、厂商标定文件匹配、其他SDK机器兼容性仍需现场验收。源码树外数据和构建目录不得放入仓库；无代码提交/push/上传。
