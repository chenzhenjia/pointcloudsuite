# scan_geometry 接口说明

更新：2026-11-13。本页集中说明当前应用内部接口，以源码和测试为准。仅整理职责、注释及重复入口，未提取SDK、设计主项目接入、修改领域算法或升级数据格式。命名空间仍为`pcv::demo`，主要文件和类型保留既有版本后缀。

## 1. 职责与源码入口

| 层次 | 入口 | 职责 |
|---|---|---|
| 会话 | [scan_geometry_session_v2.h](../../apps/scan_geometry_demo_v1/scan_geometry_session_v2.h)、[实现](../../apps/scan_geometry_demo_v1/scan_geometry_session_v2.cpp)，`pcv_scan_geometry_session_v2` | 配置、六帧窗口、调度、人工审核、拼接、修剪和保存状态 |
| 共享任务 | [selected_plane_tasks.h](../../apps/stitching_ui/selected_plane_tasks.h)，`pcv_selected_plane_tasks` | PLY/SDK深度读取、50坐标转换、完整索引、选面与不可变候选快照；任务类型仍有V2后缀 |
| 区域计算 | [result_regions_v1.h](../../apps/scan_geometry_demo_v1/result_regions_v1.h) | 拼接修正后的工件/干涉区域、固定投影网格；同步计算，不落盘 |
| 几何输出 | [scan_geometry_output_v1.h](../../apps/scan_geometry_demo_v1/scan_geometry_output_v1.h) | 完整/修剪点云及来源映射、WObj1和sidecar事务保存 |
| 可选契约输出 | [scan_geometry_contract_v1.h](../../apps/scan_geometry_demo_v1/scan_geometry_contract_v1.h) | 当前SR-2026候选子集的身份、来源和资产校验及暂存 |
| 页面/画布 | `ScanGeometryWindowV1`、`ScanGeometryCanvasV2` | 控件、拾取回译、矩形选择与显示；调用会话，不复制算法 |

领域算法仍属于10/30/50/60。30不依赖60，会话负责映射请求。后端现有构建组织不变，没有新增独立构建开关。以下所有会话接口均在所属GUI线程调用/读取，运行异步任务时必须继续处理Qt事件循环。

会话使用的共享任务均为同步函数，由会话调度到worker；不直接发送会话信号。`SelectedPlaneTaskV2`按kind读取对应载荷，检查ok/cancelled/error，不把任一非空字段当作整个任务成功。token须为有效共享原子对象，budget为共享优化预算；读取共享输入时保留const快照及原有额外共享副本。

| 共享任务入口 | 输入与返回约定 |
|---|---|
| `parseSelectedPlaneScanJsonV2(path,input,error)` | 同步解析既有扫描JSON至输入描述符，bool表示解析校验结果；不加载完整点云 |
| `loadSelectedPlaneFrameV2(...)` | PLY路径、XML、Start/End、layout、token、worker数、预算及展示上限；返回robot_base完整数据与展示映射，读取/转换失败在task中报告 |
| `loadSelectedDepthFrameV2(...)` | 读取已保存SDK帧目录的原始C16/辅助强度及摘要；可只有rawDepthFrame，没有XYZ，不表示已可选面 |
| `materializeSelectedDepthFrameV2(...)` | 持有的只读原始深度帧、来源/XML/位姿等；通过SDK物化后按FullXyz栅格行进度转换并准备展示 |
| `prepareSelectedPlaneIndexV2(...)` | 完整点集、token/worker/预算；默认normalBatchSize=4096、cacheNormalNeighbors=false，返回不可变索引和准备诊断 |
| `fitFullPlaneV2(...)` | 完整RobotBaseFrame、控制点、容差/缺失带、单击开关及配套索引；返回完整选面快照、完整帧下标和掩码，不使用显示子集 |
| `snapshotFromSingleClickV2(...)` | 已完成的完整单击选择及来源；只生成快照/结果映射，不再次拟合 |
| `prepareHeightRangeV1(task,token)` | 同步从完整点集提取Z范围并写入task；false须结合取消/任务字段，不把默认范围当真实极值 |
| `prepareDisplayCloudV2(task,cancelled,limit)` | 同步准备确定性展示子集及完整下标映射；默认limit=300000，扫描会话显式采用1000000；不改变正式点集 |
| `loadedCloudDigestV2(cloud,token)` | 同步SHA-256，包含来源路径、位姿/标定和完整点/索引/行程映射；取消可返回空摘要，不是PLY文件字节哈希 |

