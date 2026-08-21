# minimal plane output v0.1

这是 `point-cloud-plane-output-v0.1-proposal` 契约的最小非生产示例。

输入：一个已位于 `robot_base` 的 PLY 平面点云；`pixel_size_mm` 固定为 `0.05`。

任务参数：`runtime_root=runtime_data`、`job_id=job_demo_001`、`workpiece_id=workpiece_demo_001`、`base_name=demo`。

生成目录：`runtime_data/jobs/job_demo_001/`。

三项输出（PNG 前景为 `255`，背景为 `0`）：

* `point_cloud/plane/demo.png`
* `point_cloud/plane/demo.json`
* `point_cloud/plane/demo_plane_robot_base.ply`

主程序登记 `demo.json`、`demo.png` 和 `demo_plane_robot_base.ply` 三个相对引用即可；点云模块不会修改 `workpiece_list.json`。
