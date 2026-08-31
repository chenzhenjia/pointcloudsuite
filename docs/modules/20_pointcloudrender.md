# 20_pointcloudrender：点云渲染

状态：部分实现。当前实现位于 `apps/pointcloudview`，目标为抽取 Qt/OpenGL 画布、VBO、真实点选、视角交互和异步结果发布。

依赖：可依赖 Qt Widgets/OpenGL 及 `10_pointcloudread`；算法线程不得访问 QWidget/OpenGL。

接口/测试：`MainWindow`、OpenGL widget 和 processor 加载入口；需补充 VBO、版本校验、关闭烟测和 GUI 验收。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m20_pointcloudrender` 兼容入口；未移动应用代码。