## 2. 数据、坐标与有效期

### 输入配置

`InputFrameV2`是`pcv::ui::SelectedPlaneInputV2`的别名。

| 字段 | 约定 |
|---|---|
| `path`、`depthFrame` | 默认PLY文件；true时是已保存SDK帧目录，包含`frame.json`，不是裸RAW文件。扫描JSON须先通过`parseSelectedPlaneScanJsonV2()`解析 |
| `start`、`end`、`layout` | 位姿长度mm、角度deg；默认`LineProfileXz`。FullXyz保持原XYZ，转换统一由50执行 |
| `calibration` | 当前帧手眼XML；空时用`InputConfigV2::calibration` |
| `scanJson`、`scanId`、`warning` | 可选扫描来源和提示，不代替实际点云/位姿校验 |
| `InputConfigV2::frames` | 有序输入至少2帧，审核/映射保持原顺序；每轮最多6帧 |
| `outputRoot` | 必填、源码树外绝对路径；仅配置成功不代表文件内容已通过读取/坐标校验 |
| `tolerance`、`maximumGap` | 默认0.4/3.0 mm；前者有限且>0，后者有限且在[0,50] |
| `searchDistance`、`searchAngle`、`maximumSeeds` | 默认10 mm、5 deg、16；距离>0，角度(0,90]，种子数[1,64] |
| `highSideMinimumHeightMm`、`highSideMinimumPointCount` | 默认2 mm、100；分别>=0、>=1；高侧仅提示，不自动确认干涉 |
| `poseOnly`、`threePoint` | 默认false/false；分别关闭相邻精配准、改用三点选面 |
| `contract` | `runtimeRoot`非空时启用既有SR-2026导出，默认关闭 |

坐标转换后内部点集为`robot_base/mm`；位姿为`[X,Y,Z,A,B,C]`，旋转`Rz(C)*Ry(B)*Rx(A)`，`T_base_depth=T_base_flange*T_flange_depth`。已转换选面不能再做手眼变换。具体数据格式见[数据契约](../contracts/data-contracts.md)。

### 帧状态与索引

- `SessionFrameV2::id`为会话分配的稳定ID；`loadAttempt`每次进入加载队列递增。接口中的`frame`、`active()`、`reference()`、`referenceFrame`仍使用原输入0-based序号，不能传ID。`round()`为0-based轮次。
- `cloud.fullPoints`是当前帧完整转换数据，`display`是最多100万确定性实测展示点。`displayToFull[i]`将显示点回译完整帧下标；`pick()`和高侧`seedIndex`都使用完整帧下标。
- `planeIndices`、`interferenceIndices`、`pendingInterference`分别为工件面、已确认干涉和待确认干涉在当前完整帧中的下标。干涉不参与工件拼接。
- `candidate`持有完整选面快照；深度帧确认后可释放`cloud/index/display`，空XYZ缓存不代表候选丢失。`depthReady`只表示原始深度预览就绪，不代表完整XYZ、来源Verified或索引就绪。
- `sourceVerification`为Unverified/Checking/Verified/Failed；PLY摘要记录完整转换数据；SDK深度摘要记录原始C16及辅助强度。摘要和索引必须与各自来源一致。`hasCandidate`、`automatic`、`reviewed`、`confirmed`分开表示候选存在、自动生成、人工审核和确认。
- `indexQueued/indexPreparing/loading/highSideChecking`、计时、`displayLease`等为内部运行状态；调用方只能观察，不应写入或接管预算。

### 结果、版本与借用

