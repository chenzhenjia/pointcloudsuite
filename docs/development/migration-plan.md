# 结构迁移与回滚

三个GUI可执行文件名称、已有DTO/schema与坐标数学保持不变。共享选面任务文件与target改为selected_plane_tasks/pcv_selected_plane_tasks；工作台源码共用pcv_workbench对象库。

兼容include迁移到所属模块canonical header，移除转发头和pcv_m10_pointcloud_read。CLI/配准诊断与专属脚本退出。旧选面/模板页面源码无生产引用后删除，模板定位后端继续保留。

文档当前路径稳定，历史完整正文集中archive；测试按能力命名与分层。迁移表见[清单](../archive/document-migration.md)。源码外审计快照位于D:/pcv-build/structure-optimization-20261113，包含迁移前docs/tests/scripts与差异；不得用整体reset覆盖已有修改。

回滚须按迁移表逐项还原并合并后续修改，保留数据和已提交事务，不删除旧构建目录。
