# 整体架构

```text
三个 GUI / RAW converter
    → 应用会话与共享任务 / 工作台对象库
    → 10 IO / 20 Render / 30 Stitch / 40 Match / 50 Coordinates / 60 Geometry
    → core / infrastructure
    → PLY / PCVMAP / JSON / RAW / STEP / SDK
```

| target | 责任和依赖 |
|---|---|
| pcv_workbench | 工作台公共源码与资源只编译一次，程序和工作台 GUI 测试复用 |
| pcv_selected_plane_tasks | 两个选面入口共用读取、转换、展示抽样、索引、选面、快照与摘要 |
| pcv_stitching_ui | 工作台整帧窗口、参数与发布 |
| pcv_m10_pointcloud_io | IO/相机/过滤/事务 → core/infrastructure |
| pcv_m20_pointcloud_render | QWidget/OpenGL、画布和拾取 → core |
| pcv_m30_pointcloud_stitch | 拼接 → 10/50；不依赖60 |
| pcv_m40_pointcloud_matching | 模板定位 → 50/60 |
| pcv_m50_coordinate_conversion | 唯一手眼/位姿/坐标数学 |
| pcv_m60_geometry_workpiece | 平面/工件/纹理/扫描/CAD模板 → 10/50 |

领域模块不依赖 apps；20 是唯一允许 Widgets/OpenGL 的领域模块。算法位于模块 src，公共头位于模块 include，应用只适配请求和结果。

应用保持各自的审核和调度状态机：旧选面对话框与扫描会话的轮次、人工审核和缓存预算不同；共用任务算法、输入校验和结果身份门控，保留各自队列语义。
