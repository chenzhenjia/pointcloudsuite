# 30 拼接

Target：`pcv_m30_pointcloud_stitch`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：stitch、stitchRawLineProfiles、stitchSelectedPlanes、seam。

整帧ICP含转换、降采样、法向、多层配准、验证/回退与原序合并。关闭ICP仅转换。所选平面只收robot_base，修正受预算约束，warn_only；formal_output只代表事务成功。seam默认8/0.6/0.5mm。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
