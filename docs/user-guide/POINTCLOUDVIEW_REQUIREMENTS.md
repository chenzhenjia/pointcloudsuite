# pointcloudview 使用与验收说明

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

## 应用职责

`pointcloudview` 用于加载 PLY 点云、渲染和点选、平面拟合、工件坐标系建立、ROI/模板处理以及 PNG/PLY/JSON 输出。

## 界面文件

Qt Designer 文件位于：

- `apps/pointcloudview/ui/mainwindow.ui`
- `apps/pointcloudstitch/ui/stitchingwindow.ui`

`PointCloudCanvas` 的实现位于 `modules/20_pointcloud_render/src/pointcloud_canvas.cpp`，应用只负责异步任务、关闭状态、业务流程和 DTO 转换。

## 处理流程

输入校验 → 10 PLY 读取/缓存/预处理 → 20 渲染和点选 → 50 坐标转换 → 60 平面/边缘/ROI/WObj/临时工件 → 10 事务成套输出。40 的工件位置确认仍为规划占位。

后台线程不得访问 QWidget/OpenGL；窗口关闭时应丢弃过期结果并等待任务结束。

## 当前流程的使用边界

- 单个 PLY 临时工件需要手眼 XML、Start/End 和两组三点；转换到 robot_base 后才能建立 WObj1 和正式四件套。文件夹预览不等同于该工作流。
- 第五步扫描规划支持 Global/LocalRoi，默认局部 ROI；高度/相机宽度/重叠默认 50/100/50 mm，两端各延长 50 mm，单线容差为 0.1 mm。规划结果可导出项目 JSON 或 InoRobot P.json，但不控制机器人。
- 亮度纹理必须使用与原始 PLY 一一对应的同次扫描图像，像素数等于原始点数、左上角 row-major；过滤、降采样、拼接或缺少原始索引的点云不可使用该入口。
- “PLY 纹理实验（原始坐标）”不做手眼转换、不建 WObj1、不产出四件套；实验坐标不能误标为 robot_base。WObj1 修改不使严格栅格纹理失效，点云/平面索引变化会清除旧纹理。
- 扫描规划、纹理显示、外部全剖/点剖刀路和未来 40 移动判定应分别展示能力状态；外部刀路算法和移动判定尚未在本仓库完成。

## 输出契约

正式输出使用 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。PNG、PLY、JSON 必须成套提交，任一失败都要回滚。

## 验收边界

自动化测试不能替代 Windows 桌面点选、矩形选区、视角交互、关闭生命周期和真实生产数据验收。架构/UI 交接以 `docs/architecture/module_ui_integration_v0.4.md` 为准，JSON/PLY/坐标契约以 v0.3 需求文档为准。

最后核对日期：2026-10-09（文档静态核对）。
