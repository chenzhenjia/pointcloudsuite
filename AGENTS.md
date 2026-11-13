# PointCloudSuite Agent 工作手册

默认中文沟通；代码、命令、变量、路径、字段和错误保留原文。只处理明确任务，简单稳定优先，保留已有未提交/未跟踪修改。删除、覆盖、批量修改、上传、账号/数据库操作须明确授权；不得写入凭据，不得破坏性Git回退。

源码、公共头、CMake、测试是实现事实。当前规范从[文档索引](docs/README.md)进入；[需求](docs/requirements/requirements.md)、[架构](docs/architecture/architecture.md)、[契约](docs/contracts/data-contracts.md)、[协同](docs/architecture/module-ui-integration.md)。文档版本、软件版本与schema独立。

模块10 IO/相机/过滤/事务；20渲染；30拼接；40模板定位后端；50唯一坐标数学；60几何/工件/扫描/纹理/模板。30→10/50，60→10/50，40→50/60，20→core。领域不依赖apps，只有20可用Widgets/OpenGL。算法放模块src，wrapper仅适配DTO。

保留三个GUI及depth_raw_to_ply_v1。CLI、配准诊断、旧registrationdialog和旧STEP匹配页面已退出；不恢复旧兼容target/header。40有定位实现但没有GUI入口，机器人移动确认及90/100未实现。

位姿[X,Y,Z,A,B,C]，Rz(C)*Ry(B)*Rx(A)；T_base_depth=T_base_flange*T_flange_depth。LineProfileXz=[X,0,Z]，负行程合法，法向只旋转。已robot_base选面不再转换，不做翻转镜像补丁。PLY/JSON/PCVMAP/schema/默认值不可随命名整理改变。

新正式输出通过10 ArtifactTransaction；旧writePlaneOutput仍有独立回滚。失败/取消不发布部分结果，磁盘提交和GUI发布分别处理。所选平面warn_only，formal_output只表示事务成功。Mask为Grayscale8 0/255，纹理严格row-major来源映射。

worker只读const Qt容器，经at()/->at()访问；显式只读捕获，全future join后确定顺序提交。并行变更覆盖额外共享副本、串行和1/2/4任务等价、取消和join。GUI线程独占Widgets/OpenGL；关闭先cancel/join，过期revision/id/token/kind不得发布或改变当前队列状态。

开发先git status --short，阅读公共头/CMake/测试。公共头、UI、CMake或输出变更要求源码树外Debug完整构建/CTest，发布补Release；真实GUI/相机/机器人/生产样本单独验收。用户明确暂缓时遵从，不能把历史测试当本轮通过。

所有代码更新同步当前模块/协同文档，target.md登记摘要，详细记录只追加[变更记录](docs/archive/changelog.md)，包括日期、owner、API/行为、验证、风险和回滚。交付git diff --check和git status --short，报告已有修改/任务增量/未跟踪/删除/未提交。Windows优先NMake，拒绝JOM，保留VSLANG与头依赖护栏。构建、缓存、日志、导出均在源码树外，支持Unicode路径。

历史规则和验收记录见[历史开发](docs/archive/historical-development.md)，不作为当前暂停测试或临时权限指令。当前用户已授权按结构统一计划迁移和删除明确废弃内容；不提交/push/上传。
