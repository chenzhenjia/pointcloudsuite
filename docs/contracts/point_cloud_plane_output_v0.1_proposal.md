# 平面与临时工件输出契约 v0.1

文件名保留 `proposal` 后缀以兼容已有引用，但当前实现以源码常量和测试为准。
平面与边缘 Mask 输出统一使用 schema `sr2026-temp-workpiece-info-mvp-2`，临时扫描输入
使用 schema `sr2026-temp-scanning-info-mvp-2`。长度单位为 `mm`，角度单位为 `deg`。

平面 PLY 点坐标和 `source` 坐标系均为 `robot_base`，PNG 映射使用 `workpiece XY`。

## 目录和成套文件

默认在 `runtime_data/jobs/<job_id>/point_cloud/plane/` 下生成同一 `base_name` 的三个文件；
当 `JobContext.destinationDirectory` 非空时，直接写入该目录：

* `<base>.png`：`Grayscale8`、8 位单通道、背景 `0`、前景 `255`。
* `<base>.json`：当前 schema 的 `plane/image/roi/outputs` 元数据。
* `<base>_plane_robot_base.ply`：`binary_little_endian`，坐标为 `robot_base`。

JSON 的 `plane.equation` 为 `[X, Y, Z, RZ, RY, RX]`；`outputs.robot_base_point_cloud` 为来源点云，`outputs.roi_point_cloud` 和 `outputs.plane_mask` 为规范化绝对路径。

图像记录 `width_px`、`height_px`、`width_mm`、`height_mm` 和 `pixel_size_mm`。

写入顺序是临时目录中的 PNG、PLY、JSON，再依次提交正式文件；任一步失败都会回滚。稳定错误码包括 `PCV_CONTRACT_001`、`PCV_IMAGE_001`、`PCV_PLANE_001`、`PCV_FRAME_001`、`PCV_OUTPUT_001` 和 `PCV_OUTPUT_002`。

## 临时工件四件套

`pcv_interface::generateTempWorkpiece` 读取 `sr2026-temp-scanning-info-mvp-2` 扫描 JSON 和 XML 标定，按 `FullXyz` 或 `LineProfileXz` 完成手眼转换、平面提取和矩形 ROI，输出 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png` 和 `temp_workpiece_info.json`。四件文件全部先写入临时目录，再一次性提交；输入缺失、路径穿越、标定无效、平面失败或写入不完整时不留下部分正式结果。
