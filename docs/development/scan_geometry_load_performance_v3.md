# 点云加载与选面耗时优化 v3

日期：2026-11-05。Owner：Codex（实现与静态检查）、用户（构建/CTest/原生GUI/性能）。源码交付，未生成新EXE。[v2说明](scan_geometry_load_performance_v2.md)保留原记录。

## 依据与验收目标

两次Debug日志均为Qt6.8.3、相同输入路径/文件大小/完整点数、读取缓存命中；没有来源内容逐字节对比或Release性能证据。

| metric | 优化前 | v2观测 |
|---|---:|---:|
| 首帧首次显示 | 5600 ms | 5673 ms |
| 整轮就绪 | 16117 ms | 15811 ms |
| 两帧索引 | 10546～10827 ms | 10203～10245 ms |
| 两帧法向 | 7261～7662 ms | 7226～7451 ms |
| 人工选面 | 9270 ms | 8712 ms |
| 自动匹配 | 16712 ms | 10425 ms |
| 画布发布 | 236～258 ms | 58～61 ms |

当前日志 `20261105_053513_078_a63ec8f553a14c65bccf252490d66dd1.jsonl` 确认匹配复用和sorted_binary生效，法向65536批次/2workers/4块、无预算回退。人工连通3883ms、面片2815ms，摘要约1622ms；均为单轮观测，不视为稳定提速。本轮不继续扩大并发或批次，不承诺秒数/加速比。

## 行为和接口

### 提前预览与校验状态

新GUI PLY：读取/转换/完整高度/确定性展示 → 发布只读预览 → 后续PrepareIndex任务顺序做完整摘要、完整索引 → 开放选面。摘要从LoadDisplay移出，但计算和SHA256字节流不变；它仍计入本轮就绪。两个任务槽和每任务两个compute workers不变。

SessionFrameV2新增SourceVerificationV3（Unverified/Checking/Verified/Failed）。旧候选快照和标注在重载期间保留，但未校验时不显示蓝红区域/旧提醒，保持高度色，不允许选面、候选/干涉确认、自动参考匹配和正式拼接。高度范围身份使用帧ID+loadAttempt，来自本次后台完整数据统计，不用未确认摘要绑定旧范围。摘要通过后GUI接收独立source_verified回调，恢复同源叠加；源变化清理候选/干涉/提醒/确认/结果及参考依赖。索引仍未就绪时不能选面。

回调核对帧ID、input revision、attempt和token，重复接受幂等；关闭先取消/join，过期回调丢弃。PrepareIndex最终结果带sourceVerified/sourceDigest/digestMs/verifiedMs作接收兜底。校验或索引失败保留只读预览，retry在相同完整点缓冲上处理，不重读转换；校验通过但索引失败时重试只做索引。深度帧已有raw摘要及按需物化保持，不加PLY摘要，不将二维预览全部物化。

### 法向邻居表

IndexPreparationOptions追加cacheNormalNeighbors，默认false，旧调用方保持直接27邻居查找。共享prepareSelectedPlaneIndexV2追加同默认尾参数，新GUI显式true。网格完成后在现有预算中申请每格27个int；13个正向邻居查找填双向，中心slot13，按原dz/dy/dx对应的slot0～26累计矩统计，PCA不变。缓存只在本次准备任务存活，索引不持有租约。预算/分配不足回原路径，法向私有模型仍65536→4096→串行回退。

PlaneSelectionIndexResult追加neighborPreparationMs/neighborCacheUsed/neighborCacheBytes/neighborFallback。邻居准备独立于normalsMs，二者都包含于index_ms；与v2比较法向时要同时观察邻居准备，不能只看变小的normals_ms。

### 连通工作区

SingleClickOptions追加reuseConnectivityWorkspace，默认true；可显式false作旧路径对照。一次任务内私有ConnectivityWorkspaceV3保留cells/originalNext/next/previous/labels/queue、bucket向量和哈希bucket容量，各轮重新填内容。unordered_map.clear仍释放节点；本轮没有替换哈希算法。新增可选留存按projected点数×256申请保守租约；不扩大512MiB。申请不足用原局部路径，较大轮次申请新容量失败时旧容量仍按原租约保留至任务结束。

保留原逐点顺序、邻居顺序、距离表达式、连通/跨带及最多六轮精化。补connectivityBuildMs/TraversalMs/CellCount/WorkspaceUsed；法向normalMs是索引网格法向，normalConnectivityMs是投影普通连通，不混称。

### 完整面片稠密路径

PlaneMaskOptionsV1追加preferDenseGrid（默认true，可false对照）；PlaneMaskResultV1追加performance，仅会话诊断。投影、采样间距、floor占用计算不变。包围网格单元数≤完整选中点数×4且大小合法、预算可申请时使用连续占用数组和顶点编号数组；否则稀疏哈希。保留原临时网格count×256租约，新稠密数组精确字节额外申请，失败/分配失败回稀疏，不加粗网格。

完整选面点索引生成相同占用单元及两三角形；保留凹形、孤立区域和孔洞，不做凸包/减面/补点。稠密路径按首次遇到单元顺序提交，内部编号/边界起点可与稀疏不同，验收比较规范化三角形覆盖及边界边集合。闭合边界原转向规则保持。拟合和面片仍在同一任务完成后一起发布；失败保持候选及显式区域错误，不染点、不异步补面片。

## 日志与汇总

