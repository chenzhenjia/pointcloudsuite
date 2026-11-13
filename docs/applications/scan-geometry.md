# scan_geometry_demo_v1 开发说明

入口：`scan_geometry_demo_v1`；共享依赖：`pcv_scan_geometry_session_v2 / pcv_scan_geometry_demo_view_v1`。

ScanGeometrySessionV2组织六帧窗口、完整候选、参考匹配、逐帧审核、批量确认、拼接、修剪/撤销、WObj1和几何保存。展示最多100万点，旧选面对话框30万；完整计算/输出不截断。

GUI仅处理会话与参数映射，领域算法归模块。输入/参数变化使派生结果失效，worker只接收不可变快照，取消/关闭等待join。

[接口说明](../development/scan-geometry-interfaces.md)集中定义会话、区域计算、保存和可选契约函数的调用条件及数据有效期；会话保存统一使用`saveGeometry()`。

[使用指南](../user-guide/scan-geometry.md) · [构建](../development/build-windows.md) · [契约](../contracts/data-contracts.md)
