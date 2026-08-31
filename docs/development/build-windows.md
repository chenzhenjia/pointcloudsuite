# Windows 构建

环境要求：

- Qt 6.5+ Desktop MSVC x64 Kit
- Visual Studio 的 MSVC x64 工作负载
- ASCII-only source and build paths

构建两个桌面程序和测试：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
```

`build_windows.ps1` 会先检查 `cl.exe` 和 `nmake.exe`。当前 PowerShell 尚未初始化 MSVC
环境时，脚本通过 `vswhere.exe` 定位 `vcvars64.bat` 并导入 x64 环境；Qt 从显式 `-QtDir`、
`Qt6_DIR`、`CMAKE_PREFIX_PATH` 或 PATH 中的 Qt 工具定位，CMake/CTest 从 PATH 定位。
找不到必需工具时会给出明确错误，不继续配置。

`run_tests.ps1` 从 PATH 中查找 CTest，并在运行前检查 `BuildDir` 存在。

请在本机 Windows Developer PowerShell 或 Qt Creator Kit 环境中运行构建。不要通过修改
源码、删除测试或把构建目录放回源码树来绕过工具缺失问题。`cl.exe` 不在 PATH 时脚本会
通过 `vswhere.exe` 初始化；源码和构建路径必须为 ASCII，默认构建目录位于源码目录同级。

若实际出现编译器、链接器或 CTest 错误，应保留完整错误文本，在修复并重新验证前不要创建
发布 commit。

在 Qt Creator 中打开仓库根目录 `CMakeLists.txt`。工作区包含独立的
`pointcloudview` 和 `pointcloudstitch` 可执行目标；Debug 测试还覆盖
`pcv_registration`、`pcv_interface`、平面输出和边缘 Mask。

Qt Creator 不需要也不应提交 `.qtcreator/` 本机缓存。选择本机兼容 Kit 后直接配置根目录
即可；`CMakePresets.json` 不含 Qt 安装绝对路径。
