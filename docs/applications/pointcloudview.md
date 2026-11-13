# pointcloudview 开发说明

入口：`pointcloudview`；共享依赖：`pcv_workbench`。

MainWindow/SelectedPlaneDialog/相机页面持有状态，共享读取、选面与拼接任务。保留查看、过滤、CAD/模板、整帧/选面、双平面/ROI、纹理和扫描轨迹。

GUI仅处理会话与参数映射，领域算法归模块。输入/参数变化使派生结果失效，worker只接收不可变快照，取消/关闭等待join。

[使用指南](../user-guide/pointcloudview.md) · [构建](../development/build-windows.md) · [契约](../contracts/data-contracts.md)
