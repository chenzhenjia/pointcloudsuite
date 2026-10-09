# 公共头文件

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

`include/pcv/` 保存共享模块的公共头文件和迁移期 forwarding header。

当前规范头路径按模块组织，例如：

```cpp
#include <pcv/io/ply_reader.h>
#include <pcv/stitching/multiframe_registration.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/planefitting/plane_fitting.h> // compatibility namespace; implementation is module 60
#include <pcv/render/pointcloud_canvas.h>
#include <pcv/io/artifact_transaction.h>
```

公共头必须保持与规范模块 API 一致。兼容头仅用于迁移，不得新增重复实现，也不代表独立 SDK 的长期兼容承诺。

新 API 优先使用 `pcv::io`、`pcv::stitching`、`pcv::coordinates`、`pcv::geometry` 和 `pcv::workpiece`。旧 `pointcloud::`、`pcv::detail::*` 和 forwarding header 的保留/弃用是迁移规则，不能理解为已经全部标记 `[[deprecated]]`。当前实际 namespace 还包括 `pcv::interface`、`pcv::output`、`pcv::planefitting`、`pcv::roi` 和 `pcv::matching`；完整映射与待办见 [v0.4 协同文档](../docs/architecture/module_ui_integration_v0.4.md)。

最后核对日期：2026-10-09（文档静态核对）。
