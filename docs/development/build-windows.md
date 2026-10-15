# Windows 构建与验证

文档 v0.5，核对日期 2026-10-15。本轮不执行本文任何构建、CTest、GUI 或部署命令；历史结果见[版本日志](../changelog/v0.5.md)。源码树外目录必须可写，缓存/日志/导出也不得进源码。

## 环境与推荐命令

Windows10/11、MSVC x64/C++17、Qt6.5+（Core/Gui/Widgets/Concurrent/OpenGL/OpenGLWidgets）、根 CMake3.19+。CMakePresets.json 使用 version6，因此 preset 命令须 CMake3.25+；两种最低要求不要混称。当前历史机器 Qt6.8.3/MSVC19.51/NMake，不构成所有工具链承诺。

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

已有 windows-msvc-debug/release、build-debug/release、test-debug preset；Debug preset 开 Tests/Tools，Release 默认不开 Tests。环境需自行提供 Qt/MSVC 与可靠生成器。Qt Creator 打开根 CMake，选 MSVC x64 Kit，使用新的源码外目录并重新配置；缓存中关闭的 target 不因普通编译自动重开。

## 实际开关与组合风险

PCV_BUILD_POINTCLOUDVIEW/POINTCLOUDSTITCH 默认 ON，CLI/TOOLS/TESTS 默认 OFF，EXPERIMENTAL_CAMERA 根默认 ON。脚本默认关闭相机，-ExperimentalCamera 才开启。SDK 真机编译另取决于头/库可用性。

根 CMake 的 GUI 开关仍控制目录；“固定入口”注释不表示忽略 OFF。共享 UI 在任一 GUI 开启时加入；20 只随 View，30 当前只随 Stitch OR Tools。View/共享 UI 和某些无条件测试还依赖30，CLI-only/测试/单GUI等组合需独立配置验证，不能把旧记录的成功当作当前组合保证。[P0任务](development_plan_v0.5.md)。

pointcloudcli target 本身只链接 Core/Gui/Concurrent、10/50/60，不链接 Widgets/OpenGL。脚本 -CliOnly 会关闭两个 GUI、开启 CLI，与 -ExperimentalCamera 互斥；这是脚本意图，本轮未重新验证所有根/兼容 target 组合。

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
cmake -S . -B <ToolsBuildDir> -DPCV_BUILD_TOOLS=ON
cmake --build <ToolsBuildDir> --config Debug --target registration_diagnostic
```

上述为后续命令，不宣称已运行或通过。启用CLI与Tests时，15项cli契约在应用CMake注册；全量数量还随其他开关变化，文档不固定当前总测试数。

## 头文件依赖与重建

推荐 NMake Makefiles，脚本拒绝 JOM；根 CMake 对 JOM 给提示，不把根提示误作致命检查。VSLANG=1033 与编译 launcher 保持MSVC /showIncludes英文；脚本检查头文件依赖数据库，ABI探测/64位护栏保留。Ninja依赖扫描也需可靠数据库，不因一次单元测试通过而忽略空依赖问题。

公共结构体变更必须可靠依赖重编或-Clean/重新构建，避免新旧对象布局混链。旧本地化前缀/空.obj.d问题见[历史归档](../archive/history_pre_v0.5.md)。

## 启动与便携部署

pointcloudview 自关闭变量为 POINTCLOUDVIEW_SELFTEST_CLOSE=1；pointcloudstitch 参数 --selftest-close。使用匹配 Qt bin/plugins/platforms，启动成功不能代替真实 OpenGL/数据验收，offscreen只能覆盖相应headless契约。

-Deploy 仅接受 -CliOnly 且 Release，调用 windeployqt/编译器运行库检查，生成exe、Qt/VC DLL、data、README.txt，并清空PATH执行CLI契约。目标目录必须源码外且非盘符根；脚本会清空已有部署目录，运行前须明确授权准确目录。本次不执行部署。

```powershell
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

历史 Release 缺本机Visual Studio redistributable，便携部署未验收；本机启动不代表干净机器可运行。CLI运行目录/退出码见[数据契约](../contracts/data_contracts_v0.5.md)与[CLI文档](../modules/pointcloudcli.md)。
