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

已有 windows-msvc-debug/release、build-debug/release、test-debug preset；Debug preset 开Tests、关闭Tools，Release默认不开Tests，两个preset的Tools默认均为OFF。环境需自行提供Qt/MSVC与可靠生成器。Qt Creator打开根CMake，选MSVC x64 Kit，使用新的源码外目录并重新配置；缓存中关闭的target不因普通编译自动重开。

## 实际开关与组合风险

`PCV_BUILD_POINTCLOUDVIEW`、`PCV_BUILD_SCAN_GEOMETRY_DEMO_V1`默认ON，CLI/TOOLS/TESTS默认OFF，EXPERIMENTAL_CAMERA根默认ON。脚本默认关闭相机，`-ExperimentalCamera`才开启。SDK真机编译另取决于头/库可用性。独立拼接/亮度应用及旧拼接构建开关已移除；[清理清单与配置矩阵](application_target_cleanup_v1.md)。

根CMake按两个GUI开关控制入口；20随任一GUI构建；30随任一GUI、Tools或Tests构建，40聚合只在30可用时注册。两个GUI共用真实选面任务，整帧共享窗口只随pointcloudview构建。基础模块始终需要Qt Core/Gui，Tools也查找Concurrent；CLI-only不查找Widgets/OpenGL。组合已静态检查，仍需用户按矩阵实际配置与构建，不能沿用历史通过记录。

pointcloudcli target本身只链接Core/Gui/Concurrent、10/50/60，不链接Widgets/OpenGL。脚本`-CliOnly`明确关闭pointcloudview与scan_geometry_demo_v1、开启CLI，与`-ExperimentalCamera`互斥；本轮未运行配置/构建。

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
cmake -S . -B <ToolsBuildDir> -DPCV_BUILD_POINTCLOUDVIEW=OFF -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=OFF -DPCV_BUILD_TOOLS=ON
cmake --build <ToolsBuildDir> --config Debug --target registration_diagnostic
```

上述为后续命令，不宣称已运行或通过。启用CLI与Tests时，15项cli契约在应用CMake注册；全量数量还随其他开关变化，文档不固定当前总测试数。

## 头文件依赖与重建

推荐 NMake Makefiles，脚本拒绝 JOM；根 CMake 对 JOM 给提示，不把根提示误作致命检查。VSLANG=1033 与编译 launcher 保持MSVC /showIncludes英文；脚本检查头文件依赖数据库，ABI探测/64位护栏保留。Ninja依赖扫描也需可靠数据库，不因一次单元测试通过而忽略空依赖问题。

公共结构体变更必须可靠依赖重编或-Clean/重新构建，避免新旧对象布局混链。旧本地化前缀/空.obj.d问题见[历史归档](../archive/history_pre_v0.5.md)。

## 启动与便携部署

pointcloudview自关闭变量为`POINTCLOUDVIEW_SELFTEST_CLOSE=1`；新GUI为`PCV_SCAN_GEOMETRY_SELFTEST_CLOSE=1`。使用匹配Qt bin/plugins/platforms，启动成功不能代替真实OpenGL/数据验收，offscreen只能覆盖相应headless契约。

-Deploy 仅接受 -CliOnly 且 Release，调用 windeployqt/编译器运行库检查，生成exe、Qt/VC DLL、data、README.txt，并清空PATH执行CLI契约。目标目录必须源码外且非盘符根；脚本会清空已有部署目录，运行前须明确授权准确目录。本次不执行部署。

```powershell
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

历史 Release 缺本机Visual Studio redistributable，便携部署未验收；本机启动不代表干净机器可运行。CLI运行目录/退出码见[数据契约](../contracts/data_contracts_v0.5.md)与[CLI文档](../modules/pointcloudcli.md)。

## 2026-10-16 构建与运行复验

用户本轮明确要求实际修复/构建/运行，本文开头“不执行”仅是文档整理轮历史。Qt6.8.3/MSVC19.51/NMake、两个GUI/CLI/Tools/Tests开启、相机关闭的全新Debug/Release全量构建均成功，各43/43 CTest及Windows原生六画布/两个GUI启动均通过。旧build目录未覆盖；新EXE路径、工具链环境、完整命令、日志及未验证项见[修复说明](build_runtime_repair_v1.md)。Release redistributable定位告警仍在，干净机便携部署未验收。

## STEP 可选依赖（2026-10-23）

新增 `-StepImport -OcctDir <OpenCASCADEConfig目录>` 对应 `PCV_BUILD_STEP_IMPORT=ON`，严格使用OCCT7.9.3/x64动态库。默认OFF不需要OCCT，CAD模板包读写仍可用；ON缺依赖配置报错。Debug/Release用各自运行库，生成目录/依赖均在源码外。具体命令、运行库与验证见[STEP说明](step_cad_template_v1.md)。

## 2026-10-26 离线所选平面 GUI v2 构建入口

`scan_geometry_demo_v1` 保留 legacy target/开关名，当前源码已为真实离线 PLY 所选平面流程。根共享 UI 条件包括 `PCV_BUILD_SCAN_GEOMETRY_DEMO_V1`，20 随 View 或该 GUI 创建，30 随 View/Stitch/Tools/该 GUI 创建；此前“20只随View、30只随Stitch/Tools”的描述为旧状态。新增共享应用任务库 `pcv_selected_plane_tasks_v2` 依赖10/30/50/60，新 GUI 单独启用时应获得必要依赖，仍需用户实际配置验证。

按用户安排本轮未配置、构建、CTest或运行，不生成新版 EXE；已有 v1 EXE 不代表新源码行为。使用全新源码外 Debug/Release 目录、Qt6.8.3/MSVC/NMake，完整命令、独立GUI组合和用户验收清单见 [v2 开发说明](scan_geometry_real_selected_plane_v2.md)。不要用本节追加说明覆盖历史实际运行记录。


## STEP 模板与自动匹配 v1

2026-11-05 STEP模板匹配增加模块40 STATIC实现，新GUI也调用既有OCCT7.9.3部署函数；公共头变化请全新源码树外全量构建。STEP关闭仍可模板回读/匹配。 [开发与验收](step_template_matching_v1.md)。本轮未执行构建、CTest、GUI/DPI或真实样本验证，由用户执行。
