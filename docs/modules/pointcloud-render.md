# 20 渲染与拾取

Target：`pcv_m20_pointcloud_render`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：PointCloudCanvas、DepthImageCanvasV1、CadMeshCanvasV1。

展示抽样与完整业务点分离，拾取经displayToFull回译。保留高度色、统一点色、纹理、控制点、面片/边界和投影快照；非法坐标/索引拒绝。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
