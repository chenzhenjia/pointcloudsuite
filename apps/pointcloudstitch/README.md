# pointcloudstitch

`pointcloudstitch` 是 Qt 6/C++17 多帧线扫点云拼接应用。

## 当前职责

应用负责文件选择、窗口状态、进度显示、回归入口和结果展示。共享拼接流程（多帧拼接、seam fusion、正式输出）由 `pcv_m30_pointcloud_stitch` 编排，坐标转换由 `pcv_m50_coordinate_conversion` 提供，ICP、结构校验和 ROI 匹配（完整回传 robot_base）由 `pcv_m40_pointcloud_matching` 提供。

应用不维护第二套配准或接缝算法。兼容入口仅做参数转换和结果映射。

## 输入与输出

输入为四帧或多帧 PLY、手眼标定 XML 和起止位姿。统一接口为 `pcv::interface::stitchRawLineProfiles`，正式输出为 `stitched_robot_base.ply`；失败、取消或诊断不完整时不得生成或复用旧正式输出。

## 构建

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -QtDir <QtDir> -BuildDir <BuildDir> -CMakePath <cmake.exe>
```

详细契约和验收条件见 `docs/requirements/pointcloudview_v0.3.md` 与 `docs/modules/30_pointcloud_stitch.md`。

最后核对日期：2026-09-07。
