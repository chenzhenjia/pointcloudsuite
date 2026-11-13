# 公共头

根include/pcv仅保存core/infrastructure；业务公共头位于各modules/<module>/include/pcv。CMake通过对应target提供头路径，旧转发头已迁移删除，namespace/DTO/schema保持不变。

[架构](../docs/architecture/architecture.md) · [迁移表](../docs/archive/document-migration.md)
