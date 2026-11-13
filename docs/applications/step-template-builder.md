# step_template_builder_v1 开发说明

入口：`step_template_builder_v1`；共享依赖：`pcv_step_template_builder_view_v1`。

StepTemplateBuilderSessionV1组织STEP导入、完整采样、工件/干涉面、贯穿修剪、掩码处理、用户确认与模板草案。默认0.5mm，完整采样最多1000万点，展示最多100万。

GUI仅处理会话与参数映射，领域算法归模块。输入/参数变化使派生结果失效，worker只接收不可变快照，取消/关闭等待join。

[使用指南](../user-guide/step-template-builder.md) · [构建](../development/build-windows.md) · [契约](../contracts/data-contracts.md)
