# 六分屏选面、完整点云提速与模板保存读取 v2

日期：2026-10-16。owner：Codex/主线。状态：源码修正与回归源码已补，本轮仅静态检查，未构建、未运行 CTest/GUI/性能测试，未更新可执行程序。用户执行动态验收。

本文是当前整合方案。保留 [整合 v1](batch_plane_selection_template_integration_v1.md) 和 [当前帧展示 v2](current_frame_display_acceleration_v2.md) 历史；不恢复已经撤回的模块 30 配准缓存或结果预览降采样。

## 要求—实现—差异—修正审计

| 要求 | 本轮开始时的实现/差异 | v2 修正或保留 |
|---|---|---|
| 未展示帧优先，用户请求优先 | 默认活动格为0，可能在其他帧加载前建立该格索引；其他格的选面请求可能排在后台准备后 | 活动格初始/换轮为-1；真实鼠标/滚轮、表格点击或放大激活；所有完整选面请求先行，随后活动格准备、原序加载、其余索引 |
| 过期结果不影响当前会话 | 迟到 OOM 在 revision 判定前暂停调度，结果分支也会清队列/种子 | 帧指针归属、稳定id、revision、任务kind、令牌门禁先行；过期只回收旧Job，不应用载荷或清除新请求 |
| 全部提交后可拼接 | 额外限定当前轮必须最后一轮 | 移除该限制，所有帧完成、无草稿/任务且至少两帧保留即可 |
| 提交跳过后释放，撤销重载重选 | 已释放确认快照，但末轮仍保留完整帧/索引 | 提交跳过同时释放整帧/索引/展示；撤销重新排队加载，不自动恢复旧候选；未提交跳过继续保留旧确认 |
| 独立阶段诊断 | 解析/归并/投影/快照不独立，队列由总耗时相减估算 | 用实际排队起点至worker开始测量；独立记录worker、GUI发布等待、解析、归并、投影、普通连通、跨带、快照 |
| 完整数据投影并行 | 投影仍为串行 | 同一逐点表达式、连续65536点窗口、私有结果、全join后原序归并；预算不足或线程创建失败串行处理剩余区间 |
| 模块30保持完整路径 | 开始时四个模块30文件及对应测试仅status标M，内容忽略换行与HEAD相同 | 不改30求解/API/正式文件/manifest，不添加previewPoints；结果仍setCloud(result.points) |
| 模板完整保存和读取 | 已接入现有模板表单与60保存，主窗口已有读取 | 保留应用编排，不增加30→60依赖；完整PLY+JSON、robot_base/mm、独立版本目录 |

## 当前流程与状态

添加有序PLY、Start/End及共用手眼XML后，一键加载当前轮，最多六格（三列两行）。完整读取和完整转换后发布最多300000个均匀实测展示点，再后台建完整索引。每格独立视角、同一画布放大/返回。完整索引就绪后单击；索引失败/取消可重试或使用完整数据三点拟合。

抽样为 M=min(N,300000)：M=1取0，其他使用64位整数 floor(k*(N-1)/(M-1))，小帧完整显示，覆盖首尾。展示映射指向完整帧下标；完整候选只在展示子集高亮，统计仍为完整点数。没有临时选面算法或降采样正式候选。

每格有有效完整候选、有效已确认选择或明确跳过，且取点/任务结束，才开放整轮确认。提交后进入下一轮，末轮不自动拼接；可返回已确认轮检查/重选。离开未提交草稿会询问是否放弃。所有轮次完成、无未确认修改且至少两帧保留时，在任何轮次均可手动拼接。

容差/跨缺失距离/方式修改仅失效未确认候选及种子，已确认快照保留；位姿使对应帧数据/选择失效，标定使相关帧失效。处理中禁止文件重排和位姿/标定编辑。单格取消只取消该格；换轮先取消全部相关任务再等待；关闭等待全部worker。新队列不得被旧完成回调清除。

跳过不删文件，不生成空平面；未提交时旧确认可恢复。提交跳过后释放旧快照和当前帧缓存，撤销后重新加载并选面。跳过中间帧可能减小重叠，模块30仍按既有规则回退位姿。

## 完整处理优化与接口

[完整处理提速 v3](full_resolution_processing_acceleration_v3.md) 定义10/50/60的可选优化和精确对照；[六分屏 v1](batch_plane_selection_six_views_v1.md) 定义会话与用户步骤。