- session增加performance_version=v3。task_finished及index_worker携带digest_ms，source_verified记录verified_ms（请求至摘要完成，worker测量）、source_changed；index_ready增加index_complete_ms（GUI接收索引完成）及原selectable_ms。原first_frame_ms、切帧/visible_wait和整轮wall_ms保持。
- performance增加neighbor_prepare_ms、neighbor_cache_used/bytes/fallback、connectivity_build_ms/traversal_ms/cells/workspace_used；mask_projection_ms/occupancy_ms/mesh_ms/boundary_ms、mask_grid_strategy、mask_dense_fallback/bytes、occupied_cells、boundary_edges、vertices。mask_ms为总面片；细项在其中。connectivity_ms含投影/普通连通/跨带等，不能重复相加。
- tools/summarize_scan_geometry_log_v2.py兼容入口继续使用，新增variant分组；session未带版本标为legacy，可用--label显式标基线/v2/v3。新数值字段自动按_ms独立统计，不将包含或并行项相加。不运行脚本、不改原日志或缓存。

```powershell
# 用户执行；报告目录必须源码树外。每次传入同版本5轮日志。
New-Item -ItemType Directory -Force D:/pcv-reports/load-performance-v3
python -B D:/workpiece/pointcloudview/pointcloudview/tools/summarize_scan_geometry_log_v2.py --label baseline C:/logs/baseline-1.jsonl C:/logs/baseline-2.jsonl > D:/pcv-reports/load-performance-v3/baseline.json
python -B D:/workpiece/pointcloudview/pointcloudview/tools/summarize_scan_geometry_log_v2.py --label optimized-v3 C:/logs/v3-1.jsonl C:/logs/v3-2.jsonl > D:/pcv-reports/load-performance-v3/optimized.json
```

## 用户构建与测试

公共DTO变化需要完整重编。MSVC x64、Qt6.8.3、NMake环境，下面<QtDir>替换实际kit，使用全新源码外目录；本轮不执行：

```powershell
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v3-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v3-debug
ctest --test-dir D:/pcv-build/load-performance-v3-debug --output-on-failure
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v3-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v3-release
ctest --test-dir D:/pcv-build/load-performance-v3-release --output-on-failure
ctest --test-dir D:/pcv-build/load-performance-v3-release -R "plane_mask_tests_v1|single_click_plane_selection_tests_v2|reference_plane_match_tests_v2|scan_geometry_(session|view|log_summary)_tests_v2" --output-on-failure
python -B D:/workpiece/pointcloudview/pointcloudview/tests/unit/tools/scan_geometry_log_summary_tests_v2.py
```

Python CTest沿用可选解释器注册；缺Python时手工执行。另以新目录覆盖新GUI单独/旧工作台单独；SDK深度和相机配置另按原说明，不用纯PLY回归替代。

新增测试源码（未运行）：提前发布不可选/确认，同源保留、来源变化清空、过期attempt/token拒绝、校验失败retry不换缓冲；未校验隐藏旧面片/恢复无点重传；邻居缓存开关/1和2worker/零预算/取消租约释放；连通workspace开关对孔洞、折面、混密度、跨带临界距离等价；稠密/稀疏规范化三角形和边界一致、倾斜/凹形/孔洞、大跨度回稀疏及预算回退；汇总variant/校验里程碑。保留原完整映射/快照/拼接/深度/迟到结果与关闭join回归，不设毫秒阈值。

## 用户性能与GUI验收

1. 同输入PLY/XML/位姿/容差/预算，Release基线与v3各五轮；登记机器、磁盘、Qt、点数/格式、缓存状态、峰值内存。命中/未命中分开，不删除缓存、不称为OS冷缓存。
2. 比较first_frame_ms、verified_ms、index_complete_ms/selectable_ms、round_finished.wall_ms；分别报告首显等待后移和总计算收益。邻居准备+法向整体对照；选面比较普通连通、跨带、面片细项和总worker。
3. 预览可旋转缩放但校验前选面/确认禁用；同源重载叠加恢复、修改来源失效、取消/失败/重试及跨轮回载后映射正确。拟合仍待面片一起完成，灰点+蓝红区域及25%填充、孔洞和修剪保留。百万点、100%/150%DPI和原生可见框选不穿透均需验收。
4. 摘要/完整点索引/模型/快照/PCVMAP等价；正式输出算法/schema不变。首显提早不表示整轮更快；较低normals_ms可能只是增加邻居准备阶段。

## 验证状态与风险

本轮实际静态检查：两份Python源码AST解析通过（未执行脚本/测试）；14份相关C++头/实现/测试的词法括号配对通过（不能代替编译）；本轮涉及的11份文档相对文件链接均存在；测试CMake注册静态核对完成；git diff --check通过。工作区删除集合与实施前相同，仍为32项，本轮无新增删除。实施前差异及实施后状态记录在源码树外 `C:/Users/HP/AppData/Local/Temp/pcv-load-performance-v3-20261105-134814/`。

仅静态检查，不构建、CTest、GUI或性能采样，无v3耗时结论/EXE。新增邻居表可能抵消查找收益，稠密网格/工作区增加瞬时内存且预算不足可能长期走旧路径，需看实际日志。512MiB只管理可选存储，不是整个进程/GPU上限。公共头与Qt回调需要真实编译及生命周期验收。

回滚只撤本轮source verification流程、邻居缓存、workspace/稠密路径及计时/测试/文档增量并完整重编；保留RAW、契约适配、应用清理、v2等已有工作。没有提交/推送/上传或缓存清理。
