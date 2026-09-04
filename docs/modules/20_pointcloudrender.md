# 20_pointcloudrender：点云渲染

状态：迁移完成，真实桌面验收待完成。规范实现位于 `modules/20_pointcloudrender`，应用仅负责业务状态和 DTO 转换。

规范目录：`modules/20_pointcloudrender`；模块 target：`pcv_m20_pointcloudrender`。

依赖：可依赖 Qt Widgets/OpenGL 及 `pcv_core`；不得依赖 `apps/`。算法线程不得访问 QWidget/OpenGL。

接口/测试：`PointCloudCanvas`、`pointcloud_canvas_contract_tests`；真实 Windows 桌面交互和关闭生命周期需单独验收。

最后核对日期：2026-09-04。实现位于 `modules/20_pointcloudrender/src/pointcloud_canvas.cpp`。

## 变更记录

### 2026-09-04（包围盒轴线与整数刻度）

- 覆盖层的 X/Y/Z 在包围盒最小角点 `(minX,minY,minZ)` 共点，分别沿正 X、正 Y、正 Z 包围盒边延伸；使用红、绿、蓝着色，普通包围盒边不再重复绘制这些有效彩色边；
- 地板网格按 `t=i/10` 归一化后映射到当前包围盒范围（每轴 10 个区间、11 条边界线），彩色 X 边不再以灰色网格重复绘制；
- 轴标题固定为 `X(mm)`、`Y(mm)`、`Z(mm)`，刻度由实际毫米值四舍五入为整数；共点处不绘制坐标数字或刻度短线，同值标签去重，并以屏幕空间矩形跳过重叠或越界标签；
- 新增仅供渲染模块使用的 `overlay_geometry.h` 纯几何助手和回归覆盖，未扩展公共 DTO、点云数据、点选或输出契约。

### 2026-09-04（渲染设置覆盖层）

- `RenderOverlayOptions` 为坐标轴、地板网格、坐标标签、法向量、包围盒和统计信息提供显示开关；默认启用坐标轴、XY 地板、标签、包围盒和统计信息，默认关闭法向量；
- `PointCloudCanvas` 在深色画布上绘制有限网格、包围盒及最多 2,000 条有效法向量，辅助显示不改变点云数据、点选索引或输出契约；
- `RenderDialog` 由应用层编排，渲染模块不依赖 `apps/` 或 Qt Designer 生成代码。

### 2026-09（实时预览 MVP）

- `setCloudStreaming()` 的点云 VBO 改为 `StreamDraw` + 容量复用，避免每帧重新分配 GPU 缓冲；
- 流式换帧继续清除点索引覆盖层、保留视角和世界坐标覆盖层；
- 新增状态缓冲容量复用，上传失败时保持 fail-closed；
- 与 `05_camerainput` 的实时链路保持 GUI 线程上传、后台线程转换的边界；
- 未验证：真实 Windows 桌面交互和 DP2240 约 15 FPS 性能。

### 2026-08-31

- M5.3 收尾：`PointCloudCanvas` 全部构造、OpenGL、VBO/FBO、绘制、拾取、交互和资源释放方法已移入 `src/pointcloud_canvas.cpp`；公共头仅保留声明、信号回调和字段。
- 验证：`pcv_m20_pointcloudrender`、`pointcloudview` Debug 构建通过；完整 Debug CTest 14/14 通过。
- 未验证：Windows 桌面真实点选、矩形选区、旋转/平移/缩放和关闭生命周期。

- M5.3：新增 `src/pointcloud_canvas.cpp` 作为渲染模块独立编译单元，`pcv_m20_pointcloudrender` 继续保持静态库和 Qt/OpenGL 依赖。
- 当前 `PointCloudCanvas` 方法体仍以内联形式保留在公共头中，以维持现有调用方兼容；真实桌面点选、矩形选区和视角交互尚未完成发布验收。
- `registration_diagnostic` 不依赖渲染模块；`pcv_m20_pointcloudrender` 已有独立 `pointcloud_canvas.cpp` 编译单元，后续可在不改变 DTO 的前提下继续收紧 ABI。
- 自动关闭烟测在 offscreen 环境下 15 秒超时；需要 Windows 桌面人工运行验证真实窗口关闭、点选、矩形选区和视角交互。

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

### 2026-09（全界面浅色主题）

- 画布清屏与叠加文字保持浅色高对比显示，主窗口和相机对话框的 Designer 样式统一为白色视觉基线；
- 未改变 `PointCloudCanvas` 公共 API、点云数据、点选和覆盖层契约。

### 2026-09（画布深色显示区）

- OpenGL 画布恢复深色清屏背景，叠加标题、统计信息、坐标和状态文字使用浅色高对比度；
- 主窗口外围控件仍保持浅色主题，未改变 `PointCloudCanvas` 公共 API。
- 当前实现以内联形式迁移，后续可在接口稳定后拆分为 `.cpp`；真实 GUI 点选/矩形选区验收仍待执行。
- 验证补充：`pointcloudview`、`pointcloudstitch` Release 构建通过，Release CTest 14/14 通过。
- UI 资源已独立存放于 `apps/pointcloudview/ui/mainwindow.ui`，可直接使用 Qt Designer 编辑；业务源码不再与 `.ui` 文件混放。
