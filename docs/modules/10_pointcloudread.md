# 10_pointcloudread：点云读写

状态：已实现，迁移中。当前实现位于 `src/io`、`include/pcv/io`，兼容 target 为 `pcv_io`，模块 target 为 `pcv_m10_pointcloudread`。

职责：PLY ASCII/binary LE/BE 读取与写入、属性校验、缓存、取消、进度、点序和 bounds 保持。不得依赖应用或 Qt Widgets。

接口/测试：`pcv::detail::io::readPly`、`readPlyCached`；`ply_reader_tests`、`cloud_cache_tests`。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m10_pointcloudread` 兼容入口；源码暂留 `src/io`，未移动用户未提交修改。
