# stitching_ui：共享拼接窗口

v0.5，2026-10-15。target pcv_stitching_ui为应用层静态库，View/Stitch共用StitchingWindow和ui/stitchingwindow.ui。链接Qt Core/Gui/Widgets/Concurrent、core/infrastructure、10/30/50；领域模块不得反向依赖此库。

共享输入/排序去重/整行位姿移动、普通/高级参数、日志/进度/取消/覆盖确认；只调用pcv::interface::StitchingRequest/stitch。seam距离/决策块与并行设置透传，输出不覆盖输入。

StitchingPublication包含全量点云、formalPly/previewPly/mappingFile/reportJson/outputDirectory，仅成功事务后发出stitchingSucceeded。View子窗关闭隐藏，主窗关闭取消/join；独立Stitch退出取消/join。失败取消不替换旧会话，成功按应用revision门禁发布。

主线维护窗口、线程和依赖；UI feature/外部项目接入遵循[协同规范](../../docs/architecture/module_ui_integration_v0.5.md)。[架构](../../docs/architecture/software_architecture_v0.5.md) · [测试与历史](../../docs/changelog/v0.5.md)。本轮未做GUI验收。
