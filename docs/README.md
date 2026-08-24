# 文档索引

项目入口：[根目录 README](../README.md)。文档按用途分为：

- `architecture/`：共享模块、依赖边界和代码构成；
- `requirements/`：`pointcloudview` 与 `pointcloudstitch` 的当前需求和验收边界；
- `contracts/`：PNG、JSON、robot_base 平面 PLY 的输出契约；
- `user-guide/`：主程序操作、参数、故障处理和验证记录；
- `development/`：Windows + Qt/MSVC 构建说明。

当前文档基线为 `v0.1`；2026-08-24 的源码同步记录在各需求档案和用户指南末尾。
构建和测试命令以根目录 `CMakePresets.json`、`scripts/build_windows.ps1` 和
`scripts/run_tests.ps1` 为准。当前共享模块还包括 `pcv_registration` 和
`pcv_interface`，其公共头文件分别位于 `include/pcv/registration/` 与
`include/pcv/interface/`。