| 数据 | 使用规则 |
|---|---|
| `result()` | 先检查`hasResult()`；保留完整拼接点、原序来源映射和磁盘结果入口，修剪不会就地删掉完整点 |
| `removed()` | 完整拼接结果原始下标集合，不能传帧内下标或过滤后展示下标 |
| `resultRevision()` | 新结果/失效时变化；矩形选点需保存对应版本并传给`erase()` |
| `trimRevision()` | 修剪、撤销、恢复或结果失效时变化；新拼接可重置，不是全局单调序列 |
| `ResultRegionSnapshotV1` | `resultRevision/trimRevision/annotationRevision`三元组对应区域计算输入；内部只接受仍匹配的结果 |
| 区域`state` | Empty/Building/Ready/Failed/Cancelled；失败或取消时部分已有干涉区域可能保留，不以网格非空冒充全部Ready |
| `wobj1()` | 先检查`valid`；生成成功不等于已保存。当前算法从完整确认参考面包围盒中心投影及robot_base轴建系 |
| `resultHeightRange()` | 显示辅助范围，默认(0,1)，由任务字段发布；没有有效标志，不承诺一定是完整结果真实极值，不能用于业务判断 |

`config()/frames()/result()/removed()/resultRegions()/wobj1()/committedOutputs()/pendingSeeds()`返回内部`const`引用，`activeFrame()`返回内部指针。它们是借用，禁止跨异步状态变更、configure/reset、帧缓存释放或析构保存使用；再次处理事件或发出修改命令后应重新获取。复制Qt容器可以保留只读数据，但会延长共享缓冲寿命；不得与worker共享可变对象或用`const_cast`改数据。本轮未增加永久快照机制。

## 3. 会话接口清单

同步查询无业务落盘。修改入口可能记录诊断JSONL；部分`void`接口在条件不满足时直接返回，不保证发送通知或设置新的错误。`bool`的含义按每行约定，不表示整条异步流水线完成。以下“空闲”指`!busy()`，并非仅线程池里没有运行任务。

### 配置与输入

| 接口 | 参数、条件与执行结果 |
|---|---|
| 构造 / 析构 | 构造创建内部预算/线程池并写会话日志；析构调用`shutdown()`，可能阻塞等待 |
| `config()` | 借用当前已接受配置 |
| `configure(config,error)` | 同步检查空闲、帧数、参数、文件存在、有限位姿、输出边界及可选契约；成功reset旧会话、规范输入/XML绝对路径并分配ID，不自动加载。失败false，可写error，原配置保持；成功不保证清空error旧值 |
| `loadRound(round)` | 空闲、非匹配且有输入时异步加载；轮次夹取到有效范围。切轮释放窗口外缓存，清控制点并失效WObj1/保存状态；确认快照保留 |
| `processActive()` | 当前帧非忙、非匹配，且为已就绪原始深度源时，按需异步物化XYZ和索引；已有XYZ/索引时不重复处理 |
| `activate(frame)` | 当前轮内合法序号，且交互未阻塞、非未暂停匹配时切换；必要时排队加载，清控制点 |

### 状态查询

| 接口 | 返回语义 |
|---|---|
| `frames()/activeFrame()` | 借用帧列表/当前帧；无合法当前帧返回nullptr |
| `active()/round()/reference()` | 原输入序号、轮次、参考帧序号；active=-1可为结果视图或未配置，reference=-1为尚无参考 |
| `runningCount()/busy()` | 前者为watcher数量；后者还包括待执行任务及加载队列 |
| `interactionBlocked()/activeFrameBusy()` | 前者检查前台排队、非读取/索引任务或取消token；后者再加当前帧加载/索引状态。busy与允许交互不等价 |
| `canSelectActive()` | 当前帧非忙、非连续匹配（或已暂停）、来源Verified、完整XYZ/索引存在且未跳过 |
| `matching()/paused()` | 匹配流程状态；人工处理后须显式resume |
| `status()/summary()` | 显示文本及保留/跳过/异常/未审核计数，不是可解析错误码 |
| `canReviewAll()` | 空闲、非匹配、无控制点、参考存在；至少两帧未跳过，来源Verified、无problem、有候选、已审核且工件/干涉不冲突 |
| `canStitch()` | `canReviewAll()`且所有未跳过帧confirmed |
| `canConfirmGeometry()` | 空闲、有结果且在结果视图、完整参考快照已确认、无problem或待确认干涉，保留至少三个非共线点。它不是IO、WObj1建系或SR-2026来源校验 |
| `geometryConfirmed()` | 当前几何已成功保存并被会话接受；后续修改可清除此状态。磁盘成功提交但结果过期时可能仍为false |
| `progress()` | -1无已知比例，否则0..100；不能用100替代结果状态 |

