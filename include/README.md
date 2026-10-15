# 公共头与兼容入口

v0.5，2026-10-15。include/pcv保存core/infrastructure及迁移期forwarding，模块公共头位于各modules的include。使用实际公共路径：

```cpp
#include <pcv/io/ply_reader.h>
#include <pcv/io/artifact_transaction.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/stitching/selected_plane_stitch.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <pcv/render/pointcloud_canvas.h>
```

v1选面头承载当前v2行为以保兼容；PointCloudCanvas是全局类，头路径render不是namespace。IO、整帧、坐标等旧namespace仍在，不能声称已统一或全部deprecated。[实际映射](../docs/architecture/software_architecture_v0.5.md)与[迁移规则](../docs/architecture/module_ui_integration_v0.5.md)。兼容头不得新增第二套实现。
