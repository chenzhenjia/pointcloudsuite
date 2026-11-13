# 构建错误修复与运行验证 v1

日期：2026-10-16；owner：主线/Codex。用户本轮明确要求实际修复并构建、测试和运行；先前整合 v1/v2 的“仅静态交付”仅是历史记录，不再是本轮验证安排。

## 根因与修复范围

- 模块 60 的 `preparePlaneSelectionIndex()` 在同一 `try` 作用域先后定义 `int count` 和 `size_t count`，导致 MSVC C2373；第173/182行 C2737 为连带错误。分别改为 `validSampleCount` / `bucketCount`，对应引用一并更新，类型、遍历顺序和算法条件不变。
- 原生主窗口可启动，但 `startup.log` 报七个 `:/pointcloudview/icons/*.svg` 不存在。原因是把 `.qrc` 列入 target 不等于启用资源编译。为 `pointcloudview` 及同样包含该 QRC 的两个主窗口测试 target 显式开启 `AUTORCC`；不修改资源路径或图标本身。
- `workpiece_template_view_tests_v1` 逐项验证七个资源存在且能解码为非空 QPixmap，防止资源未链接但窗口测试仍通过。
- `selected_plane_dialog_tests_v1 --native-gl` 追加可选原生回归：与应用一致的 desktop OpenGL 3.3、六画布实际绘制/完整下标拾取/候选高亮、同一画布放大恢复、跳过确认和完整结果绘制。常规 CTest 仍使用 offscreen 会话测试；原生模式单独运行，不将无 OpenGL 的机器误报为通过。

本轮不改变公共 API、容差、跨带/迭代、坐标数学、模板 schema、PLY/PCVMAP 或模块 30 配准。保留所有任务前未提交/未跟踪修改，不修改原始数据，不提交/推送。只对本轮增量做必要修复，不整体回退之前的六分屏实现。

## 工具链与构建

- Qt：`C:/Qt/6.8.3/msvc2022_64`。
- MSVC：19.51.36252，x64；环境脚本：`C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat`。
- CMake/CTest：`C:/Qt/Tools/CMake_64/bin`；生成器 `NMake Makefiles`；保留 `VSLANG=1033` 及现有编译依赖护栏。
- 全新源码树外目录：`D:/pcv-build/build-repair-v1-debug-20261016` 和 `D:/pcv-build/build-repair-v1-release-20261016`。
- 两种配置均开启 `PCV_BUILD_POINTCLOUDVIEW/POINTCLOUDSTITCH/CLI/TOOLS/TESTS`，关闭 `PCV_BUILD_EXPERIMENTAL_CAMERA`。不是通过关闭 GUI 或测试绕过错误。

在导入上述 MSVC x64 环境后，配置/执行方式如下；Release 使用单独目录及对应 `CMAKE_BUILD_TYPE`：

```powershell
$pcvRepairDir = 'D:/pcv-build/build-repair-v1-debug-20261016'
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' -S 'D:/workpiece/pointcloudview/pointcloudview' -B $pcvRepairDir -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_POINTCLOUDVIEW=ON -DPCV_BUILD_POINTCLOUDSTITCH=ON -DPCV_BUILD_CLI=ON -DPCV_BUILD_TOOLS=ON -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --build $pcvRepairDir
$env:PATH = 'C:/Qt/6.8.3/msvc2022_64/bin;' + $env:PATH
& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir $pcvRepairDir --output-on-failure
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_FORCE_STDERR_LOGGING = '1'
& "$pcvRepairDir/selected_plane_dialog_tests_v1.exe" --native-gl
```

## 本轮实际结果

| verification | Debug | Release |
|---|---|---|
| 全量构建（两个 GUI、CLI、Tools、Tests） | 成功 | 成功 |
| 最终全量 CTest | 43/43，43.09 s | 43/43，7.37 s |
| Windows 原生六画布/拾取/完整结果回归 | exit=0 | exit=0 |
| pointcloudview 原生启动/正常自关闭 | exit=0 | exit=0 |
| pointcloudstitch 原生启动/正常自关闭 | exit=0 | exit=0 |

