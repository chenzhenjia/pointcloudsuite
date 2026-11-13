# 相机取流启动顺序修复 v1

2026-10-16；owner：主线/Codex；主责模块10，必要测试运行环境修复；用户授权实施、构建及测试。

## 原因与现场证据

本机SDK 1.3.4.2、相机MV-DP2240-01P。2026-10-16 15:51:45与15:52:05成功打开设备并读回ImageMode=7，随后在StartMeasure前调用ClearDataBuffer，SDK返回0x80060003（MV3D_LP_E_CALLORDER），程序关闭设备，应用记录received=0/saved=0。更早打开失败为0x8006000E（MV3D_LP_E_ACCESS_DENIED），具体占用者未确认，最新两次已成功打开。触发节点读回0x80060004只作诊断，不是该两次中止原因。

## 启动与失败处理

SDK整图路径：OpenDeviceBySN → ImageMode设置/读回与原有配置 → 注册图像/异常回调 → StartMeasure → ClearDataBuffer → 开放图像接收 → start返回成功后应用显示取流中。保留当前门控无效及一次门控对应一整图的人工预检，不写入触发配置、不自动发软触发。

CallbackContext新增内部imagesReady原子门禁，仅阻挡启动期间图像，不屏蔽设备异常。清缓存完成前图像既不转换也不发布至最新帧槽/保存队列；预览与固定行数仍在原有时机接收图像，不调用新增清缓存步骤。StartMeasure成功后立即标记running，后续清缓存失败复用shutdown：关闭图像门禁、等待已进入回调、停止已启动测量、关闭设备和归还SDK引用。清缓存失败不忽略，不降级Mock；保留首错及既有成功保存帧。

打开、两类回调注册、测量启动、清缓存统一记录阶段与十六进制状态。错误信息增加CALLORDER和ACCESS_DENIED名称与中文处理提示。FrameSource公共接口、会话schema、坐标/拼接数学及输出不变。

## 回归与验证

新增mv3d_startup_tests_v1直接编译生产mv3d_lp_camera.cpp，使用真实SDK头声明、本地可控SDK函数替身，不连接硬件、不加载SDK DLL。替身复现未测量时清缓存返回CALLORDER；检查实际调用顺序、启动三阶段旧图像门禁、清缓存后完整帧/队列发布、打开/配置/注册/启动/清缓存失败与重试、反复启停、清缓存期异常仍上报、固定模式兼容和关闭等待in-flight回调。

测试CMake修正ENVIRONMENT内PATH分号转义，并在相机开启时优先加入已安装的两个x64 SDK运行库目录。此前未转义PATH被拆成环境变量条目，可导致UI测试找不到SDK依赖；不修改系统PATH或产品部署。

本轮实际工具链Qt6.8.3/MSVC19.51.36252.0/CMake3.30.5/NMake Makefiles，两个GUI、CLI、Tools、Tests及ExperimentalCamera均开启。全新源码树外Debug/Release完整构建成功，最终补齐夹具后全target增量构建再次成功；CTest最终分别52/52（42.45s）与52/52（8.53s），均exit0。套件墙钟不代表实际扫描性能。

首轮全量各51/52，新增替身的旧模式兼容测试缺少原有组帧器要求的亮度栅格；只补齐测试夹具后定向及最终全量通过，未修改组帧器或生产算法。历史UI测试的启动依赖问题通过本轮测试PATH修正后，Debug/Release相机UI回归均实际通过；不回写历史失败为通过。

两个配置的pointcloudview（POINTCLOUDVIEW_SELFTEST_CLOSE=1）与pointcloudstitch（--selftest-close）原生启动退出共4次exit0，使用各目录smoke-config.json与runtime-smoke隔离数据。View日志确认RTX4060/OpenGL3.3。Release另外以原系统PATH启动exit0，未修改系统环境；冒烟未连接相机。dumpbin依赖检查确认替身测试不导入Mv3dLp.dll。Release windeployqt仍提示本机VS release redistributable目录缺失，干净机部署未验收。

构建目录与日志：

- `D:/pcv-build/camera-startup-fix-v1-debug-20261016`
- `D:/pcv-build/camera-startup-fix-v1-release-20261016`
- 每目录`configure.log`、`build.log`、`build-verified.log`、`ctest-verified.log`；首轮失败保留于`ctest-final.log`。原生冒烟为`view-smoke.*.log`、`stitch-smoke.*.log`、`runtime-smoke/logs/startup.log`，汇总为Debug目录`smoke-results.json`。

实际配置/验证方式（预先加载vcvars64.bat并设置VSLANG=1033）：

```powershell
# 两配置分别使用上述独立目录，CMAKE_BUILD_TYPE对应Debug/Release。
cmake -S . -B <BuildDir> -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=<Config> -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_TESTS=ON -DPCV_BUILD_CLI=ON -DPCV_BUILD_TOOLS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=ON
cmake --build <BuildDir>
ctest --test-dir <BuildDir> --output-on-failure
```

新Release程序为`D:/pcv-build/camera-startup-fix-v1-release-20261016/pointcloudview.exe`。用户当前Qt Creator旧程序未覆盖或关闭；需主动退出旧程序，使用新程序进行真机验收。运行日志/数据在源码树外，已有会话保留。

交付静态检查：git diff --check、8份本轮文件严格UTF-8及4份相关文档50处本地链接检查通过。工作区54个已跟踪M、21个未跟踪文件；任务开始54个M/19个未跟踪全部保留，本轮新增替身回归与开发说明，无删除、暂存、提交、推送或上传。

## 真机验收与边界

1. 在其他客户端主动断开该相机，确保单一客户端控制；不强制结束未知进程。
2. 门控无效时完成预检，启动后依次检查StartMeasure succeeded、ClearDataBuffer状态0和SDK image reception ready。
3. 再开始现场扫描，至少三次完整扫描，核对每幅实际宽高/行数、接收数与保存数，停止后等待排空并检查会话。
4. 若StartMeasure后ClearDataBuffer仍失败，保留SDK与应用日志，停止验收；不忽略错误、不用超时猜整图、不控制机器人。

本地替身与启动冒烟不能证明真机清缓存语义、网络丢包、门控边界及生产吞吐。真机三次扫描需现场操作，未实际完成不得记为通过。干净Windows部署仍须独立验收。

回滚仅人工反向撤回本轮驱动启动顺序/诊断、回归和测试环境/文档增量；保留全部既有用户工作、原始会话、相机向导及SDK整图格式，不整体还原任何文件。不提交/推送/上传。
