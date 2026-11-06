# 基于真实日志的点云耗时优化 v2

日期：2026-11-05。Owner：Codex（实现与静态检查），用户（构建、CTest、原生 GUI 和真实性能验收）。状态：源码交付，未更新 EXE。本文接续 [v1](scan_geometry_load_performance_v1.md)，不覆盖此前验证记录。

## 依据与目标

依据日志 `20261105_045240_921_dddf695829254356859344a897c331c9.jsonl`：Debug、Qt 6.8.3、两帧各约453万点，读取缓存均命中。

| metric | 该日志观测 | 含义 |
|---|---:|---|
| 首帧首次显示 | 5.60 s | 请求至当前数据的实际 frameSwapped |
| 完整索引 | 10.55～10.83 s | 包含网格及法向 |
| 法向 | 7.26～7.66 s | 约占索引67%～73% |
| 源文件指纹 | 2.02～2.06 s | 即使缓存命中也计算 |
| 转换后摘要 | 1.54～1.55 s | 完整来源一致性校验 |
| 转换 | 1.37～1.39 s | 含并行归并 |
| 人工选面 / 自动匹配 | 9.27 / 16.71 s | 应用任务耗时 |
| 画布发布 / GL上传CPU调用 | 0.24～0.26 / 0.014～0.017 s | 上传不等于GPU完成 |

排队接近零。第二帧 `first_frame_ms=55782` 包含用户延后切换，不能据此认定渲染慢。以上仅为优化前 Debug 的单次样本；没有优化后或 Release 的测量，不推算加速比例。

## 实现与边界

1. 应用共享任务新增 `snapshotFromSingleClickV2()`：自动匹配直接复用候选的完整 `SingleClickResult`，取消再次执行同一种子的完整选面。人工单击仍执行原算法，再共用快照、中心及完整面片后处理。三点路径共用后处理。三个快照向量按完整选中点数 reserve；保持原序点、sourceIndices、scanRatios、模型与采样轴/间距。索引已递增时不再排序复制，乱序输入仍排序、重复/越界拒绝。区域生成失败仍保留合法拟合候选。
2. 模块60 `IndexPreparationOptions.normalBatchSize` 默认4096；共享准备函数新增同默认的尾参数，新 GUI 显式请求65536。每个worker每批最多65536网格，仍是两帧各两个worker；邻居累计、PCA和提交顺序不变。并行私有模型缓冲申请现有预算；失败先降4096，仍失败串行，线程创建失败先收敛已创建任务再串行剩余区间。没有提高512 MiB预算。取消不发布部分索引，已启动任务全部等待结束。
3. 模块20 `setDisplayCloud()` 对递增映射做相邻校验，反查使用 lower_bound；乱序仍构建QHash并拒绝重复。完整/展示映射及可见点GPU拾取语义不变，旧调用方也可自动采用快路径。不改点数、着色、面片、marker/texture缓冲策略或GPU上传策略。
4. 新增切帧观察序号与展示revision/attempt关联；同一已加载数据再次激活也观测frameSwapped，但不重传点云。隐藏期间不累计可见等待且不接收为已显示，恢复后继续。切到未加载帧时观察保留至加载发布；attempt改变重新开始观察。原加载请求首显保留，仍包含用户延后激活等待。
5. 成功拼接正常说明记录在 `message`，warn_only质量政策记录在 `warning`；成功的 `error` 为空。失败才写错误，取消/过期按任务身份记录；事务提交状态不变，不宣称严格质量通过。

SHA256、完整转换摘要、缓存校验、模块50数学、模块60选面数学、模块30拼接、正式输出schema保持不变。有限种子/歧义/80%去重与人工审核规则不变。浅灰画布、百万展示、六帧窗口、两帧均衡调度、完整面片、世界Z软提醒均保留。不新增转换后磁盘缓存，不删除现有缓存。

## 新日志字段

`task_finished.performance`：`algorithm_ms`、`local_fit_ms`、`candidate_ms`、`classification_ms`、`connectivity_ms`、`refinement_ms`、`projection_ms`、`normal_connectivity_ms`、`gap_ms`，以及后处理 `snapshot_validation_ms`、`snapshot_copy_ms`、`center_ms`、`mask_ms`、`postprocess_ms`。三点只有实际对应的算法字段。原 `snapshotMs` 现在只表示快照复制，不混入面片。

匹配另有 `match_search_ms`、`attempted_seeds`、`eligible_buckets`、`seed_selection_ms`（按尝试种子顺序）、`reused_selection`。搜索失败/取消也保留已获得诊断；复用仅针对 Unique。`algorithm_ms` 是被复用候选先前的算法时间，已包含于搜索内，不能再次相加。

`index_worker.performance`：`bucket_count`、`normal_batch_size`、`normal_batches`（实际计算的块数）、`normal_workers`（本次最大实际并行块数）、`normal_fallback`（空、budget_small_batch、budget_serial、launch_serial）、`normal_compute_sum_ms`、`normal_join_ms`、`normal_commit_ms`。compute为各worker计算时间之和；join为等待结果的墙钟时间，含计算等待。它们与normals_ms重叠，不能相加为索引总时间。串行不分配私有模型数组。

`canvas_publish_cpu.mapping_strategy`：sorted_binary / hash。`view_frame_swapped`：`view_sequence`、`display_revision`、`view_wait_ms`（本次激活至显示，包含其间隐藏）、`visible_wait_ms`（仅可见等待）；帧ID/attempt由会话补齐。界面显示最近一次切帧指标。`first_frame_ms` 仍是本次加载请求至首显，不当作持续帧率。程序自动定位异常帧时观察从画布绑定开始；这不是原始鼠标按下响应的精确测量。

