# 70_roi_template：ROI/模板

状态：部分实现。当前实现分布于 `pcv_interface` 和 `apps/pointcloudview`，负责工件坐标系、矩形 ROI、平面映射、模板点云和 Mask。

接口/测试：`extractPlaneImage`、`generateTempWorkpiece`；`temp_workpiece_interface_tests` 和几何回归测试。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m70_roi_template` 兼容入口；未改变现有 ROI 行为。
- M4：临时工件输入解析、平面/ROI 准备和四件套接口已迁移到模块源码，行为契约保持不变。
