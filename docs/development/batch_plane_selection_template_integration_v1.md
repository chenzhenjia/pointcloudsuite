# 六分屏选面、完整处理提速与模板保存读取 v1

日期：2026-10-16。owner：主线/Codex。实现处于未构建、未运行验收状态；本轮仅静态检查，构建、CTest、真实 GUI、数据与性能由用户执行。本文整合并替代单帧交互部分的 [当前帧展示 v2](current_frame_display_acceleration_v2.md)；错误方向的 [拼接结果预览 v1](selected_plane_stitch_preview_acceleration_v1.md) 保持撤回，不重新启用。

## 操作流程

1. 打开“所选平面拼接”，添加有序 PLY，填写每帧 Start/End 和共用手眼 XML、源码树外输出根目录。
2. 点击“一键加载本轮 / 重载”。按文件顺序每轮最多六帧，三列两行；最后一轮不足六格时只显示实际帧。
3. 完整读取、robot_base 转换后，每格发布最多 300,000 个展示点，后台准备完整选面索引。各格独立旋转、平移、缩放，“放大/返回”移动同一画布布局，保留视角，不重载。
4. 单击平面内部，或使用三点备用。拾取回译完整下标；算法与快照使用完整点云。“候选点数”表示完整数量，未展示的点不补画。
5. 每格有完整候选、已有有效确认或明确跳过，且本轮任务和取点均结束后，点击“确认本轮 / 下一轮”。跳过可撤销，不删原文件；旧确认快照保留到提交跳过时，之后撤销跳过需重新选择。
6. 返回上一轮时重新加载其未跳过的完整帧与索引，恢复已确认高亮。换轮只保留确认快照和必要状态，有未确认草稿时，离开/重载会询问是否放弃；可先确认或取消。所有轮次处理完、没有待确认修改且至少两帧有效时，手动拼接。
7. 在独立结果视图检查完整结果。点击“保存为模板”，填写名称/根目录并勾选现有基准确认项；后台事务保存。关闭此窗口后通过主窗口“打开工件模板”选择模板 JSON，完整读取。

## 数据边界和模块职责

| layer | implementation | data_contract |
|---|---|---|
| 应用 | `selectedplanedialog.{h,cpp}` | Frame 稳定 id/revision，六格、两 Job、整轮提交、跳过、独立结果与模板编排 |
| 10 | `cloud_cache.h/.cpp` | 四参 `readPlyCached(..., CloudCachePolicy)`；默认仍生成/验证 canonical PLY，批量入口显式 false |
| 20 | `pointcloud_canvas.h/.cpp` | `setDisplayCloud()`、完整下标映射、展示子集高亮；主窗口默认最小尺寸保持，批量画布单独缩小 |
| 50 | `handeye_transform.h/.cpp` | `collectSamplePoints=true`、`parallelEnabled=false`、`maxParallelTasks=4`、可选共享预算；原默认语义保持 |
| 60 | `single_click_plane_selection_v1.h/.cpp` | `IndexPreparationOptions`、可选 point→bucket 缓存、独立网格法向准备、分类/投影/距离/连通队列复用 |
| 30 | 原所选平面拼接入口 | 完整确认快照按原顺序拼接；求解/预算/事务/产物契约维持，不依赖60 |
| 模板 | 60 既有 `saveWorkpieceTemplate()` / `loadWorkpieceTemplate()` | `pcv-workpiece-template-v1`、robot_base/mm、完整 PLY + JSON，不嵌入逐点映射 |

展示采样：`M=min(N,300000)`，`M=1` 取0，否则 `floor(k*(N-1)/(M-1))`，使用 qint64 中间值，保留首尾及实际测量坐标/法向。映射指向 `fullPoints` 下标，不是 PLY 来源编号。展示点不进入确认、正式拼接或模板。结果画布使用 `setCloud(result.points)`，不生成 preview PLY，不新增正式 manifest 展示字段。

模板来源记录完整参与文件路径、跳过清单、拼接报告和处理步骤。这些路径仅作说明，搬迁模板包、删除原始输入后仍能读取；PCVMAP/报告保留原拼接目录，不复制进模板。更改选择/位姿/标定/拼接模式使当前结果过期，保存入口关闭；已保存模板版本保留。保存失败/提交前取消保留拼接结果可重试，提交后迟到取消遵从现有模块成功语义。

## 不降低精度的优化

- 完整 SHA-256 不改成 size/mtime；缓存头、源声明点数、载荷长度继续校验。指纹/缓存读写每 1 MiB 检查取消；批量入口跳过不需要的 canonical PLY 创建、完整读回和逐字节比较。保留原调用入口。
- 转换复用同一个逐点计算体，连续区间私有输出，每窗口最多 65,536 点/worker，原区间顺序归并。过滤、ratio、插值查表、矩阵/法向及点序不变；关闭未使用的 samplePoints。预算不足走直接串行追加，不分配第二份整帧。线程创建失败先收齐前序区间，再串行处理剩余区间。
- 网格编号和插入顺序不变；point→bucket 的 int 映射按预算可选。网格法向并行，各格仍按原 dz/dy/dx 顺序累计和 PCA。分类按原点序归并；保留 v2 距离、最大跨缺失距离、六轮上限、稳定停止条件及 PCA/RMS 遍历顺序。
- 连通函数只增加可选队列缓冲，不改连接/边界/跨带判定。预算不够时释放阶段缓冲或走原串行路径，不截断正式结果。

