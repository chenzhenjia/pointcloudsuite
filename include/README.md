# 公共头文件

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

新 API 优先使用 `pcv::io`、`pcv::stitching`、`pcv::coordinates`、`pcv::geometry` 和 `pcv::workpiece`。旧 `pointcloud::`、`pcv::detail::*` 和 forwarding header 保留一个版本周期并标记为兼容/弃用。

最后核对日期：2026-09-17。
