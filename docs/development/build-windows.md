# Windows 构建

## 环境要求

- Windows 10/11；
- MSVC x64，支持 C++17；
- CMake 3.19+；
- Qt 6.5+，包含 Core、Gui、Widgets、Concurrent、OpenGL 和 OpenGLWidgets；
- 源码、构建、Qt、输入和输出路径支持 Unicode；推荐用含中文目录的构建目录执行一次验证。

## 推荐命令

在 Developer PowerShell 中执行：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

也可以使用：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

## Qt Creator 直接运行 View 和 Stitch

根 `CMakeLists.txt` 固定接入 `pointcloudview` 与 `pointcloudstitch`，因此
Qt Creator 完成一次 CMake 重新配置后，项目树会显示 `apps/` 和两个可执行
target；可在项目栏右键构建/运行，或从右上角运行目标列表选择它们。

旧的 `build\\Desktop_Qt_6_8_3_MSVC2022_64bit_Debug` 配置曾把两个 target
缓存为 `OFF`，不能仅靠重新编译恢复。请在 Qt Creator 中关闭项目、移除该旧
构建配置或创建新的源码树外构建目录，再打开根 `CMakeLists.txt`，选择
`Desktop Qt 6.8.3 MSVC2022 64bit` Kit 并执行 **CMake Reconfigure**。构建时
选择 `NMake Makefiles`，不要使用 `NMake Makefiles JOM`。

启动自测可使用：

```powershell
# pointcloudstitch 的运行参数
--selftest-close

# pointcloudview 的运行环境变量
POINTCLOUDVIEW_SELFTEST_CLOSE=1
```

## CLI 骨架构建

`pointcloudcli` 默认关闭，打开 `PCV_BUILD_CLI` 后可与两个桌面应用在同一构建目录中编译。CLI target 仅依赖 Core/Gui/Concurrent 和业务模块，不链接 Widgets、OpenGL、PointCloudCanvas 或 GUI application target。当前入口包括 `inspect ply`、`read ply` 和 `temp-workpiece run <job.json>`。

```powershell
cmake -S . -B <CliBuildDir> `
  -DPCV_BUILD_CLI=ON `
  -DPCV_BUILD_TESTS=ON
cmake --build <CliBuildDir> --config Debug --target pointcloudcli
ctest --test-dir <CliBuildDir> -C Debug -R "cli_" --output-on-failure
```

保持默认 `PCV_BUILD_CLI=OFF` 时，Qt Creator 的 View/Stitch 运行目标不受影响。

CLI-only 配置不会查找 Qt Widgets/OpenGL。`inspect ply` 只检查 Header，`read ply` 读取完整点云并输出摘要；两个命令默认不写文件。若使用 Job JSON 执行业务命令，成功摘要写入 stdout，阶段进度和错误 JSON 写入 stderr。退出码 `0/2/10/20/30/40/130` 分别表示成功、契约、输入、坐标转换、平面/WObj1/ROI、输出和取消。

脚本会检查 Qt、CMake 和 MSVC 环境。构建输出、缓存、日志和导出物应放在源码树之外。

### 一键 CLI 构建与便携部署

`scripts/build_windows.ps1` 在 `-CliOnly` 下会自动探测 CMake、Visual Studio/MSVC 和 Qt；仍可通过 `-CMakePath`、`-QtDir` 覆盖探测结果。`-CliOnly` 与 `-ExperimentalCamera` 互斥，`-Deploy` 必须配合 `-CliOnly` 且只支持 Release：

```powershell
.\scripts\build_windows.ps1 -CliOnly -BuildTests -Config Debug -BuildDir C:\pcv-build\cli-debug
ctest --test-dir C:\pcv-build\cli-debug -C Debug -R "cli_" --output-on-failure
.\scripts\build_windows.ps1 -CliOnly -Config Release -BuildDir C:\pcv-build\cli-release -Deploy -DeployDir C:\pcv-deploy\pointcloudcli
```

部署步骤会调用同一 Qt 版本的 `windeployqt --release --compiler-runtime`，补齐可用的 x64 VC Runtime，检查 `pointcloudcli.exe` 及 Qt DLL 的导入依赖，创建 `data` 目录并运行清空 PATH 的 15 项 CLI 契约测试。目标目录必须位于源码树外且不能是盘符根目录；现有目录会在安全检查通过后清空重建。也可运行 `scripts\build_pointcloudcli_release.bat` 使用默认 Release 部署目录。

CLI 运行数据目录按 `--data-dir`、`PCV_DATA_DIR`、exe 同级 `data`、`%LOCALAPPDATA%\PointCloudSuite\data` 顺序选择可写目录。只有 `temp-workpiece run` 接受 `--data-dir`；`inspect ply` 和 `read ply` 携带该选项会返回 `PCV_CONTRACT_001` 和退出码 `2`。

## 生成器与头文件依赖跟踪

推荐使用 `NMake Makefiles`，因为项目通过 CMake 自身扫描器记录 MSVC 头文件依赖。`Ninja` 依赖 MSVC `/showIncludes` 前缀，在本地化 MSVC 环境可能生成无头文件依赖的数据库；`NMake Makefiles JOM` 会生成空的 `.obj.d` 文件，脚本会拒绝该生成器。

- `NMake Makefiles` 使用 CMake 扫描器；`Ninja` 仍通过 `/showIncludes` 生成依赖，脚本会在构建后检查依赖数据库；
- 在中文 MSVC 上该前缀被以错误编码保存，可在 `<BuildDir>/CMakeFiles/<ver>/CMakeCXXCompiler.cmake` 中看到乱码值（例如 `CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢:  "`），过滤永远不命中：所有 `*.obj.d` 为 **0 字节**，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`；
- 后果：**修改公共头不会触发依赖方重新编译**。按不同结构体布局编译出的新旧目标文件被链接进同一个库，运行期表现为栈破坏、崩溃或随机行为；而单元测试可能因为可执行文件是在头文件变更之后新建的、自身布局自洽而全部通过。

因此：

- 项目不再设置 `CMAKE_DEPENDS_USE_COMPILER OFF`；配置阶段通过 `VSLANG=1033` 固定 MSVC `/showIncludes` 语言，构建阶段由 `CMAKE_CXX_COMPILER_LAUNCHER` 和 `scripts/build_windows.ps1` 继续保持一致；
- `scripts/build_windows.ps1` 构建完成后会检查依赖数据库中是否真的记录了头文件，未记录则以非零码退出；
- MSVC 配置若 ABI 探测失败或指针宽度不是 8 字节会直接终止；脚本会拒绝 `NMake Makefiles JOM` 和依赖数据库失效的生成器；
- 根 CMake 的 `PCV_BUILD_EXPERIMENTAL_CAMERA` 默认值为 `ON`；`scripts/build_windows.ps1` 默认传入 `OFF`，需要实验相机时显式使用 `-ExperimentalCamera`。该模块不属于 v0.3 发布基线；
- 命令行强制全量重建：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Clean -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
```

**修改公共头（尤其是新增结构体字段）后，必须使用能跟踪依赖的构建树，或执行 `-Clean` / Qt Creator 的“重新构建项目”。旧构建目录需要重新配置一次以应用新的 `VSLANG` 和依赖护栏。**

## 验收原则

修改公共头、CMake、UI、processor 或输出契约后，必须重新配置并构建受影响应用，必要时执行 Debug/Release 全量 CTest。GUI 人工验收和真实生产夹具验收必须单独记录。

最后核对日期：2026-09-13。
