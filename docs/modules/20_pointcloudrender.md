# 20_pointcloudrender：点云渲染

状态：部分实现。当前实现位于 `apps/pointcloudview`，目标为抽取 Qt/OpenGL 画布、VBO、真实点选、视角交互和异步结果发布。

依赖：可依赖 Qt Widgets/OpenGL 及 `10_pointcloudread`；算法线程不得访问 QWidget/OpenGL。

接口/测试：`MainWindow`、OpenGL widget 和 processor 加载入口；需补充 VBO、版本校验、关闭烟测和 GUI 验收。

## 变更记录

### 2026-08-31

- 建立模块边界和 `pcv_m20_pointcloudrender` 兼容入口；未移动应用代码。
- M5.3 首轮：`pcv_m20_pointcloudrender` 已从 `INTERFACE` 改为静态库，新增渲染快照版本/点状态契约和独立测试；`PointCloudCanvas` 实现仍待从 `mainwindow.cpp` 抽取。
- M5.3 第二轮：确认 `PointCloudCanvas` 直接持有 `WorkpieceCoordinateSystem`、`PlaneContour` 等应用 processor 类型；在这些类型下沉到公共头前不迁移实体，避免共享模块反向依赖 `apps/`。

### 2026-08-31（M5.3 快照集成）

- `PointCloudCanvas::setCloud()` 和 `setPlaneResult()` 接入 `RenderSnapshot` 校验，并在每次画布数据更新时递增渲染版本号。
- 快照校验失败时拒绝发布点云或平面状态，保留当前画布内容并记录 `qWarning`；未改变 Qt/OpenGL 线程边界。
- 验证：`pointcloud_canvas_contract_tests` 和完整 Debug CTest（14/14）通过。

### 2026-08-31（M5.3 DTO 下沉）

- 新增 `pcv::render::CoordinateFrame` 和 `pcv::render::Contour` 渲染 DTO。
- `PointCloudCanvas` 不再直接接收 `WorkpieceCoordinateSystem`、`PlaneContour`；应用入口负责显式转换，渲染模块保持独立依赖方向。
- OpenGL 实体仍暂留 `mainwindow.cpp`，后续可在不引入 processor 依赖的前提下迁移。
- 验证：`pointcloudview` Debug 构建、完整 Debug CTest（14/14）通过。

### 2026-08-31（M5.3 DTO 边界校验）

- 新增 `validateCoordinateFrame()` 和 `validateContours()`，拒绝渲染坐标系/轮廓中的非有限值。
- `PointCloudCanvas` 在更新坐标系或轮廓前执行 fail-closed 校验，失败时不改变当前 OpenGL 状态。
- `pointcloud_canvas_contract_tests` 新增 NaN/Inf 边界覆盖；完整 Debug CTest（14/14）通过。

### 2026-08-31（M5.3 画布实体迁移）

- `PointCloudCanvas` 实体已从 `apps/pointcloudview/mainwindow.cpp` 移至 `modules/20_pointcloudrender/include/pcv/render/pointcloud_canvas.h`。
- 画布继续使用既有 OpenGL/FBO/VBO 生命周期和 Qt 线程检查；`MainWindow` 仅保留业务编排与 DTO 转换。
- 当前实现以内联形式迁移，后续可在接口稳定后拆分为 `.cpp`；真实 GUI 点选/矩形选区验收仍待执行。
- 验证补充：`pointcloudview`、`pointcloudstitch` Release 构建通过，Release CTest 14/14 通过。
- UI 资源已独立存放于 `apps/pointcloudview/ui/mainwindow.ui`，可直接使用 Qt Designer 编辑；业务源码不再与 `.ui` 文件混放。