所有阶段分别分析：index含grid/normals，search含种子选面，postprocess含复制/中心/面片。缺失值不填零，不能把并行/包含阶段相加。

## 日志汇总脚本（用户执行）

`tools/summarize_scan_geometry_log_v2.py` 仅使用Python标准库，读取UTF-8 JSONL（可带BOM），报告stdout，不写缓存或输入文件。按Debug/Release、缓存命中、任务状态、事件和指标分组；每项输出count、median_ms、min_ms、max_ms。关联frame/input revision/attempt，多个会话的相同ID不串用缓存状态。损坏JSON行登记parse_issues，缺失数值跳过。

```powershell
# 先创建源码树外报告目录，替换准确的日志路径。
New-Item -ItemType Directory -Force D:/pcv-reports/load-performance-v2
python -B D:/workpiece/pointcloudview/pointcloudview/tools/summarize_scan_geometry_log_v2.py C:/path/to/baseline.jsonl C:/path/to/optimized.jsonl > D:/pcv-reports/load-performance-v2/summary.json
python -B D:/workpiece/pointcloudview/pointcloudview/tests/unit/tools/scan_geometry_log_summary_tests_v2.py
```

比较基线和优化版请分别执行汇总命令，避免混合成一组；可以将同一版本的五次会话作为多参数一次汇总。脚本逐指标统计而不自动判断性能达标，不输出加速承诺。缺Python时CMake跳过该脚本测试，C++测试照常注册；用户可随后手动执行。

## 构建与测试命令（用户执行，本轮未运行）

公共DTO/画布类布局变化需要全量重编。在已初始化MSVC x64、Qt6.8.3的PowerShell中使用全新源码树外目录。`<QtDir>` 替换为实际Qt MSVC kit路径，不复用旧缓存。

```powershell
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v2-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v2-debug
ctest --test-dir D:/pcv-build/load-performance-v2-debug --output-on-failure
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v2-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v2-release
ctest --test-dir D:/pcv-build/load-performance-v2-release --output-on-failure
# 定向复验不能替代全量：
ctest --test-dir D:/pcv-build/load-performance-v2-release -R "reference_plane_match_tests_v2|single_click_plane_selection_tests_v2|scan_geometry_(view|session|log_summary)_tests_v2" --output-on-failure
```

另用新目录覆盖新GUI单独启用、旧工作台单独启用（`PCV_BUILD_POINTCLOUDVIEW` / `PCV_BUILD_SCAN_GEOMETRY_DEMO_V1`）。相机/SDK测试另按现有配置执行。

测试源码新增：匹配结果复用与重新拟合的完整索引/模型/快照/来源/面片等价；4096/65536、串行/2/4 worker和零/小预算回退等价（大网格样例跨多个窗口），取消不发布且释放预算；递增/乱序/缺失/重复/越界/修剪映射；同源激活不重传与隐藏恢复；成功拼接日志无error及匹配复用标识；汇总分组/缺失值/多会话隔离。既有多候选、未找到、迟到结果、关闭join、完整来源与拾取回归保留。不设不稳定的毫秒阈值。

## 用户性能与GUI验收

- 使用相同两份PLY、XML、位姿、容差、预算和显示上限。分别保存Release基线与优化版日志各五轮，并记录CPU/GPU/磁盘、Qt、编译类型、文件格式/点数和峰值内存。缓存命中与未命中分别统计，不自动清缓存，不称为OS冷缓存。
- 比较首显、可选面、索引/法向、人工选面、自动匹配、整轮耗时的中位数与范围。固定相同的切帧/交互节奏，隐藏帧的首次延后激活单列。确认匹配日志reused_selection=true，不再有第二次完整选面。
- 切帧/重复激活/隐藏恢复检查view_sequence及visible_wait；确认重复观察没有点缓冲上传。原工作台和新GUI都检查单击/矩形可见点拾取、修剪/撤销、候选确认与取消、蓝红面片、高度色切灰。
- 检查完整点数、索引、来源映射、快照、正式PLY/PCVMAP一致；正常成功拼接显示完成/质量警告而非错误。Qt Creator可编辑UI，100%/150%DPI及百万点交互由用户验收。

## 验证状态、风险与回滚

实施方仅静态审阅、Python语法、C++分隔符/接口引用、CMake注册/文档链接与git diff检查；不运行构建、CTest、GUI或性能采样。实际通过情况由用户补充，不以静态检查代替编译或运行。没有新版EXE或优化后耗时结论。

较大法向批次增加瞬时缓冲，由预算回退；二分反查可能使大量反向查询变慢，需完整拾取回归。Debug中的SHA和摘要占比需要Release再次量测；本轮不改其契约。回滚仅撤回本功能的共享后处理、法向调度/计时、递增映射、视图计时/日志及对应测试/文档增量，完整重编；保留既有RAW、契约适配、配色和应用清理等未提交修改。不提交、不推送、不上传。

实际静态记录：两份Python文件AST解析、15份C++头/源词法分隔符、CMakePresets JSON、Python CTest注册/路径及上述更新文档本地链接检查通过；git diff --check通过。源码外status-before/after核对既有32个删除项完全一致，本轮没有新增删除。以上均非编译或测试运行。工作区仍保留既有及本轮未提交/未跟踪修改。
