# depth_raw_to_ply_v1 开发说明

入口：`depth_raw_to_ply_v1`；共享依赖：`pcv_m10_pointcloud_io`。

独立工具，PCV_BUILD_RAW_CONVERTER=ON且SDK可用时构建。输入depth.json，校验SDK版本/指纹/配置、RAW及强度摘要，输出新PLY，拒绝覆盖。

同步命令行工具，帧物化调用模块10官方SDK转换，输出camera/mm坐标。PLY与`<new.ply>.source_indices.u64`通过ArtifactTransaction一起提交；不会执行机器人位姿转换。usage/已有输出返回2，输入或摘要错误返回10，SDK环境/转换错误返回20，事务失败返回30。成功返回0。

[使用指南](../user-guide/input-output.md) · [构建](../development/build-windows.md) · [契约](../contracts/data-contracts.md)
