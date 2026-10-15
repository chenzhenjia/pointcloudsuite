# PointCloudSuite Agent 工作手册

适用本仓库，更新 2026-10-15，文档/架构 v0.5。[索引](docs/README.md) · [需求](docs/requirements/pointcloudview_v0.5.md) · [协同规范](docs/architecture/module_ui_integration_v0.5.md)。本次经用户明确授权重整原正文与历史区，历史迁往集中归档。

## 沟通、范围与安全

默认中文，代码、命令、变量、路径、字段和错误保留原文。只处理用户明确指定任务/文件，简单稳定优先，不做无关重构或过度防御。报告做了什么、改了什么、如何验证和剩余风险。

保留已有未提交/未跟踪修改，不擅自覆盖；删除、覆盖、批量重命名/修改、上传、账号/数据库操作须有明确授权。不得写入凭据。禁止 reset --hard、checkout -- 等破坏性回退；用户仅要求本地提交时不 push。缓存、日志、构建、导出与运行数据必须在源码树外，Windows 路径支持 Unicode。

## 真相源与模块边界

源码、公共头、CMake、测试是实现事实；当前规范为 v0.5 需求/架构/契约/协同文档，旧版本短页只指向当前和历史，不再定义现行规则。文档版本不等于 CMake PROJECT_VERSION，不能自行升级软件或 schema。

| module | target | responsibility |
|---|---|---|
| 10 | pcv_m10_pointcloud_io | PLY/缓存、相机实验输入、过滤、writer/事务 |
| 20 | pcv_m20_pointcloud_render | 画布、OpenGL、拾取、渲染 DTO |
| 30 | pcv_m30_pointcloud_stitch | 整帧/所选平面拼接、诊断与输出编排 |
| 40 | pcv_m40_pointcloud_matching | INTERFACE 聚合 30/60，位置确认业务未实现 |
| 50 | pcv_m50_coordinate_conversion | 唯一手眼/位姿/坐标数学 |
| 60 | pcv_m60_geometry_workpiece | 平面/单击/边缘/ROI/WObj1、临时工件、扫描/纹理 |

30→10/50，60→10/50，40→30/60，20→core；70/80 兼容转发 60/10。共享模块不得依赖 apps；20 是唯一允许 Widgets/OpenGL 的领域模块，apps/stitching_ui 是应用层共享库。core/infrastructure 位于基础层。40/90/100 不得写成功能已实现。

算法实现放模块 src，公共头 include；wrapper 只做参数/结果映射。50 是唯一坐标数学，不复制 XML/SLERP/矩阵转换。30 不新增 60 依赖，应用负责选面 DTO；MainWindow 壳、任务生命周期、共享拼接 UI 和模块依赖由主线维护。同事白名单、临时 adapter 与交接冻结按协同规范执行。

## 稳定契约与线程

公共类型、输出、坐标/错误码变更前阅读[数据契约](docs/contracts/data_contracts_v0.5.md)及对应模块文档；既有 JSON/PLY/PCVMAP、schema 名称和默认值不得随命名整理改动。新命名规则及实际 namespace 债务见协同规范，不把头路径当 namespace。

位姿 [X,Y,Z,A,B,C]，A=Rx/B=Ry/C=Rz，Rz(C)*Ry(B)*Rx(A)；RTmatDepth2robot=T_flange_depth，T_base_depth=T_base_flange*T_flange_depth。LineProfileXz 为 [X,0,Z]，Y 仅作 y/signedTravel；负行程合法，法向只旋转不平移。禁止固定翻转/镜像补丁，已在 robot_base 的选面请求不再转换。

新业务正式输出经 10 ArtifactTransaction，30/60 可编排事务；旧 writePlaneOutput 在 10 内仍有自身回滚实现，收敛属于后续任务。Mask 为 Grayscale8 0/255，显示亮度 0～255。取消/失败不发布部分结果，已提交文件与 GUI 发布分别处理，不复用旧结果假装成功。

worker 只经 const QVector 视图、at()/->at() 读取共享 Qt 容器，捕获列表显式只读对象；禁止非 const operator[] 触发 detach。worker 返回私有任务结构，全部 future join 后按确定顺序提交。新增并行路径保留 refcount>1 的额外共享副本，覆盖串行和 1/2/4 任务等价、取消与 join 生命周期。GUI 线程独占 QWidget/OpenGL；关闭先取消/等待，revision 过期结果不发布。

平面拒绝重复/越界/非有限/共线控制点；渲染拒绝非法点/坐标/索引；来源映射与点序保留。seam 默认 8/0.6/0.5 mm。所选平面 warn_only、formal_output 仅为事务成功，不宣称严格质量通过。单击真实端到端性能不由合成模块基准替代。

## 开发、验证与记录

1. 先 git status --short，记录工作区及主责模块/应用层，阅读公共头、CMake、测试和当前文档。
2. 明确接口、依赖和影响面，只改授权模块及必要跨界文件；并发修改先重读合并。
3. 按风险运行定向测试/构建；涉及公共头、CMake、processor、UI 或输出时独立执行源码树外 Debug 完整构建/CTest，发布变更补 Release。用户明确暂缓时遵从，并标明未运行。
4. 真实 GUI、相机、机器人、生产样例和部署分别验收，不由单元测试替代。只报告实际命令/结果，历史测试不得充当本轮通过。
5. 所有代码更新必须同步模块当前文档、协同映射；在 target.md 登记简要摘要，详细记录只追加 docs/changelog/v0.5.md，包含日期、owner、文件/API、行为、验证、风险和回滚点。
6. 交付前 git diff --check 与 git status --short，报告已有修改、任务增量、未跟踪/删除及未提交状态。

推荐构建工具及命令见[Windows 构建](docs/development/build-windows.md)。C++17、Qt6.5+、MSVC；根 CMake3.19+，preset version6 另要求3.25+。优先 NMake Makefiles，避免 JOM；VSLANG/头文件依赖护栏保留，公共类型修改后全量重建。GUI/CLI/Tools/Tests 组合存在待验证项，不沿用“固定开启 GUI”等旧文字。

## 兼容清理与本轮例外

旧 target/header/namespace 不新增重复实现；保留兼容周期。删除前须生产和工具引用清零、公共 include 迁移、无重复算法/流水线、构建/CTest/GUI/现场验收及弃用期或明确批准；删除独立评审/提交，不与迁移混合。

本次用户已授权按 v0.5 清单重写文档、合并历史、删除18份重复 Markdown 和1份 txt，源码兼容层不删。本轮只静态审计，不构建/CTest/GUI/性能测试、不提交/push/上传。[详细结果](docs/changelog/v0.5.md#documentation-audit)与[文档迁移表](docs/archive/document_migration_v0.5.md)为本次记录；先前规则和变更可在历史归档追溯。
