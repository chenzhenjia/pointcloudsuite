# 公共头文件

`include/pcv/` 保存共享模块的公共头文件和迁移期 forwarding header。

当前规范头路径按模块组织，例如：

```cpp
#include <pcv/io/ply_reader.h>
#include <pcv/registration/multiframe_registration.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/render/pointcloud_canvas.h>
#include <pcv/output/plane_output.h>
```

公共头必须保持与规范模块 API 一致。兼容头仅用于迁移，不得新增重复实现，也不代表独立 SDK 的长期兼容承诺。

最后核对日期：2026-08-31。
