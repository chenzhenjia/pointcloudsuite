# Windows 构建

## 环境要求

- Windows 10/11；
- MSVC x64，支持 C++17；
- CMake 3.19+；
- Qt 6.5+，包含 Core、Gui、Widgets、Concurrent、OpenGL 和 OpenGLWidgets；
- 源码和构建路径使用 ASCII。

## 推荐命令

在 Developer PowerShell 中执行：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

也可以使用：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

脚本会检查 Qt、CMake 和 MSVC 环境。构建输出、缓存、日志和导出物应放在源码树之外。

## 生成器与头文件依赖跟踪

Windows 上使用 `NMake Makefiles`。当前 `CMake 3.30 + MSVC 19.51` 环境下，`NMake Makefiles JOM` 会生成空的 `.obj.d` 文件，即使编译成功也不能保证公共头依赖被跟踪；Qt Creator 的生成器必须从默认的 JOM 切换为 `NMake Makefiles`。

- 这些生成器默认让编译器产出依赖：编译时附加 `/showIncludes`，再按**配置阶段捕获的界面语言前缀**过滤编译器输出；
- 在中文 MSVC 上该前缀被以错误编码保存，可在 `<BuildDir>/CMakeFiles/<ver>/CMakeCXXCompiler.cmake` 中看到乱码值（例如 `CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢:  "`），过滤永远不命中：所有 `*.obj.d` 为 **0 字节**，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`；
- 后果：**修改公共头不会触发依赖方重新编译**。按不同结构体布局编译出的新旧目标文件被链接进同一个库，运行期表现为栈破坏、崩溃或随机行为；而单元测试可能因为可执行文件是在头文件变更之后新建的、自身布局自洽而全部通过。

因此：

- 项目不再设置 `CMAKE_DEPENDS_USE_COMPILER OFF`；配置阶段通过 `VSLANG=1033` 固定 MSVC `/showIncludes` 语言，构建阶段由 `CMAKE_CXX_COMPILER_LAUNCHER` 和 `scripts/build_windows.ps1` 继续保持一致；
- `scripts/build_windows.ps1` 构建完成后会检查依赖数据库中是否真的记录了头文件，未记录则以非零码退出；
- MSVC 配置若 ABI 探测失败或指针宽度不是 8 字节会直接终止；脚本会拒绝 `NMake Makefiles JOM` 和依赖数据库失效的生成器；
- 相机扩展通过 `-ExperimentalCamera` 显式启用，默认 OFF，不属于 v0.3 发布基线；
- 命令行强制全量重建：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Clean -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
```

**修改公共头（尤其是新增结构体字段）后，必须使用能跟踪依赖的构建树，或执行 `-Clean` / Qt Creator 的“重新构建项目”。旧构建目录需要重新配置一次以应用新的 `VSLANG` 和依赖护栏。**

## 验收原则

修改公共头、CMake、UI、processor 或输出契约后，必须重新配置并构建受影响应用，必要时执行 Debug/Release 全量 CTest。GUI 人工验收和真实生产夹具验收必须单独记录。

最后核对日期：2026-09-01。
