# pointcloudview adapters

> 文档状态：当前参考。整理日期：2026-10-09（仅静态核对）。规范入口：[pointcloudview_v0.4.md](../../../docs/requirements/pointcloudview_v0.4.md)；总索引：[docs/README.md](../../../docs/README.md)。当前实现与目标状态分开记录，带日期的构建/测试结果仅对应当次变更。

临时 adapter 仅允许放在此目录，并在文件头标记 `Experimental` 或 `Deprecated`。每个 adapter 必须注明 owner、创建日期、替代 API、截止版本、删除条件和回滚点；不得成为模块间长期依赖。交接时必须附 API 依赖表、构建命令、测试结果、已知问题和回滚点。最后核对日期：2026-10-09（文档静态核对）。

## Adapter 登记与退出条件

每项登记文件路径、owner、创建日期、DTO/API 版本、Experimental/Deprecated 状态、实际使用的旧入口、替代稳定 API、截止版本、删除条件、验证与回滚点。只转换输入参数/结果状态，不复制坐标、配准、几何或文件提交算法。

后台结果须保留错误码/cancelled/诊断；UI 发布前核对 revision，关闭先取消再等待。namespace/类型迁移未实现前登记当前实际接口，不依赖虚构的新 namespace。

交接包与白名单以 [v0.4 协同文档](../../../docs/architecture/module_ui_integration_v0.4.md) 为准，目标与更新记录见 [target.md](../../../target.md)。本次仅文档静态整理，未新增页面/adapter 或运行 GUI。
