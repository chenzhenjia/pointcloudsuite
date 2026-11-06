# 新 GUI 加载计时、首显优化与日志 v1

日期：2026-11-05。owner：应用层 / 模块20，共享任务库；10/60仅计时增量。状态：源码交付，用户执行构建、CTest、原生 GUI 与性能验证。未产生新版 EXE，无实测加速结论。

## 行为与接口

新 GUI 的加载拆为读取/转换/摘要/高度范围/展示抽样与独立 PrepareIndex。LoadDisplay 接受后立即发布展示，完整索引后台使用同一完整点集，不重读、不重新转换。索引失败保留点云，重试按钮只重建索引。深度源仍先二维预览，点击当前帧处理才物化 XYZ；确认后释放 XYZ 的既有行为保留。

当前帧索引就绪即可选面、确认，同轮其他帧可继续预加载。最多两个帧任务，各两个计算 worker；前台选面进入优先队列，停止派发新预取，已运行任务安全收敛。三点输入未完成时暂停新预取，Esc 解除；切帧提升目标帧加载/索引优先级。跨轮、配置和重置仍等待任务收敛。每帧 loadAttempt 与现有 id/input revision/reference revision/token 共同门控，迟到结果不清除新任务状态。

共享 SelectedPlaneTaskV2 增加 XML、SDK 物化、摘要、完整有限 Z 范围和展示预算申请计时及高度范围元数据。旧调用方保留默认30万展示上限，未执行的新增阶段为 -1，日志输出 null。prepareHeightRangeV1 可取消；仅新 GUI 加载及结果任务调用。新 GUI 画布 publish 可接收已计算高度范围，避免 GUI 线程扫描完整数据；结果修剪使用完整结果固定范围。

模块20新增默认未设置的 displayTiming(revision, QJsonObject) 回调和只读 displayCloudRevision()。revision 只在点云替换时递增，面片或着色变化不递增。计时覆盖映射/校验/状态/marker/texture 缓冲准备、bounds 和 OpenGL 各缓冲上传调用。保持 QHash、缓冲策略、默认渲染和拾取契约；无 glFinish，不把 CPU 调用称为 GPU 完成。

浅灰背景、100万点/帧、六帧窗口、512 MiB 预算、完整选面与拼接、输出 schema、世界 Z 提醒不变。

## 日志入口与格式

右侧“加载耗时与日志”默认折叠，在 Designer 表单中可编辑。展开查看当前帧、本轮等待与最近200条 JSONL 记录；“打开日志目录”定位自动保存文件。界面记录有上限，磁盘保留本次会话全部事件，不自动清理历史日志。

默认路径由 QStandardPaths::AppLocalDataLocation 决定，应用为 PointCloudSuite/scan_geometry_demo_v1，子目录 logs/scan_geometry_load_v1。每次会话使用 UTC 时间和 UUID 独立 .jsonl 文件；若目录落在源码树内，改用系统临时目录 pcv_load_logs_v1。写入失败在耗时区显示，内存日志仍保留。日志写入在 GUI 线程，每任务仅少量事件，不记录逐点正文；大量历史文件需用户自行管理。

事件含 UTC、level、input_revision，帧事件含 frame_id/attempt，任务事件带 kind/reference revision、成功/失败/取消/过期状态、错误/警告和已提交产物路径。操作事件表示收到操作请求，是否成功应结合 task_finished、状态及成果事件判断。没有安装全局 Qt message handler，不捕获底层 Qt/OpenGL 普通输出；模块20计时回调中的上传错误会进入日志。异常进程终止前已写入的事件可用于排查，不能保证最后一条事件持久化到物理介质。

主要事件：session、operation、load_requested、task_started/task_finished、load_worker、display_ready、index_dispatch/index_worker/index_ready、canvas_publish_cpu、gl_upload_cpu、first_frame_swapped、round_finished、close_attempt、session_closed。task kind 数值对应 SelectedPlaneTaskV2::Kind。会话记录 Qt、编译日期、Debug/Release、CPU 架构与 OS；CPU 型号/GPU/磁盘由测量者登记。

