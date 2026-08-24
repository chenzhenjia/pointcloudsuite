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

在 Qt Creator 中打开仓库根目录 `CMakeLists.txt`。工作区包含独立的
`pointcloudview` 和 `pointcloudstitch` 可执行目标；Debug 测试还覆盖
`pcv_registration`、`pcv_interface`、平面输出和边缘 Mask。
