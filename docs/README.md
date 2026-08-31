# 文档索引

项目入口：[根目录 README](../README.md)。文档按用途分为：

- `architecture/`：共享模块、依赖边界和代码构成；
- `requirements/`：`pointcloudview` 与 `pointcloudstitch` 的当前需求和验收边界；
- `contracts/`：PNG、JSON、robot_base 平面 PLY 的输出契约；
- `user-guide/`：主程序操作、参数、故障处理和验证记录；
- `development/`：Windows + Qt/MSVC 构建说明。

当前文档基线为 `v0.3` 迁移收尾版；`v0.1`、`v0.2` 文档保留为历史基线。
v0.3 需求文档位于 `requirements/pointcloudview_v0.3.md`，模块变更记录位于 `modules/`。
构建和测试命令以根目录 `CMakePresets.json`、`scripts/build_windows.ps1` 和
`scripts/run_tests.ps1` 为准。`pcv_io`、`pcv_registration`、`pcv_output` 为迁移期间兼容
target，待弃用周期和发布验收完成后单独删除；规范 target 使用 `pcv_m<编号>_<名称>`。
# 文档索引

当前活动版本为 `docs/requirements/pointcloudview_v0.3.md`。该文档统筹模块边界、代码迁移、接口契约、测试和发布验收；`pointcloudview_v0.1.md`、`pointcloudview_v0.2.md` 为历史基线。

## 模块文档

- [10_pointcloudread](modules/10_pointcloudread.md)：点云读写
- [20_pointcloudrender](modules/20_pointcloudrender.md)：点云渲染
- [30_pointcloudstitch](modules/30_pointcloudstitch.md)：点云拼接
- [40_pointcloudregistration](modules/40_pointcloudregistration.md)：点云配准
- [50_coordinateconversion](modules/50_coordinateconversion.md)：坐标转换
- [60_planefitting](modules/60_planefitting.md)：平面拟合
- [70_roi_template](modules/70_roi_template.md)：ROI/模板
- [80_planeoutput](modules/80_planeoutput.md)：工具平面输出
- [90_interferenceplane](modules/90_interferenceplane.md)：干涉平面检查（未实现）
- [100_qualityreport](modules/100_qualityreport.md)：质量报告（未实现）

模块文档采用追加式变更记录。每次代码修改必须同步记录日期、需求、文件、验证结果和风险。