- 10：四参readPlyCached的CloudCachePolicy关闭本入口canonical创建/比较；默认三参不变，canonicalReady=false；完整SHA-256、头/声明数/缓存载荷校验、分块取消保留。
- 50：CloudTransformOptions兼容追加样本收集/并行/预算；完整逐点数学相同，原序归并。RobotCloudResult末尾新增mergeMs、actualWorkerCount，仅用于诊断。
- 60：IndexPreparationOptions兼容追加网格归属缓存及法向并行；SingleClickResult末尾新增projectionMs、normalConnectivityMs，既有gapMergeMs保留。邻域/PCA/RMS顺序、六轮、容差和跨带规则不变，投影按完整索引原序归并。
- 应用：SelectedPlaneTask增加解析、归并、投影、快照与worker开始/完成时刻；Job保存稳定id及排队时刻。新增计时仅会话日志，不加正式manifest字段。

共享256 MiB预算只约束当前轮新增性能存储和活动临时数组；双帧任务各最多2计算worker，只有单任务可用4，四worker任务运行时新任务等待。预算不足缩窗口/减worker/回退串行，展示预算或实际OOM暂停新增任务，重试/跳过恢复。全部完整业务数据、原有索引、确认快照、Qt/allocator管理及GPU不是该预算的硬限制。六个600万点基础数组约1.3 GB，另需索引和候选内存。

## 完整结果与模板

未跳过确认快照按原列表顺序交模块30，PLY点数等于保留快照点数之和，PCVMAP来源和点序保持，结果画布全量显示，不新增preview PLY。

检查结果后点击“保存为模板”，复用名称/根目录/基准确认表单，应用后台调用saveWorkpieceTemplate。模板沿用pcv-workpiece-template-v1、robot_base/mm和完整Binary Little Endian PLY+JSON。来源记录参与文件、跳过清单、原报告；PCVMAP/报告不复制进模板包。修改输入使旧结果过期并禁止保存。失败/提交前取消保留完整结果可重试；事务提交后迟到取消仍按已提交成功处理。

主窗口“打开工件模板”复用loadWorkpieceTemplate，完整读取且不再次转换。模板整体搬迁、原始文件和拼接目录不在时仍可打开。失败/取消/过期保留原主窗口数据。不新增匹配、定位或移动判定。

## 用户验证与交付界限

回归源码：cloud_cache_tests；handeye_transform_tests；single_click_plane_selection_tests_v2；selected_plane_dialog_tests_v1；既有模板接口/主窗口测试。新增针对默认格调度、真实激活优先、选面优先、旧轮可拼接、revision/id/token/kind过期OOM保留新请求、102400点投影串行/1/2/4与零预算/16MiB一致的回归。所有本轮测试均未执行。

请按 [Windows构建](build-windows.md) 在源码树外新建Debug/Release目录完整构建并执行CTest；公共头已变化，不复用旧对象。配置实际Qt/MSVC路径后，命令示例：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

真实验收：1/6/7/12/13帧，末轮不足六格、独立拾取/视角、放大恢复、返回旧轮、单击/三点、重选失败保旧确认、参数/位姿/标定失效、跳过与撤销、取消/关闭、完整PLY/PCVMAP及模板往返/搬迁/坏包。必须实际验证旧轮可拼接和提交跳过后重载重选。

分别记录冷/缓存加载、首格/整轮首显、各格可选面、点击至完整候选及首个frameSwapped高亮、整轮人工耗时、模板读写、进程峰值内存。日志中的派发前排队与最终实际排队不同：后者包含Qt线程池等待；GUI发布等待由worker完成至GUI回调测量；首显/首高亮仍等待实际绘制revision。没有候选点不能报告高亮成功。优化预算预留不等同进程RSS。

同输入/工具链/配置比较完整坐标、法向、ratio/source、过滤统计、索引集合、模型/RMS及迭代/跨带结果；耗时字段不比较。任何对照失败的优化不得验收启用，先回退对应可选路径并修复。串行路径和优化路径逐点计算体相同，另需与审计确定的v2完整路径对照；不要把未经核实的HEAD当完整v2基线。

本轮只运行静态差异、UTF-8、声明/定义和本地链接检查，结果另记changelog；这些检查不证明编译、结果等价或提速。未承诺固定秒数、加速倍数、识别率或生产质量。

回滚仅人工撤回本轮v2增量，保留既有工作区/v1/v2历史与画布映射；不整体覆盖，不修改原始数据，不提交/推送。六帧内存、六OpenGL画布、完整读取/连通和全量结果上传成本仍需用户实测。

计时口径补充：既有connectivityMs保留包含投影的历史总口径；新增projectionMs与normalConnectivityMs/gapMergeMs独立定位阶段，不可将重叠指标重复求和。

后续构建/运行复验（2026-10-16）：按用户新指令修复模块60编译冲突和应用资源配置后，已完成全新Debug/Release全量构建、各43/43 CTest、原生六画布/完整结果绘制及两个GUI启动。上文“未执行”是本方案静态交付时的历史事实；真实大样本/性能仍待验收。新程序和日志见[修复说明](build_runtime_repair_v1.md)。
