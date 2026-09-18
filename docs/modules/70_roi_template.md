# 70_roi_template：ROI/模板

状态：兼容入口。ROI、模板、Mask、WObj 和临时工件实现已迁入 `modules/60_geometry_workpiece` / `pcv_m60_geometry_workpiece`；`modules/70_roi_template`、`pcv_m70_roi_template` 和旧 header 只做转发，不得新增重复实现。正式输出事务归 10。

接口/测试：`pcv::workpiece` 下的 ROI 与临时工件接口；`temp_workpiece_interface_tests`、`roi_matching_tests` 和几何回归测试。

最后核对日期：2026-09-17。以下变更记录保留 70 仍为实现模块时的历史事实。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m70_roi_template` 兼容入口；未改变现有 ROI 行为。
- M4：临时工件输入解析、平面/ROI 准备和四件套接口已迁移到模块源码，行为契约保持不变。