应用排队、线程池排队、worker、GUI 接收等待分别计时。input_ms 包含源指纹、缓存、解析和写缓存，transform_ms 包含 merge，index_ms 包含 grid/normals；不能将包含或并行阶段相加作为总耗时。缓存命中仍完整计算源指纹；不放宽来源校验，不清理已有缓存。缓存命中时解析/boundary 未执行字段为 null；header_ms含缓存身份校验所需头读取，冷读还包含解析器头读取。模块10只补头读取计时，缓存契约不变；模块60增加budgetAcquireMs统计既有预算申请，index_budget_ms包含在grid/normals和总准备中，不改变索引算法。

first_frame_ms 从本次请求到观察到当前点云 frameSwapped（并核对实际绘制 revision），隐藏帧/失败帧为 null。selectable_ms 为完整索引就绪里程碑，不包含其他前台任务占用导致的短时 UI 门控；实际按钮同时检查 canSelectActive。round_finished 统计当前轮数据与索引任务收敛，包含后台加载与用户暂停派发的等待，不等待后续选面业务完成。取消/失败不记 ready。

## 构建与测试命令（用户执行）

在已初始化 MSVC x64 和 Qt 环境的 PowerShell 中，使用全新源码树外目录。下面目录名仅为建议，勿复用旧缓存。

```powershell
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v1-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v1-debug
ctest --test-dir D:/pcv-build/load-performance-v1-debug --output-on-failure
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/load-performance-v1-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<QtDir> -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/load-performance-v1-release
ctest --test-dir D:/pcv-build/load-performance-v1-release --output-on-failure
```

公共 DTO 和 PointCloudCanvas 头变化必须完整重编，新GUI单独启用/旧GUI启用配置均需回归。相机源验收按项目现有 SDK 开关另行启用。定向 CTest 使用 `-R "scan_geometry_(session|view)_tests_v2|pointcloud_canvas_contract_tests"`，不能替代全量。

新增测试覆盖：发布后未有索引、索引就绪开放选面、索引失败保留展示、后台任务不阻塞当前帧门控、前台排队且并发不超2、迟到 attempt 不清除新索引状态、完整 Z/常量范围/取消、首显 attempt 去重、JSONL 解析、折叠日志面板、发布分段计时以及仅配色变化不重传。沿用现有2/6/7/13帧、取消重试、映射/来源、配色、面片与修剪测试。测试源码已追加，尚未运行，不以静态检查代替通过。

## 真实测量与验收

1. Release 记录构建、Qt、CPU型号、GPU驱动、磁盘/网络路径、文件格式/大小/点数及缓存命中。测试前后保持同样输入、位姿、XML、显示点数与预算。
2. 分别记录首次无应用缓存和重复加载各5轮；不自动删除已有缓存，首次加载不等于 OS 冷缓存。真实首次样本需选择尚未生成应用缓存的独立测试文件或保留相应证据。
3. 分别比较首显、当前帧索引就绪、整轮就绪的中位数与范围。测试中统一是否操作/暂停预取；隐藏帧 first_frame_ms 为 null，不填0。保留取消和失败样本，不混入成功分组。
4. 点云出现后立即旋转缩放，索引前选面禁用。当前帧就绪后其他帧仍加载时选面/确认；检查前台任务优先、跨轮保护、取消/重试和关闭等待。深度输入保持二维按需处理。
5. 检查日志错误、完整点序/来源映射及正式产物一致；工件成功后灰色、蓝红面片和结果高度色、修剪固定色阶、旧工作台默认外观保持。
6. Qt Creator 编辑新增日志控件，检查最小窗口、100%/150% DPI、百万点流畅度。OpenGL CPU 提交耗时不能代表独立 GPU 完成耗时。

## 实际验证、风险与回滚

本轮已完成静态检查：Designer XML及控件名唯一性、77个窗口UI引用、修改C++分隔符检查、开发/应用/target文档链接与git diff --check通过；核对已有32个删除项不变，本轮没有新增删除。分隔符检查不等于编译。未配置/构建、未运行测试、GUI或性能采样。索引、缓存指纹、解析和GPU上传谁占主要耗时仍待日志确认。先显后索引可能缩短首显，但后台索引仍竞争CPU和内存带宽，不承诺整轮提速。同步小量日志也有I/O开销；若日志盘慢应在实测中区分。

改动集中于新GUI会话/画布/表单、共享任务元数据、模块20可选计时、10/60仅计时增量与定向测试。回滚需只撤回本功能增量并完整重编，保留先前配色、应用清理、RAW采集和契约适配等未提交改动。无提交/推送/上传，无软件/schema升级。
