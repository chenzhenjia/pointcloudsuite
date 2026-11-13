# 完整点云处理提速 v3：减少重复工作，保持计算结果一致

日期：2026-10-16。owner：Codex/主线。当前整合与修正以 [整合 v2](batch_plane_selection_template_integration_v2.md) 为准；本页独立记录底层优化。未构建/运行测试，用户负责动态验证。

## 完整数据路径

展示降采样仅减少上传/绘制量。本轮优化完整读取、转换、网格法向和单击分类/投影，不降低数学精度或改变选面拓扑，不引入GPU、持久化选面索引、跨轮预加载或模块30配准缓存。

| 模块 | 公共扩展 | 批量入口策略 |
|---|---|---|
| 10 | CloudCachePolicy与四参readPlyCached，三参默认兼容 | 不准备canonical PLY，canonicalReady=false；完整SHA-256、原头/声明点数/缓存校验和分块取消保留 |
| 50 | CloudTransformOptions末尾collectSamplePoints/parallelEnabled/maxParallelTasks/processingBudget | 关闭samplePoints；连续65536点窗口、最多4worker、私有输出全join后原序归并 |
| 60 | IndexPreparationOptions；SingleClickOptions共享预算 | 可选point→bucket整数缓存、独立网格法向并行、分类/投影/距离/队列缓冲复用 |

逐点转换仍使用原过滤、比例、位姿插值、矩阵运算及法向处理，保留来源、点序和拒绝统计。网格编号/插入/邻居遍历顺序不变，PCA/RMS累计不并行重排。单击保留v2六轮、容差、跨带、模型判断和停止条件；投影串并行使用相同点表达式，分块join后按原局部下标归并。

## 预算、取消与诊断

新增缓存/临时缓冲共享256 MiB，包含展示映射、point→bucket和计算私有窗口；不含完整业务数据/原有索引/确认快照/GPU及管理开销。预算不足减worker/串行或释放复用，不能减少正式点数；OOM暂停新任务，保留已确认数据。线程创建失败先join已有任务，再原序串行处理未完成区间。

RobotCloudResult追加mergeMs/actualWorkerCount，SingleClickResult追加projectionMs/normalConnectivityMs；既有gapMergeMs独立记录跨带。会话还记录指纹/cache读写/解析、转换、索引网格/法向、局部模型、分类、PCA、快照、真实排队、worker、GUI发布等待和实际首显/首高亮。新增诊断不写正式manifest。

## 对照和验收

1. 用户完整Debug/Release构建，执行cloud_cache_tests、handeye_transform_tests、single_click_plane_selection_tests_v2及全量CTest。
2. 缓存hit/miss、源内容变化、损坏/截断、分块取消及canonical开关解码点集一致；不以size/mtime替代内容指纹。
3. 串行和1/2/4worker保持额外Qt共享副本，逐项精确比较XYZ、法向、ratio、source、过滤统计、模型/RMS/选点/迭代及合并区域；不比较耗时。
4. 至少102400点夹具覆盖真正的并行投影窗口，零预算覆盖回退；取消不发布部分结果，全部租约释放。
5. 同工具链/输入/配置比较冷/缓存加载和多个内部点击；记录端到端和进程峰值内存，不能把等待转移视作收益。

保留串行参考与审计确认的v2完整路径对照。失败的优化不得作为验收通过的默认路径；修复或退回对应可选优化。不预先承诺速度比例，完整读取/连通仍可能是瓶颈。具体构建命令与用户验收清单见整合v2。
