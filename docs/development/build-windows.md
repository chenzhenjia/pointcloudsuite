# Windows 构建

环境要求：

- Qt 6.8.3 MSVC 2022 64-bit
- Visual Studio 2022/18 的 MSVC x64 工作负载
- ASCII-only source and build paths

构建两个桌面程序和测试：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
```

`build_windows.ps1` 会先检查 `cl.exe` 和 `nmake.exe`。当前 PowerShell 尚未初始化 MSVC
环境时，脚本通过 `vswhere.exe` 或标准 Visual Studio 安装路径定位 `vcvars64.bat`，导入
x64 环境后再构建；同时校验 Qt 目录和 `Qt6Config.cmake`，并依次从 PATH、Qt Tools 和
Visual Studio 安装目录查找 `cmake.exe`。找不到必需工具时会给出明确错误，不继续配置。

`run_tests.ps1` 从 PATH 中 `cmake.exe` 的同目录或
`C:\Qt\Tools\CMake_64\bin\ctest.exe` 查找 CTest，并在运行前检查 `BuildDir` 存在。

如果执行环境拒绝访问工作区外的 `C:\Qt\Tools\CMake_64\bin\cmake.exe` 或
`ctest.exe`，这是沙箱/权限限制，不是项目编译错误。请在本机 Windows Developer
PowerShell 中运行，或取得针对该工具路径的显式执行授权；不要通过修改源码、删除测试
或把构建目录放回源码树来绕过限制。`cl.exe` 不在 PATH 时先调用 Visual Studio 的
`vcvars64.bat`，并使用 ASCII 且位于源码树外的构建目录（如 `C:\qt-build-pointcloudsuite`）。

若实际出现编译器、链接器或 CTest 错误，应保留完整错误文本，在修复并重新验证前不要创建
发布 commit。

在 Qt Creator 中打开仓库根目录 `CMakeLists.txt`。工作区包含独立的
`pointcloudview` 和 `pointcloudstitch` 可执行目标；Debug 测试还覆盖
`pcv_registration`、`pcv_interface`、平面输出和边缘 Mask。
