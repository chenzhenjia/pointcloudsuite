# Point cloud plane output v0.1

该文档同时作为当前 `pcv_output` 实现的 v0.1 契约基线；`proposal` 名称保留用于
兼容已有示例和下游引用。

首版 schema 为 `point-cloud-plane-output-v0.1-proposal`。长度单位为 `mm`，角度单位为 `deg`；平面 PLY 点坐标和 `source` 坐标系均为 `robot_base`，PNG 映射使用 `workpiece XY`。

## 目录和成套文件

`runtime_data/jobs/<job_id>/point_cloud/plane/` 下生成同一 `base_name` 的三个文件：

* `<base>.png`：`Grayscale8`、8 位单通道、背景 `0`、前景 `255`。
* `<base>.json`：契约元数据，最后原子提交。
* `<base>_plane_robot_base.ply`：`binary_little_endian`，坐标为 `robot_base`。

JSON 中的 `outputs` 和 `image.file` 始终是相对于 job 根目录的正斜杠路径，主程序只需登记这些引用，不由点云模块修改 `workpiece_list.json`。

矩阵约定为 `P_base = T_base_workpiece * P_workpiece`，JSON 同时提供按行排列的 `T_base_workpiece` 和 `T_workpiece_base`。

写入顺序是 PNG、PLY、JSON；任一步失败都不会提交 `success=true` 的最终 JSON。稳定错误码包括 `PCV_CONTRACT_001`、`PCV_IMAGE_001`、`PCV_PLANE_001`、`PCV_OUTPUT_001` 和 `PCV_OUTPUT_002`。
