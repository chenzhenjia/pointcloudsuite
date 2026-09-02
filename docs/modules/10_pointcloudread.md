# 10_pointcloudread：点云读写

状态：已实现。规范实现位于 `modules/10_pointcloudread/{include,src}`，模块 target 为 `pcv_m10_pointcloudread`；`pcv_io` 为迁移期兼容 target。

职责：PLY ASCII/binary LE/BE 读取与写入、属性校验、缓存、取消、进度、点序和 bounds 保持。不得依赖应用或 Qt Widgets。

接口/测试：`pcv::detail::io::readPly`、`readPlyCached`；`ply_reader_tests`、`cloud_cache_tests`。

最后核对日期：2026-09-01。当前状态以源码、CMake 和测试为准。

## 变更记录

### 2026-09-01

- binary 读取并行快速路径泛化：原 compactFloatXyz 仅支持"恰好 3 个 float 且顺序为 x,y,z"，现扩展为"全部属性固定宽度 + x/y/z 为 float32"即按固定 stride mmap + 多线程按列偏移解码，覆盖 4-float（x,y,z,intensity）、6-float（x,y,z,nx,ny,nz，含 canonical 缓存文件）与乱序属性，worker 数沿用自适应选择（上限 4 核），并支持 PlyReadOptions::binaryWorkerCount 显式指定。
- 新增可选强度通道：PlyReadOptions::readIntensity 开启且文件声明 intensity 属性时，PlyReadResult::intensity（与 points 等长的 QVector<float>）被填充；ASCII 与 binary（快速/慢速）路径语义一致，uchar/ushort/double 等类型归一化为 float。未开启或文件无 intensity 时保持空数组，不改变 Point3D 布局与既有结果结构。
- PlyReadResult::binaryWorkerCount 记录 binary 快速路径实际 worker 数（慢速路径为 1）。
- 测试：ply_reader_tests 新增 4-float LE/BE、乱序属性、6-float 法向、uchar/ASCII intensity 与显式/自适应多线程 worker 用例。

性能优化（同日）：

- P0：PlyReadResult 新增 fastPath 标记，明确区分并行固定 stride 路径与串行保底路径，便于诊断是否走快速路径。
- P1：selectWorkerCount 阈值放宽——点数 >= 10 万或 payload >= 4MB 直接使用全部 4 核；>= 2 万点或 >= 1MB 使用 2 核；低于阈值仍单线程避免线程启动开销。
- P2：decodeBinaryFloat 内联；连续 float32 x,y,z 且 little-endian、stride 4 对齐时按 float[3] 块直接读取（去逐列调用）；标量/intensity 解码改为按整数 kind 预分派的 switch（去除每点 QByteArray 字符串比较）；并行 chunk 的进度原子计数从每点一次降为每 1 万点一次（尾部余数补计）。
- P3：固定宽度属性统一走并行解码——mmap 失败时改为整块读入后仍按 stride 分块并行，非 float32 x/y/z（如 double）同样并行；仅 list 等非常规布局保留串行保底。
- 测试：ply_reader_tests 为各快速路径用例新增 fastPath 断言，并新增 double x,y,z 并行解码用例。

缓存读取优化（同日）：

- P0：sourceFingerprint 使用完整文件 SHA-256；`readPlyCached()` 单次生成 `SourceIdentity` 并复用到路径、校验和写入，缓存版本升至 3，避免未采样区域修改返回旧点云。
- P1：writeBinaryPly 使用整块写；`Point3D` 增加 standard-layout/六个连续 `float` 的 `static_assert`，带/不带 normals 的完整字段回读测试列入后续路线图。
- P2b：readPlyCached 中 canonical PLY 写失败不再清空结果/整体失败；结果保持 `ok=true`，并通过 `CachedCloudResult.warnings` 透传到应用层，同时记录 `qWarning`。
- 测试：cloud_cache_tests 覆盖首次解析、canonical 生成、二次缓存命中和完整指纹失效；PLY writer 回归覆盖 binary little-endian 带/不带 normals 的逐字段回读。

ABI 与事故记录（同日）：

- 本次对 `PlyReadOptions`（新增 binaryWorkerCount、readIntensity）和 `PlyReadResult`（新增 intensity、binaryWorkerCount、fastPath）的扩展改变了结构体尺寸，属于**破坏性 ABI 变更**：仍按旧布局编译的调用方目标文件与新的 `ply_reader.cpp.obj` 链接在一起时，`readPly()` 按新尺寸写入返回值，会越过调用方为返回值预留的栈槽。
- 实际故障：`pointcloudview.exe` 打开扫描 JSON 时弹出 `Run-Time Check Failure #2 - Stack around the variable 'canonical' was corrupted`。`canonical` 是 `cloud_cache.cpp` 中 `readPlyCached` 的局部变量，位于返回值槽相邻位置；调用链为 `openTempScanningInfo` → `prepareTempWorkpiece` → `readPlyCached` → `readPly`。
- 根因不在算法逻辑，而在构建树：Qt Creator 的 `NMake Makefiles JOM` 目录未跟踪头文件依赖（全部 `*.obj.d` 为 0 字节，`compiler_depend.make` 中每个目标文件只依赖同名 `.cpp`），`ply_reader.h` 变更后 `cloud_cache.cpp.obj`、`ply_writer.cpp.obj` 没有重新编译，与新编译的 `ply_reader.cpp.obj` 混装进同一个 `.lib`。单元测试可执行文件在头文件变更之后重建，自身布局自洽，因此全部通过、无法暴露该问题。
- 历史处置已被 `VSLANG=1033` 策略替代：配置和构建阶段统一固定 MSVC `/showIncludes` 语言，并保留依赖数据库护栏。详见 `docs/development/build-windows.md`。
- 验证：新建 `NMake Makefiles` 构建目录后，`.obj.d` 中出现 `ply_reader.h`；基线全量 CTest 13/13 通过，相机开关 ON 的全量 CTest 17/17 通过。`NMake Makefiles JOM` 在当前环境生成空 `.obj.d`，不作为支持生成器。
- 约束：今后扩展 `PlyReadOptions`/`PlyReadResult` 必须把新字段追加在结构体末尾，并在本文件变更记录中显式标注"需要全量重建"。

### 2026-08-31

- 建立模块边界和 `pcv_m10_pointcloudread` 兼容入口；源码暂留 `src/io`，未移动用户未提交修改。
- M2：PLY 读写、缓存和 writer 已迁移到 `modules/10_pointcloudread/{include,src}`，旧 `pcv_io` 与头文件作为兼容层保留。
