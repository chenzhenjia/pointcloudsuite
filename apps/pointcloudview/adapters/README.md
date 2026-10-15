# pointcloudview adapters：临时接入边界

v0.5，2026-10-15。临时adapter只在此目录，标Experimental或Deprecated，登记owner、创建日期、依赖、替代公共API、删除条件和回滚点。[协同规则](../../../docs/architecture/module_ui_integration_v0.5.md)。

adapter仅做UI/外部输入输出映射，不能成为模块间长期依赖或复制算法。主线负责生命周期与依赖，同事按交接白名单开发feature和外部项目。契约/页面/调用方式未冻结前只登记需求，不发明正式schema。

迁移到稳定API后，在引用清理及回归完成时独立清理adapter；保留用户修改。更新记录集中[版本日志](../../../docs/changelog/v0.5.md)，target追加摘要。
