# pointcloudview 使用与验收说明

## 应用职责

`pointcloudview` 用于加载 PLY 点云、渲染和点选、平面拟合、工件坐标系建立、ROI/模板处理以及 PNG/PLY/JSON 输出。

## 界面文件

Qt Designer 文件位于：

- `apps/pointcloudview/ui/mainwindow.ui`
- `apps/pointcloudstitch/ui/stitchingwindow.ui`

`PointCloudCanvas` 的实现位于 `modules/20_pointcloudrender/src/pointcloud_canvas.cpp`，应用只负责异步任务、关闭状态、业务流程和 DTO 转换。

## 处理流程

输入校验 → PLY 读取/缓存 → 渲染和点选 → 坐标转换 → 平面拟合 → ROI/模板 → 成套输出。

后台线程不得访问 QWidget/OpenGL；窗口关闭时应丢弃过期结果并等待任务结束。

## 输出契约

正式输出使用 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。PNG、PLY、JSON 必须成套提交，任一失败都要回滚。

## 验收边界

自动化测试不能替代 Windows 桌面点选、矩形选区、视角交互、关闭生命周期和真实生产数据验收。当前验收状态以 v0.3 需求文档为准。

最后核对日期：2026-08-31。
