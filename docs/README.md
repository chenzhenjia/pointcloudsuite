# Documentation

项目入口：[根目录 README](../README.md)。文档按用途分为：

- `architecture/`：共享模块、依赖边界和代码构成；
- `requirements/`：`pointcloudview` 与 `pointcloudstitch` 的当前需求和验收边界；
- `contracts/`：PNG、JSON、robot_base 平面 PLY 的输出契约；
- `user-guide/`：主程序操作、参数、故障处理和验证记录；
- `development/`：Windows + Qt/MSVC 构建说明。

当前文档基线为 `v0.1`。构建和测试命令以根目录 `CMakePresets.json`、
`scripts/build_windows.ps1` 和 `scripts/run_tests.ps1` 为准。
