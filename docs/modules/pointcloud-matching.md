# 40 模板定位

Target：`pcv_m40_pointcloud_matching`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：matchTemplateV1。

STATIC后端保留，当前无GUI入口。有限平面/边界假设、6DoF精化、完整核验、多候选人工确认；不保证全局唯一或完备。机器人移动判定/修正下发未实现。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
