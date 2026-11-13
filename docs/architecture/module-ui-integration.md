# 模块与应用协同

新算法放领域模块，UI wrapper只映射DTO。50拥有唯一XML/SLERP/矩阵数学；30不依赖60，应用将选面结果映射为30请求。

- MainWindow：当前点云/来源/revision、工作台会话和发布。
- SelectedPlaneDialog：旧六画布轮次、候选、确认快照和有界调度。
- ScanGeometrySessionV2：新扫描输入、参考匹配、逐帧审核、结果修剪与几何保存；唯一保存入口saveGeometry，详细约定见[接口说明](../development/scan-geometry-interfaces.md)。
- StepTemplateBuilderSessionV1：STEP模型/完整采样/角色/修剪/草案确认。
- pcv_selected_plane_tasks：共享读取/坐标转换、完整索引、选面快照、任务身份校验。
- pcv_workbench：工作台应用与测试共用的对象库，资源和头布局保持一致。

worker仅经const QVector和at()/->at()读取共享数据，显式只读捕获；并行返回私有结果，全join后确定顺序提交。GUI线程独占Widgets/OpenGL。任务检查revision、稳定ID、token和kind；过期结果不得改变当前队列、预算暂停或确认状态。

所有代码更新同步当前文档与target.md，详细记录追加[变更记录](../archive/changelog.md)。兼容头已迁移至模块canonical include，数据格式、namespace、默认值不随路径整理修改。