### 选面与审核

| 接口 | 参数、条件与副作用 |
|---|---|
| `pick(fullIndex,interference)` | 需canSelectActive和合法完整帧下标；false选工件、true选干涉。单击或收齐三点后异步拟合；重复点拒绝。重新选择工件会失效旧候选及拼接结果 |
| `pendingSeeds()` | 借用当前完整帧控制点下标 |
| `cancelSelection()` | 同步清控制点并pump；不取消已经运行的拟合任务 |
| `setThreePoint(enabled)` | 当前帧非忙时修改选面模式并清控制点；忙时忽略 |
| `confirmCandidate()` | 同步bool；非忙、来源Verified且候选存在。首个未跳过帧须先成为参考；确认/重确认参考使依赖结果失效，深度确认可释放XYZ缓存 |
| `confirmInterference()` | 同步bool；有待确认干涉且与工件不重叠时提交；递增annotation版本，失效几何，有结果时异步重建区域 |
| `clearInterference()` | 当前帧非忙时清已确认/待确认干涉及网格；失效几何，视情况异步重建区域或重新搜索提醒 |
| `selectHighSideCandidate(seedIndex)` | seed必须属于当前高侧候选；true仅表示拟合任务已提交，结果仍需confirmInterference |
| `ignoreHighSideCandidate(seedIndex)` | 同步bool；从当前候选移除提醒；不修改拼接输入，也不意味着确认干涉 |
| `clearPlane()` | 当前帧非忙时清工件面，失效拼接结果；清参考时还使参考依赖候选失效 |
| `matchRemaining()` | 空闲、无控制点、参考已确认且Verified时启动异步匹配；异常/多解等会暂停，自动候选仍需人工审核 |
| `resumeMatching()` | 空闲、无控制点、已暂停，当前帧已审核或跳过；问题帧无有效候选时仍阻止恢复 |
| `skipActive()` | 当前帧非忙时显式跳过，清候选、pending干涉和索引排队，失效结果；跳过参考时取消参考依赖 |
| `confirmAll()` | canReviewAll时同步批量confirmed；深度帧可释放XYZ缓存；不自动拼接 |

### 结果处理

| 接口 | 参数、条件与副作用 |
|---|---|
| `showResult()` | 空闲且有结果时切结果视图、清控制点 |
| `hasResult()/result()/resultHeightRange()` | 完整结果及显示辅助范围，适用条件见第2节 |
| `removed()/resultRevision()/trimRevision()/resultRegions()` | 修剪集合与版本、区域借用视图 |
| `canUndo()/wobj1()` | 是否有撤销记录、WObj1借用；canUndo本身不检查busy或当前视图 |
| `stitch()` | canStitch时异步调用30，保留完整选面输入，原坐标不再手眼转换。先事务提交完整拼接，再生成区域；区域失败不撤销拼接 |
| `rebuildResultRegions()` | 有结果、空闲且未shutdown时异步重建工件/干涉展示区域；不改已提交点云 |
| `erase(originalIndices,resultRevision)` | 空闲、有结果、结果视图且版本匹配；忽略越界项，无实际变化不操作；更新删除集合/撤销栈、失效几何并异步重建区域 |
| `undo()` | 空闲结果视图、有撤销记录时恢复上一删除集合，失效几何并重建区域 |
| `restore()` | 空闲结果视图、有删除点时恢复全部点；此次操作入撤销栈，失效几何并重建区域 |
| `generateWobj1()` | canConfirmGeometry时同步生成；返回valid，失败可更新status。不会保存；每次生成都会清已保存标志/路径 |

### 保存与生命周期

