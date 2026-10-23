# 连续采集亮度图保存与纹理映射解耦 v2

日期：2026-10-23；owner：主线/Codex。v2 调整连续采集的默认行为；v1 的亮度数据来源、逐帧格式和外置光源操作提示仍有效。

## 默认流程

连接 → 设置/预检 → 同步采集点云和亮度图 → 停止并排空/join → 确认位姿 → 拼接并发布几何点云。

“同步采集2D亮度图”默认开启，允许关闭；开启时逐帧保存 `intensity.png` 和 `intensity.mono8`，保留尺寸、SDK 帧号、统计及 2D 预览，不拼接 2D 图像。第四步新增默认关闭的“生成纹理点云”，仅显式开启时执行原有灰度映射和 sidecar 输出。每次新采集重置纹理选项为关闭。

开启亮度采集时，SDK 缺图、尺寸或载荷无效仍锁定首错、停止软件接收、排空完整帧并等待后台线程；保留已保存帧。程序不控制门控、机器人或外置光源；外置光源仍由操作员确认开灯稳定、结束扫描后按提示关灯。

## 读取、校验与发布

- `readCapturedFrame(directory, cancelled, mode)` 新增 `CaptureFrameReadMode`：默认 `WithIntensity` 保留旧调用行为；`GeometryOnly` 严格校验几何元数据、PLY、原栅格来源映射及取消，但不读取或校验亮度载荷。
- 模块 30 的采集输入、向导几何预览使用 `GeometryOnly`。位姿/几何状态与亮度图状态分列；一键校验及开始拼接前刷新选中帧的磁盘状态。点云或来源映射错误始终阻断拼接。
- 默认模式下，事后亮度文件丢失或损坏只产生警告，有效几何帧仍可拼接。开启纹理时必须校验所有选中帧亮度，失败定位对应行并阻止发布，不静默降级。
- 正常几何发布不调用模块 60 映射，不生成 `texture_manifest.json`、`stitched_robot_base.intensity.bin` 或 valid sidecar。已有历史文件不删除。
- `StitchingPublication::intensitySourceDirectory` 传递逐帧目录供工作台状态栏显示；复用原发布/revision 生命周期，新点云清除旧灰度覆盖。可选纹理发布后切换“纹理灰度”显示，普通发布恢复几何显示。
- `parameters.json` 增加可选 `texture_mapping_enabled`，缺省为 false；重新打开会话时恢复显式选项，旧会话不会因 `intensity_enabled=true` 自动生成纹理。采集亮度配置在启动时快照保存；之后修改下轮采集选项不改写本轮保存事实。

## 兼容与回滚

不升级 session/frame/parameters schema 或 PLY 结构，不改变点数、坐标、位姿和拼接算法。保留高级实时预览、固定行数、旧无图/带图会话、模块 60 与其他独立纹理入口。公共头有新增枚举/参数和应用 DTO 字段，其他构建目录应完整重建。

保留任务开始时的工序 JSON 等未提交修改。回滚只人工撤回本轮读取模式、向导开关、状态展示、发布字段及回归/文档增量；不整体恢复文件、不删除会话或旧产物、不提交/推送/上传。

## 验证记录与现场边界

本轮使用源码树外 `D:/pcv-build/camera-intensity-v2-debug-20261023` 和 `D:/pcv-build/camera-intensity-v2-release-20261023`，两个 GUI、Tests 和 ExperimentalCamera 开启，CLI/Tools 关闭；全量指这两种实际配置的所有 target，不代表其他 CMake 开关组合。Qt 6.8.3、MSVC 19.51、CMake 3.30.5、NMake Makefiles；复用 `scripts/build_windows.ps1` 的依赖跟踪护栏，原构建目录及 EXE 未覆盖。

| verification | Debug | Release |
|---|---|---|
| 全新完整构建及最后增量的全目标重编 | 通过 | 通过 |
| 最终代码全量 CTest | 37/37，45.35 s | 37/37，11.76 s |
| pointcloudview 原生启动/自关闭 | exit 0 | exit 0 |
| pointcloudstitch 原生启动/自关闭 | exit 0 | exit 0 |

没有失败测试；表中耗时只代表本轮套件墙钟时间，不是现场性能。最终全量 CTest 已包含最后 UI/测试调整。新 EXE 位于各目录根，Release 为 `D:/pcv-build/camera-intensity-v2-release-20261023/pointcloudview.exe`。

日志：各目录 `build.log`、`build-final.log`、`ctest-final.log`、`smoke-results.json` 和两个入口的 `*-smoke.stdout.log`/`*-smoke.stderr.log`。冒烟使用 `PCV_CONFIG_FILE=各目录/smoke-config.json`，指向源码外 `runtime-smoke`；View 的 `runtime-smoke/logs/startup.log` 确认 RTX 4060/OpenGL 3.3。只启动/退出，未操作真机。Release 的 `windeployqt` 仍警告本机 VS redistributable 目录缺失，本机启动成功不等于干净机部署通过。

复现构建时分别使用对应目录和 `-Config Debug/Release`，示例：

```powershell
./scripts/build_windows.ps1 -Config Debug -BuildTests -ExperimentalCamera -Generator 'NMake Makefiles' -QtDir 'C:/Qt/6.8.3/msvc2022_64' -BuildDir 'D:/pcv-build/camera-intensity-v2-debug-20261023' -CMakePath 'C:/Qt/Tools/CMake_64/bin/cmake.exe'
& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir 'D:/pcv-build/camera-intensity-v2-debug-20261023' --output-on-failure
```

启动冒烟使用 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 和 Stitch 的 `--selftest-close`；正常使用新程序时不设置自关闭变量。

收尾时工作区出现并行 CAD 模板/CMake 等后续修改，本任务未改动这些内容。以上构建和测试对应本任务最终代码及当时工作区，不代表对随后并行修改完成了整库重验；本轮 EXE 和日志仍保留在上述独立构建目录。

回归覆盖默认保存但无纹理输出、显式纹理、坏 PNG/缺失 Mono8 的几何读取与严格读取差异、几何映射损坏、缺图停止/首错/排空、无亮度采集、旧参数默认关闭/新参数恢复、采集配置快照、取消/revision、旧灰度清除和高级预览兼容。

无真实相机和外置光源验收证据。现场需要亮度开启/关闭和外置光源两种提示流程；亮度开启至少三次扫描，核对 `StartMeasure succeeded`、清缓存成功、逐帧 SDK 号/尺寸及 PNG/Mono8 可读性和接收/保存数量。真实灰度外观、持续吞吐/峰值内存、物理同步和干净 Windows 部署必须独立验收；Mock/SDK 替身和自关闭启动不能代替这些证据。
