# 点云 I/O 模块

`pcv_io` 是加载 PLY 点云的唯一可复用边界。

## 支持的输入

- ASCII PLY
- Binary little-endian PLY
- Binary big-endian PLY
- 任意标量属性顺序
- 可选 `nx`、`ny`、`nz` 法向属性

读取器拒绝缺少 `x`、`y` 或 `z` 属性、不支持的标量类型、vertex list 属性、截断载荷和错误文件头。

## 缓存契约

缓存文件位于应用本地缓存目录。只有格式版本、源文件大小、源文件修改时间、点数和载荷
长度全部有效时才接受缓存。缓存使用 `QSaveFile` 写入，避免崩溃后把部分文件当作有效缓存。

当前公共头文件导出的读取 API 位于 `pcv::detail::io` 命名空间，调用
`pcv::detail::io::readPly` 和 `pcv::detail::io::readPlyCached`；Qt 应用、测试和诊断工具
均通过该共享边界复用实现。

## 第一阶段加载优化

- `pointcloudview` 异步加载统一使用共享 `pcv::detail::io::readPly`，不再维护应用层解析器。
- 读取器一次分配声明的顶点数组并按索引写入，避免重复 `QVector::push_back` 扩容。
- 解析时累计有限 XYZ 边界并随结果返回，UI 不再为设置 Z 颜色范围二次扫描数百万点。
- ASCII/二进制格式、点顺序、可选法向和错误处理保持不变。

## ASCII 解析优化

- ASCII 顶点值使用不依赖区域设置的原地浮点解析器，支持有符号小数和科学计数法，避免每个字段都初始化 `strtof`。
- 根据 Header 属性位置计算 `lastRequiredIndex`；最后一个必需坐标/法向属性之后的值不再转换，只跳过尾部文本。
- 读取器仍拒绝错误数字、非有限值和溢出浮点值，保持顶点顺序和二进制解析不变。

## ASCII 内存映射载荷

- 解析文件头后，通过 `QFile::map` 读取 ASCII 顶点载荷，并用指针运算原地扫描，减少大文件逐行系统调用和临时字节数组分配。
- 直接在映射缓冲区定位换行边界，同时保持现有无区域浮点解析器和点顺序。
- 映射不可用时回退缓冲逐行读取，兼容网络文件和不支持映射的平台。

## 阶段一：加载基线

- 成功读取时，`PlyReadResult` 记录 `headerElapsedMs`、`boundaryScanElapsedMs`、`parseElapsedMs` 和 `totalElapsedMs`。这些仅用于诊断，不改变解析器或 UI 行为。
- 应用记录读取器耗时和缓存发布边界；VBO 上传耗时仍属于独立渲染指标。启用分块并行解析前必须保留该基线。

## 阶段二：ASCII 分块边界

- 映射的 ASCII 载荷划分为四个预备块。候选字节切分点前移到下一个完整换行，保证顶点行不会跨两个块。
- 轻量二次扫描统计各块行数，并验证总数等于 Header 顶点数。

## 阶段三：并行解析

- 映射的 ASCII 顶点载荷按已验证块并行解析。各 worker 只写预分配点数组中的固定不重叠区间，并维护私有边界和错误状态。
- 第一次边界扫描在声明的顶点行数后停止，避免把后续 face 或其他元素误认作顶点。
- 全部 worker 汇合并合并点数和边界后才发布结果；映射失败仍使用串行缓冲回退。
- worker 的具体解析或取消错误会保留到最终点数校验，不会被通用“不完整数据”消息覆盖。
- `ply_reader_tests` 可接收外部 PLY 路径，在内建回归通过后报告格式、点数、边界和读取耗时，为大型生产数据提供可重复基线。

## 阶段四：自适应 ASCII 并行度

- 小载荷选择 1 个 worker，中等载荷最多 2 个，大载荷最多 4 个；同时受 `std::thread::hardware_concurrency()` 限制，避免低核心系统过载。
- `PlyReadOptions::asciiWorkerCount` 可强制使用 1 到 8 个诊断 worker；生产调用保持 `0` 以使用自适应行为。
- 映射回退保持单线程。实际数量写入 `PlyReadResult::asciiWorkerCount`，测试程序可用第二参数执行可重复的 1/2/4 worker 对照。

## 阶段五：紧凑 binary XYZ 快速路径

- 顶点布局严格为连续 `float x/y/z` 的 Binary PLY 使用映射和按索引分区的读取器。各 worker 把固定点区间直接解码到预分配 `Point3D` 数组，并维护私有边界。
- 紧凑二进制路径使用与 ASCII 相同的自适应 1/2/4 worker 策略；二进制 worker 数仅保留在读取器内部，不暴露到跨模块结果结构，以保持 Qt Creator 增量构建时的公共 ABI 稳定。
- 含法向、颜色、强度、属性重排或其他标量布局的文件继续走通用标量读取器；映射失败也回退该路径，保持 binary little/big-endian 兼容性。
- 调用方未提供进度回调时跳过逐点原子进度计数；取消轮询和最终点数校验仍独立生效。
- 两个 GUI processor 翻译单元都把 `ply_reader.h` 声明为显式 CMake `OBJECT_DEPENDS`。本地化 MSVC `/showIncludes` 在 Qt Creator Makefile generator 下可能留下空依赖文件；显式依赖可防止新读取库与按旧结果布局编译的调用方链接。