| 接口 | 参数、条件与执行结果 |
|---|---|
| `savedGeometryPath()` | 当前成功发布的sidecar路径；未保存或失效时空 |
| `committedOutputs()` | 本会话已知拼接/sidecar/契约入口列表，不是所有资产枚举，也不是当前几何有效性证明；reset清列表但不删文件 |
| `saveGeometry()` | 唯一会话保存入口。canConfirmGeometry时必要则先generateWobj1，再提交异步保存。契约同run已有正式JSON时直接拒绝；待确认干涉或契约所需掩码缺失也阻止保存。接受任务不是保存成功；等待收尾后检查geometryConfirmed/path/status |
| `retry()` | 仅当前帧非忙且非连续匹配时：已有XYZ但无索引/未验证则重做校验索引，否则清当前缓存重新加载来源。不是最终保存重试函数 |
| `cancel()` | 非阻塞设置token/递增输入revision、清队列/控制点/匹配状态；保留原始来源和已审核候选。须等待busy=false，不能立即销毁worker数据 |
| `reset()` | 空闲时清配置、候选、结果、撤销记录和当前产物列表；busy时忽略。保留磁盘产物 |
| `shutdown()` | 幂等；取消并join所有watcher和线程池，保留已提交文件并尽可能登记入口。同步阻塞，调用后对象仅供销毁，不得重新configure/load |

### 诊断与通知

| 接口 | 含义 |
|---|---|
| `timingSummary()/recentLog()/timingLogDirectory()` | 显示耗时、最近最多200条JSONL、日志目录；缺耗时为“未观察”，日志失败可反映在summary |
| `recordOperation(operation)` | 同步记录操作和当前帧/版本身份；不改变算法 |
| `observeCanvasTiming(frameId,attempt,timing)` | 仅匹配ID/attempt时接纳显示耗时；特定换帧事件刷新状态 |
| `observeFirstFrame(frameId,attempt)` | 记录该加载attempt首次显示；旧身份、已取消或已记录时忽略 |
| `changed(cloudChanged)` | true请求刷新点云/选择/区域，false刷新状态；没有任务ID，不是成功/完成事件，不保证每次被拒调用都发出 |

## 4. 同步计算与输出函数

这些函数不驱动会话状态，不发送`changed`。输入在调用期间不得并发修改；取消回调可空，非空时必须可在worker安全读取。

| 接口/类型 | 当前约定 |
|---|---|
| `ResultRegionBasisV1` | robot_base投影平面、U轴、mm间距和网格原点；无有效基准时从修正后的参考面建立。解析间距后固定原点，修剪/重试复用 |
| `ResultRegionsRequestV1` | 完整拼接points、removed下标、保留帧顺序的frames/corrections；referenceKeptIndex是保留列表下标，不是原输入序号；提供预算和取消回调 |
| `buildResultRegionsV1(request)` | 同步计算，不落盘。分别检查workpieceOk/interferenceOk/cancelled/error；一侧失败另一侧可能有效，空干涉合法。performance仅诊断 |
| `GeometryFrameOutputV1` | 原输入顺序的帧证据，ID、原始来源、可空scanJson和完整原帧干涉下标 |
| `GeometryOutputRequestV1` | 完整成功拼接结果及等长映射数组、removed、valid WObj1、原输入参考序号/帧证据、输出根和取消回调；契约启用时还需完整contractFrames来源证据。resultRevision仅记录，不替调用方核验活动版本 |
| `saveSelectedPlaneGeometryV1(request)` | 同步IO；校验完整映射、WObj1、修剪下标和可选契约后在同笔事务保存。调用方负责已有会话审核条件和源码外输出根，不应把低层函数当完整会话校验器 |
| `GeometryOutputResultV1` | success仅commit成功后true；失败时检查cancelled/error。outputDirectory、trimmedPly、trimmedMapping、sidecar仅成功有效；contractFile仅契约模式成功有效 |
| `ScanGeometryContractV1::enabled()` | 仅检查runtimeRoot是否非空，不代表上下文有效 |
| `validateScanGeometryContractV1(context,error)` | 同步无落盘；关闭则true。启用时检查源码外绝对非根目录、ID、wobjId=1、正像素尺寸和安全相对引用；成功不保证清空error |
| `pointCloudContractDirectoryV1(context)` | 仅计算`jobs/<jobId>/runs/<runId>/03-point-cloud`路径，不验证或创建目录 |
| `stagePointCloudContractV1(transaction,request,document,error)` | 同步重读/核验来源和上游证据，暂存资产、最后暂存JSON；可选document/error输出。它不commit，失败由调用方终止整笔事务 |
| `validatePointCloudResultV1(document,error)` | 同步校验当前JSON子集、字段关联和引用形式；不是实际文件存在、数据精度或业务身份真实性证明 |