## 调度、生命周期和内存

最多两个帧级重任务，覆盖加载、索引和选面。操作格优先，其余未显示加载先于索引准备。两个帧任务时每任务最多两个计算 worker，只有一个待处理任务时最多四个；若一个四 worker 任务运行中收到新请求，新任务排队至其结束，避免超额嵌套。读取 worker 数也按同一配额设置。

256 MiB 的 `ProcessingBudget` 只计新增优化存储：展示点/正反映射按每展示点64字节预留、point→bucket int 数组、并行私有区间及跨轮迭代复用缓冲。它不是进程 RSS 硬限制；Qt/allocator/GPU 管理开销、完整业务数据、原有索引/算法基础存储及确认快照仍需额外内存。可选缓存不足则关闭，转换/分类退回串行、阶段缓冲不跨阶段保留；展示预算不足或实际分配失败暂停新任务，用户重试或跳过，不减少正式点。

六帧各约600万点，仅完整点、来源与扫描比例基础数组约1.3 GB，另有索引、候选、确认快照及GPU成本。不能把256 MiB理解为整个程序上限。

任务携带共享 Frame 身份、revision 和取消标记。单格取消不影响其他格；参数只取消当前轮识别与种子，完整加载/索引可继续；位姿使对应帧、标定使所有相关帧数据和确认失效。重任务期间禁止文件编辑/重排及位姿/标定编辑。换轮和关闭取消并等待；旧 watcher 断开后释放，过期/取消结果不发布。worker 只读 const Qt 视图，不修改 GUI。

## 诊断和用户验证

会话日志记录完整/展示点数，读取总耗时、SHA/cache read/cache write、完整转换、展示抽样、从加载开始到首次 `frameSwapped`、完整索引网格/法向准备及至可选面、点击到候选/首个高亮、排队估计和预算预留。首显检查画布渲染 revision；候选更新也推进 revision，不能把仅调用发布 API 当作实际绘制完成。模板耗时在保存/读取接口外分别测量；峰值进程内存请在相同机器用既有内存面板或系统工具记录。优化预留不等同真实峰值。

已补充但未运行的回归源码：

- `cloud_cache_tests`：批量/默认解码一致、miss/hit、canonical关闭、缓存截断重建、分块取消；原同size/mtime改内容回归保留。
- `handeye_transform_tests`：串行/1/2/4、零预算回退、额外 Qt 副本、坐标/法向/ratio/source/样本/统计精确一致及取消不发布。
- `single_click_plane_selection_tests_v2`：原v2完整性夹具保留，新增索引法向并行/映射缓存与零预算、完整索引/模型/RMS一致、取消与预算释放。
- `selected_plane_dialog_tests_v1`：采样/画布映射、1/6/7/12/13轮次、跳过与撤销、双帧完整单击/三点、候选/参数/位姿失效、过期与关闭、完整PLY/PCVMAP对应、模板版本/搬迁/原输入不存在后的完整点往返。
- 既有 `workpiece_template_tests_v1` / `workpiece_template_view_tests_v1`：坏包、取消、事务、主窗口发布/失效和完整读取，仍由用户执行。

后续命令（本轮未执行），配置真实 Qt/MSVC 路径，构建目录用源码树外新目录，公共头变动须完整重建：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <DebugBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <DebugBuildDir> -C Debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release -BuildTests -Generator "NMake Makefiles" -QtDir <QtDir> -BuildDir <ReleaseBuildDir> -CMakePath <cmake.exe>
ctest --test-dir <ReleaseBuildDir> -C Release --output-on-failure
```

完整工具链/启动冒烟说明见 [Windows 构建](build-windows.md)。重点人工验收：六格独立视角与放大恢复、第一格先显后索引、处理中取消/换轮/关闭、返回旧轮恢复高亮、跳过中间帧后位姿回退、完整候选与产物点数、检查后保存以及主窗口打开搬迁后的模板。对比冷/缓存加载、首格/整轮展示、每格可选面、单击完整候选、人工总时长、模板读写及峰值内存。

本轮未构建/CTest/启动/GUI/性能测量；静态检查不证明编译、等价或提速。不得宣称固定秒数、加速比例、识别率或生产质量通过。均匀抽样可能显示条带/漏掉细小特征，正式数据仍完整；跳过可能削弱相邻重叠，单平面不可观测方向仍依赖机器人位姿。

## 回滚边界

仅人工撤回本轮 apps 会话、10/50/60可选优化、core预算、回归与文档增量；保留任务前所有工作区修改、既有画布映射及v1/v2历史。不用整文件覆盖或破坏性Git回退，不恢复已撤回的模块30结果降采样方案，不改原数据，不提交/推送。


2026-10-16 后续说明：本文保留历史；当前纠错与验收规则由[整合v2](batch_plane_selection_template_integration_v2.md)替代，包含实际激活调度、过期OOM门禁、旧轮拼接、提交跳过缓存释放及投影并行/独立计时。本轮仍未构建或运行验收。
