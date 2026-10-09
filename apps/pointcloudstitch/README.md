# pointcloudstitch

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

`pointcloudstitch` 是 Qt 6/C++17 多帧线扫点云拼接应用。

## 当前职责

应用负责文件选择、窗口状态、进度显示、回归入口和结果展示。共享拼接流程（多帧拼接、ICP、seam fusion、manifest）由 `pcv_m30_pointcloud_stitch` 编排，坐标转换由 `pcv_m50_coordinate_conversion` 提供，最终文件由 10 的 `pcv::io::ArtifactTransaction` 提交。40 当前为规划占位，不作为生产拼接实现依赖。

应用不维护第二套配准或接缝算法。兼容入口仅做参数转换和结果映射。

## 输入与输出

输入为四帧或多帧 PLY、手眼标定 XML 和起止位姿。统一接口为 `pcv::interface::StitchingRequest` / `stitch()`；`stitchRawLineProfiles` 为兼容入口。正式输出沿用 `stitched_robot_base.ply` 与 provenance/manifest。GUI 默认 `PoseOverlapFine`，公共 API 默认 `Legacy`；快速模式每对预算 `10 mm / 1°`，仅质量问题可警告并回退机器人位姿，输入、取消、映射或事务错误仍失败。`formal_output` 表示事务成功，不能表示严格质量验收通过；快速模式默认关闭 seam 与 preview。

## 构建

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <BuildDir> -CMakePath <cmake.exe>
```

详细架构、UI 边界和交接格式见 `docs/architecture/module_ui_integration_v0.4.md`；数据契约和验收条件见 `docs/requirements/pointcloudview_v0.3.md` 与 `docs/modules/30_pointcloud_stitch.md`。

最后核对日期：2026-10-09（文档静态核对）。
