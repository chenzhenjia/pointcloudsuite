# 连续采集与位姿直拼 v1

本页保留固定行数初版行为与验证记录。新采集入口默认SDK完整图像/自动行数，当前操作见[连续采集v2](camera_capture_sdk_frame_v2.md)；固定行数仍可显式选择兼容模式。

日期：2026-10-16；owner：主线/Codex。主责10/30/50，应用入口为CameraDialog；选面/六分屏既有工作保留。

## 操作

连接相机，选择源码树外已存在目录并确认每次完整扫描行数，开始连续采集。SDK重新开始测量，现场必须保证首行对应Start、末行对应End，中间行沿行程近似均匀，回程/换位不产生扫描。

“停止并等待保存”后选择共用XML，逐帧填写Start/End六个数值，可预览、勾选、重排。至少两帧有效后“位姿拼接并发布主界面”，确认替换当前会话后后台事务输出，成功发布完整robot_base点云供后续处理。不执行ICP/seam/去重，不保证消除错位/双层。Mock仅在start产生一帧，测试可多次emitFrame；不能替代真机验证。

## 数据与生命周期

- 10的LatestFrameSlot额外挂载CaptureFrameQueue，在预览覆盖前接收完整标准ABC32f；最多2个待处理帧、合计512MiB，正在保存的一帧另占内存。后台串行转换/事务保存；展示最多300000实测点不影响完整保存。
- 固定行数仅用于显式采集，普通取流保持自适应。分片不连续、行数冲突、断线、队列满和保存错误均锁定首错并停止。先静止SDK回调，再排空/join；尾部不完整扫描报警，保存失败后的剩余帧不声称已保存。记录线程无设备/GUI引用，断线不续接原会话。
- capture_v1_UTC目录冲突追加序号；session.json记录pcv-camera-session-v1、行数、设备。每帧frame_00000001事务保存camera.ply、raster_indices.bin、frame.json，无frame.json目录视为未提交。
- camera.ply保存有效原相机XYZ，过滤非有限值但保留原始row-major下标。raster_indices.bin固定LE：uint32 magic=0x31524943、uint32 version=1、uint64 count、count个uint64下标，严格递增且小于width*height。frame.json记录尺寸、有效/原始数量、稳定ID与SDK号/时间；严格组帧保留最后分片SDK帧号。
- parameters.json保存有序ID、勾选和位姿文本；可读XML复制到handeye.xml并采用相对引用，整体目录可搬迁。更新仅事务覆盖本会话生成文件，不覆盖外部输入。
- 50显式useRasterRowProgress，ratio=(sourceIndex/width)/(height-1)，同一行相同；刚体输入仍[X,0,Z]，原Y留存但不参与新路径进度。输出来源是原栅格下标。旧Y/signedTravel默认不变。
- 30的StitchingFrameInput.captureFrameDirectory选择采集输入，拒绝ICP/seam，经50转换一次后复用现有四件套和schema，diagnostics标记pose_only/not_run；每次pose_stitch_v1_UTC独立目录。cloudId遵循确认后的列表顺序。
- 应用复用stitchingSucceeded更新CPU缓存、完整画布、来源并失效旧衍生数据。发布前停止预览，失败/取消保留旧处理会话。所选平面与独立Stitch默认不变。

## 验证与风险

capture_pose_tests_v1覆盖队列/预览分离、过载、组帧、断线、索引、保存失败、回放、反向倾斜转换、串行/1/2/4及取消；camera_capture_panel_tests_v1覆盖Mock两帧、填写/重排、发布、取消及重复会话。全新源码外Debug/Release开启-ExperimentalCamera，完整构建/CTest和定向until-fail:100；另验证相机关闭组合。最终实际结果见[本轮日志](../changelog/v0.5.md#camera-capture-pose-v1)。

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -ExperimentalCamera -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-pose-debug-v1 -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
.\scripts\build_windows.ps1 -Config Release -BuildTests -ExperimentalCamera -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-pose-release-v1 -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir C:\Qt\6.8.3\msvc2022_64 -BuildDir C:\pcv-build\capture-pose-nocamera-debug-v1 -CMakePath C:\Qt\Tools\CMake_64\bin\cmake.exe -Generator 'NMake Makefiles'
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-pose-debug-v1 -C Debug --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-pose-release-v1 -C Release --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-pose-nocamera-debug-v1 -C Debug --output-on-failure
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir C:\pcv-build\capture-pose-debug-v1 -C Debug -R "capture_pose_tests_v1|camera_capture_panel_tests_v1|handeye_transform_tests|frame_source_tests|stitching_interface_tests" --repeat until-fail:100 --output-on-failure
```

本轮脚本配置开启两个GUI及Tests，CLI/Tools关闭；“全量”指这三组实际配置的全部target和CTest，不代表所有CMake开关组合。新程序为`C:/pcv-build/capture-pose-release-v1/pointcloudview.exe`，旧构建目录/EXE未替换。启动冒烟使用各构建目录的`smoke-config-v1.json`和`PCV_CONFIG_FILE`隔离运行数据；`PCV_DATA_DIR`不是本项目GUI配置入口。实际使用时不要设置`POINTCLOUDVIEW_SELFTEST_CLOSE`。

真机至少三次扫描核对固定行数与物理边界、首末位姿、正反行程、换位触发、保存回放及主界面后续处理。Start/End不能补偿变速/触发延迟或错误手眼；真机、大帧吞吐/峰值内存、现场几何及干净机部署单独验收。

回滚只撤回本任务capture_session_v1、camera_capture_panel_v1及10/30/50/应用/CMake接入增量，不整体覆盖任何已有修改。无Git提交/推送/上传。
