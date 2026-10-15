# v0.5 文档交付与后续软件开发方案

日期：2026-10-15。[当前架构](../architecture/software_architecture_v0.5.md) · [需求](../requirements/pointcloudview_v0.5.md) · [版本日志](../changelog/v0.5.md)。

## 本轮实施边界

只整理文档，以 HEAD 75d3e3b 加完整工作区为准；建立需求/架构/契约/协同/日志/归档及迁移表，重整 target 和 AGENTS，先迁移独有内容再删除 18 份重复 Markdown 和 1 份旧 txt。旧需求版本及 v0.4 协同保留短指引，不删除源码兼容层。

不修改源码、CMake、schema、运行版本或数据，不运行构建、CTest、GUI、性能测试，不提交/推送/上传。当前主文档只记录实现与缺口；历史测试/合成基准带日期，不能借用为本轮通过。

## P0：构建边界收敛

主线负责。静态检查发现 30 的 add_subdirectory 条件为 Stitch OR Tools，而 View/共享 UI 和部分测试依赖 30；CLI-only 的兼容聚合/无条件测试注册也需检查。根注释“固定入口”与实际选项分支并不等价，旧文档“CLI-only 独立配置成功”不能直接沿用。

后续先用全新源码外目录复现组合，再修正 target 创建和测试条件：双 GUI；View-only；Stitch-only；CLI-only；Tools-only；各组合 Tests ON/OFF；实验相机 ON/OFF（SDK 实际可用性单列）。验收为目标存在、链接正确、CLI 不链接 Widgets/OpenGL、无 apps 反向依赖。未复现前不宣称根因全部确认，修复不得强制打开用户关闭的 GUI。

保留 C++17、Qt 6.5+ 与根 CMake 3.19 最低声明；CMakePresets.json version=6 需要支持该格式的较新 CMake（3.25+），命令行最低版本和 preset 使用条件分开说明。NMake/VSLANG/依赖数据库护栏保留。

## P1：现有主流程与真实性能验收

后端陈振佳与前端/外部项目于海军共同验收固定样例，记录输入 SHA256、标定、Start/End、选点/点击索引、参数、程序基线及输出。没有真实控制点或位姿时不猜测。

| scenario | acceptance |
|---|---|
| 整帧配准/仅转换 | 输入顺序与位姿联动、参数透传、来源一致、质量门禁、事务完整 |
| 单击/三点选面 | 预览确认、自动下一帧、重选保留快照、参数失效、跨带阈值及共面近邻风险 |
| 所选平面拼接 | 与相同所选点直拼对照，法向间距/残差/双层、累计与回退可追溯 |
| 会话与任务 | 失败/取消保留旧会话，成功清派生状态，revision 过期不发布，关闭 join |
| 输出 | 覆盖保护、取消/失败回滚、PLY/PCVMAP/manifest 一致与输入保护 |
| 性能 | 索引准备、模块耗时、拾取至首个完整高亮分别计量，预热一次正式五次 |
| UI/内存 | 最小窗口、100%/150% DPI、同 PID 读数、提示共存与真实数据操作 |
| 纹理/扫描 | row-major、灰度/Mask、单线容差、P.json 局部坐标与方向 |

单击真实点击至完整显示 2 秒仍是未验收目标。历史样本1的选面重叠法向间距 ≤0.1 mm 是特定离线对照目标，不新增为通用运行门槛。性能不允许通过截断实测点集伪装达标。

代码变更后独立做 Debug/Release 构建、全量及定向 CTest、启动、GUI/真实数据；Qt 共享副本串行与 1/2/4 任务等价、取消/join 不得丢失。每次只报告实际执行结果；PageHeap、便携部署和相机现场另外记录。

## P2：公共 API 与兼容收敛

先提供不复制算法的 pcv::io/stitching/coordinates/geometry/workpiece 门面，逐批迁生产调用。共享 UI 已用分组请求，RegistrationDialog 已退出编译，不把迁移废弃页面当生产任务；诊断工具仍需切换旧 Options/target。

将分组请求成为主实现、旧 Options 反向适配列为独立任务；逐字段对照旧 allowOverwrite=true 与新 false、seam、preview、诊断、并行/进度/取消，保持输出契约。旧 writePlaneOutput 的内部事务实现后续再复用 ArtifactTransaction，保留旧 JSON 差异及覆盖/回滚语义。

保留应用轻量 launcher 与 ViewLegacy 私有类型边界，避免同名不同布局 DTO 再次冲突。每批有旧新 header/target/namespace、调用方与回滚映射；不重命名历史字段或回滚后重新启用 PoseOverlapFine/2.5D。

兼容删除必须引用清零、弃用期、独立评审及构建/CTest/GUI/现场条件满足；与迁移分开授权，不执行破坏性 Git 回退。

## P3 与远期

先由双方冻结高点标注/全剖/点剖的文件契约、坐标与图片关系、DTO/schema 版本、调用方式、UI 形态、错误/取消和固定样例，再联调。未冻结只登记，不发明算法或正式字段。40/90/100 见[路线图](roadmap_v0.5.md)。

## 文档验收与回滚

每份原文档均在[迁移表](../archive/document_migration_v0.5.md)登记；删除前内容和证据已迁移。检查本地链接/锚点、单一级标题、围栏、UTF-8/替换字符、空文档、旧入口及过时能力声明。当前规范不含历史实现残留，历史区明确日期/替代关系。

实施前后对非文档文件做 SHA256 对照，保护已有工作区修改；git diff --check 只作为空白验证，不能代替内容审计。回滚参考为基线 diff、迁移记录和历史档案；旧路径还原须人工合并后续修改，不使用 reset --hard 或 checkout --。
