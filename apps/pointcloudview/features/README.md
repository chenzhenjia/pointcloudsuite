# pointcloudview features（UI 同事工作区）

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

此目录由 UI 同事负责视觉重设计和新业务页面。新增 feature 必须通过稳定 adapter/API 接入，不能直接依赖模块 `src/` 或应用 processor；本轮不接管 `pointcloudstitch` 视觉。交接包格式、文件白名单、owner、创建日期、删除条件和回滚点见 `docs/architecture/module_ui_integration_v0.4.md`。最后核对日期：2026-10-09（文档静态核对）。

## 所有权与接入

UI/外部项目 owner 为于海军，后端/公共 API owner 为陈振佳。UI 白名单为本目录、交接登记的专属 .ui/资源/样式、UI 测试与交互说明；MainWindow 壳、任务生命周期、模块依赖与 modules/** 归主线。

页面采用独立 feature 组件与 adapter；QWidget/QDialog 形态、刀路 DTO/schema 版本及外部项目调用方式在交接包中冻结。扫描规划属于现有 60 能力，不等于外部刀路算法完成。

交接包与白名单以 [v0.4 协同文档](../../../docs/architecture/module_ui_integration_v0.4.md) 为准，目标与更新记录见 [target.md](../../../target.md)。本次仅文档静态整理，未新增页面/adapter 或运行 GUI。
