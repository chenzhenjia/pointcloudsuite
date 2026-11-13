# PointCloudSuite 当前目标

2026-11-13：按用户确认的轻量化与结构统一计划实施。保留三个GUI、核心业务、40定位后端、RAW转换和已有数据兼容；删除CLI/配准诊断、失效页面及兼容转发。共享选面任务稳定命名，工作台对象库避免重复编译，测试按能力分层，docs当前与历史分离。

owner：应用/构建与记录由本轮主线维护，领域边界保持10/20/30/40/50/60。详细结果见[变更记录](docs/archive/changelog.md#structure-optimization)，文件去向见[迁移表](docs/archive/document-migration.md)。

本轮源码树外 Debug/Release 全量构建成功，CTest 各56/56通过；三个单GUI、RAW-only、STEP/相机关闭共五种独立配置/构建全部通过。Debug/Release 原生六画布拾取通过，关闭能力定向测试3/3、可选SDK假符号启动测试1/1通过。

三个EXE自动启动/退出已纳入CTest；相机真机、机器人、生产样本、DPI人工视觉和干净机部署仍未验收，不以smoke替代。已有未提交构建/掩码修复保留。未提交、未推送、未上传。

2026-11-13 scan_geometry接口整理：按职责统一会话声明/实现及数据字段注释；删除confirmGeometry转发方法和无调用的processingBudget getter，GUI/测试统一调用saveGeometry，UI对象名不变。算法函数体和数据布局保持；不设计主项目接入。集中[接口文档](docs/development/scan-geometry-interfaces.md)已建立。本轮Debug/Release全目标clean-first重建成功，四项扫描定向CTest各4/4、完整CTest各56/56、原生六画布拾取均通过，详细命令/日志和限制见[本轮记录](docs/archive/changelog.md#scan-geometry-interfaces)。

2026-11-13版本归档：用户授权提交当前全部项目变更并上传GitHub与Gitee的develop。Gitee使用shanghai-senjing/point-cloud-processing，提交身份陈振佳。实际提交版本及远端核对结果见交付回复；本次不修改软件版本或schema。