## 5. 正常流程与失效规则

1. 构造会话，configure成功后loadRound；PLY先显示完整数据的抽样，后台校验来源和建索引。SDK深度可先预览，再processActive物化XYZ。
2. canSelectActive后将拾取回译完整点下标，调用pick；候选就绪后人工confirmCandidate。首个未跳过帧为参考。
3. matchRemaining辅助其余帧，逐帧人工审核、修正或跳过；暂停后满足条件再resumeMatching；未处理的帧换轮加载。
4. canReviewAll后confirmAll，canStitch后显式stitch；等待有结果且不busy。区域生成失败可rebuildResultRegions，不重新冒充拼接失败。
5. 结果视图下按当前resultRevision调用erase，等待区域重建后再undo/restore或下一步。干涉与修剪修改会使WObj1/保存状态失效。
6. 可先generateWobj1预览，再saveGeometry；也允许saveGeometry按既有规则先生成WObj1。收尾后检查geometryConfirmed及savedGeometryPath，不能用canConfirmGeometry或进度100当成功。
7. 取消后等待busy=false再retry/reset/关闭；退出调用shutdown并等待完成。

参考变化、来源摘要改变、重选工件面或跳过等会清依赖候选/拼接结果；重新拼接会清会话修剪。切轮/重读/几何修改也可能使WObj1与已保存标志失效。过期任务核对输入/参考revision和token，区域任务另核对结果/修剪/标注版本。不能把旧结果复用为本轮成功。

取消恰好在拼接事务完成后、区域计算结束前到达时，当前代次完整拼接可被保留为结果，区域标记Cancelled供重建；其他过期结果不发布，但已提交文件登记在committedOutputs。保存已commit而通知过期时，磁盘可能存在产物，geometryConfirmed仍不置true。shutdown会join并记录能获知的提交；不会删除文件或假装回滚已完成事务。

## 6. 保存产物与限制

普通模式：stitch先在outputRoot下提交唯一`selected_plane_<time>_<uuid>`目录；saveGeometry在拼接产物目录下新建唯一`geometry_<time>_<uuid>`。每次普通保存创建新版本，不覆盖已有几何。

最终几何至少包含：完整`selected_plane_robot_base.ply/.pcvmap`、`selected_plane_manifest.json`、`selected_plane_report.json`、修剪`selected_plane_trimmed.ply/.pcvmap`和`selected_plane_geometry_v1.json`。修剪保留原序来源，WObj1、删点集合和人工干涉写入sidecar。完整拼接与最终几何是两笔事务，不承诺跨目录原子性。

可选契约保持当前SR-2026首次建模候选子集：正式目录由runtimeRoot/Job/Run确定，JSON为`point_cloud_result.json`，同run不得覆盖，修改后重新发布需新run并重新处理；缺真实扫描成果输出partial，显式提供无效成果则拒绝。另有base/候选PLY、0/128/255 PNG和原始XML/帧来源证据；契约名称base与内部robot_base表达同一已校验坐标系，不额外变换。规范见[契约](../contracts/data-contracts.md)及[Schema](../contracts/point_cloud_result_v1.schema.json)。

失败/取消撤销当前未提交事务，先前拼接目录可能保留；JSON最后发布不是断电级文件系统原子承诺。`formal_output`只表示提交，质量策略仍为warn_only；completed不意味着模板已发布、精度达标、可达或机器人可执行。相机真机、生产大样本、物理精度和部署不属于本轮接口整理验收。

## 7. 本轮变更与验证

- `saveGeometry()`成为唯一会话保存入口；删除仅转发的`confirmGeometry()`方法，保留同名UI对象、按钮文字与行为，测试改用保存入口。
- 删除无调用的`processingBudget()` getter；共享预算仍在内部管理，未改变数据结构布局。
- 会话声明与实现按相同职责顺序组织；补全输入、帧、区域、输出及契约注释，保留主要签名、单一通知和既有算法。
- 实际构建/测试结果及回滚点统一追加[本轮记录](../archive/changelog.md#scan-geometry-interfaces)，不将历史结果当本轮通过。
