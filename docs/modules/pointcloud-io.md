# 10 IO、过滤、相机与事务

Target：`pcv_m10_pointcloud_io`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：readPly/readPlyCached、voxelDownsample/removeStatisticalOutliers、CameraFrame/采集会话、ArtifactTransaction、writePlaneOutput。

PLY保留ASCII/大小端读取、来源映射和点序。RAW核对尺寸/摘要/SDK转换环境，未知轮廓拒绝。正式新业务用ArtifactTransaction；旧writePlaneOutput仍保留自身回滚，不宣称事务实现全部统一。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