上述时间是本轮测试套件墙钟时间，不是生产处理性能。初次全量构建和初次 CTest 通过后，资源修复及新增断言重新构建并重跑全量套件，表中记录最终结果。

全量套件包含读取/缓存、转换、单击 v1/v2、串行与1/2/4 worker/预算回退及额外 Qt 共享副本、拼接、模板接口/主窗口读取、六分屏会话/取消/revision/跳过/跨轮等回归。临时 PLY 夹具在源码树外生成；两帧625点的混合单击/三点会话完整输出1250点，校验 PCVMAP 顺序/来源，并做模板版本、搬迁和删除临时输入后的完整往返。此夹具不代表真实工件精度验收。

原生六画布回归加载六帧各625点，等待每格实际上传/绘制完成后，通过鼠标事件与 GPU 拾取通道验证中心点312及各格完整625点候选；等待实际高亮后检查放大恢复/跳过提交。另显示两帧拼接的完整1250点结果。日志确认 NVIDIA GeForce RTX 4060 Laptop GPU、OpenGL 3.3.0 NVIDIA 592.82。主窗口另经 computer-use 读取原生窗口并正常关闭；最终启动日志无缺失图标告警。

## 产物、日志与运行方式

每个构建目录均包含：

- `pointcloudview.exe`、`pointcloudstitch.exe`，以及本次构建部署的匹配 Qt DLL / `platforms/qwindows[d].dll`。
- `build.log`（首次全量）、`build-final.log`（资源/回归修复后全量）、`ctest-final.log`、`native-six-canvas.log`。
- `pointcloudview-smoke-final.*.log`、`pointcloudstitch-smoke-final.*.log`；View 的 OpenGL 证据在 `runtime-smoke-final/logs/startup.log`。
- `smoke-final-config.json` 指向该目录内源码树外运行数据；Debug 目录另有四次启动退出码汇总 `smoke-results.json`。

推荐直接启动新的 Release `D:/pcv-build/build-repair-v1-release-20261016/pointcloudview.exe`。需要使用上述隔离运行目录时，在启动该进程的终端设置 `PCV_CONFIG_FILE` 为同目录 `smoke-final-config.json`；不设置自关闭变量即可正常使用。`PCV_DATA_DIR` 不是现有 GUI 的配置入口，不能用它替代 `PCV_CONFIG_FILE`。冒烟使用 `POINTCLOUDVIEW_SELFTEST_CLOSE=1` / `POINTCLOUDSTITCH_SELFTEST_CLOSE=1`，实际使用前应取消这些变量。

旧 Qt Creator/build 目录和旧 EXE 未被覆盖；本轮可运行结果位于以上两个明确目录，不应把旧路径的程序当作新版本。公共结构体已有前序修改，其他构建目录也应全量重建。

## 风险与回滚点

- Release `windeployqt` 仍提示本机指定 VS redistributable 目录缺失；本机两个入口实际运行成功，但未宣称不带开发环境的干净 Windows 机器便携部署通过。
- 实验相机/SDK/机器人、其他独立 CMake 开关组合、真实六大帧的峰值内存、几何质量和处理速度未验收；不承诺固定秒数、倍数或识别率。
- 静态差异/UTF-8/文档本地链接检查见本轮 changelog。回滚仅人工反向撤回变量命名、AUTORCC及本轮测试/文档增量；不得整体恢复文件或撤回此前完整点云/六分屏功能。

## 用户确认与收敛

用户随后反馈“测试完成，报错结束功能正常，收敛”。本轮停止功能扩展及追加测试，仅完成最终静态检查和记录；保留上述自动验证范围与未验收边界，不把未提供的真实样本/性能明细写成通过结论。
