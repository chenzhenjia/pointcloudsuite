# PointCloudSuite Agent 工作手册

适用范围：D:\workpiece\pointcloudview\pointcloudview  
真相源：当前源码、CMake、测试；历史文档冲突时以实现为准。

## 0. Agent 配置

| key | value |
|---|---|
| agent.role | PointCloudSuite repository maintainer |
| agent.can | inspect \| modify requested files \| build \| test \| diagnose |
| agent.cannot | modify unspecified files \| delete/overwrite without confirmation |
| agent.source_of_truth | current source + CMake + tests |
| agent.communication | 中文；代码、命令、变量名、路径、错误信息保留原文 |
| agent.protected_data | 禁止写入 password、secret、token、API key、private key |
| agent.default_scope | 只处理明确指定的任务和文件；保留现有未提交修改 |

硬规则：

- 共享算法放入 src/，头文件放入 include/pcv/；共享库不得依赖 apps/ 或 Qt Widgets。
- 应用可依赖共享库；共享库不能反向依赖应用。
- 缓存、日志、导出和运行数据不得写入源码树。
- Windows 源码路径和构建路径只能使用 ASCII。
- 不把 build*/、test_pointcloud_a/、mid_gap/ 或旧报告当作稳定 API。
- 删除、批量修改、覆盖原始文件、外部上传、操作账号/数据库前必须先确认。

## 快速索引

| keyword | jump target | 用途 |
|---|---|---|
| 角色/边界 | 0. Agent 配置 | 查看 Agent 身份、能力、权限和不可违反的规则 |
| 任务路由 | 1. 任务路由 | 按任务类型确定检查文件、修改范围和最低验证 |
| 模块/API | 2. 项目速查；3. 关键共享 API | 查找目录职责、依赖矩阵、参数、结果和默认值 |
| 应用流程 | 4. 应用级关键 API | 定位 pointcloudview、pointcloudstitch 的处理入口和约束 |
| 错误/日志 | 5. 错误、日志和失败处理 | 判断返回状态、错误码、弹窗和日志级别 |
| 测试 | 6. 测试行为矩阵 | 查找测试覆盖、CTest 目标和新增测试步骤 |
| 风险/构建 | 7. 风险与重编译；8. 构建和验收命令 | 判断影响等级、重编译范围和验证命令 |

## 1. 任务路由

| task.type | inspect | modify | verify |
|---|---|---|---|
| 添加共享模块 | src/*、include/pcv/*、src/CMakeLists.txt | 头文件、实现、CMake、测试 | 模块测试、CTest、依赖边界 |
| 修改 PLY 读取 | ply_reader.h/.cpp、缓存、调用方 | 保持点序、源索引、bounds、取消、错误语义 | ASCII、binary LE/BE、属性重排、缓存、取消 |
| 修改滤波 | include/pcv/filtering/*、src/filtering/* | 保持真实点/质心和 source index | 空输入、非法参数、边界点数 |
| 修改平面输出 | plane_output.h/.cpp、契约文档 | PNG/PLY/JSON 成套输出、错误码 | 路径、灰度、矩阵、失败回滚 |
| 修改 UI/异步 | mainwindow.*、.ui、processor.* | 工作线程不碰 QWidget/OpenGL | uic、Debug build、关闭烟测 |
| 修改拼接/ICP | apps/pointcloudstitch/*、报告字段 | 不放宽验收、不伪造成功 | 手眼测试、回归报告、诊断输出 |
| 添加测试 | tests/CMakeLists.txt、tests/unit/* | add_executable + add_test | 单测、CTest、Qt PATH |
| 只改文档 | 目标文档和源码事实来源 | 不改业务代码 | git diff --check、结构检查 |

## 2. 项目速查

### 技术配置

| key | value |
|---|---|
| project.name | PointCloudSuite |
| language | C++17；CMAKE_CXX_EXTENSIONS=OFF |
| platform | Windows 10/11 + MSVC |
| cmake.min | 3.19 |
| qt | 6.5+；当前 preset 为 C:\Qt\6.8.3\msvc2022_64 |
| qt.components | Core;Gui;Widgets;Concurrent;OpenGL;OpenGLWidgets |
| options | PCV_BUILD_POINTCLOUDVIEW=ON；PCV_BUILD_POINTCLOUDSTITCH=ON；PCV_BUILD_TOOLS=OFF；PCV_BUILD_TESTS=OFF |
| external.algorithm_deps | 不依赖 Python、Open3D、PCL、VTK、CUDA |

### 目录和依赖矩阵

| path/target | responsibility/type |
|---|---|
| apps/pointcloudview | Qt Widgets + OpenGL 查看、处理、平面/障碍/边缘 |
| apps/pointcloudstitch | 手眼转换、多帧 ICP、接缝融合 |
| src/core | Point3D 基础类型 |
| src/infrastructure | 应用数据、缓存、日志、导出目录 |
| src/io | PLY 读取和缓存 |
| src/filtering | 比例、体素、统计滤波 |
| src/output | 平面 PNG/JSON/PLY 契约 |
| tests | CTest 单元测试 |
| tools/registration_diagnostic | 默认关闭的诊断 CLI |
| pcv_core | INTERFACE；Qt::Core |
| pcv_infrastructure | STATIC；Qt::Core |
| pcv_io | STATIC；pcv_core、pcv_infrastructure、Qt::Core |
| pcv_filtering | STATIC；pcv_core、Qt::Core |
| pcv_output | STATIC；pcv_core、pcv_infrastructure、Qt::Core、Qt::Gui |
| pointcloudview | executable；全部 pcv_* + Qt GUI/OpenGL |
| pointcloudstitch | executable；全部 pcv_* + Qt Core/Gui/Widgets/Concurrent |
| registration_diagnostic | executable；pcv_* + 直接编译 pointcloudview processor |

## 3. 关键共享 API

### pointcloud::Point3D

文件：include/pcv/core/point_types.h

| field | type | meaning |
|---|---|---|
| x/y/z | float | 点坐标 |
| nx/ny/nz | float | 可选法向，默认 0 |

修改字段会影响所有应用、测试、缓存二进制布局和直接编译目标。

### pcv::detail::io

文件：include/pcv/io/ply_reader.h、cloud_cache.h

| API | inputs/default | success | failure/cancel |
|---|---|---|---|
| readPly | fileName；progress、isCancelled；asciiWorkerCount=0 自适应 | ok=true、points、bounds、format、计时 | ok=false、error；取消时 cancelled=true |
| readPlyCached | 源文件、可选 cache dir、读取选项 | points、usedCache、ok | 缓存失效自动重读；失败返回 error |
| cacheFilePath | 源文件、可选 cache dir | ply_<basename>_<sha256-prefix>.pcvbin | 只计算路径，不读缓存 |

readPly 支持 ASCII、binary little-endian、binary big-endian；按属性名读取 x/y/z，可选 nx/ny/nz，属性顺序可变。拒绝缺坐标、未知 scalar type、vertex list、坏 header、坏数字、非有限/溢出值、截断 payload。

ASCII 映射路径按完整换行切 chunk，自适应 1/2/4 worker；asciiWorkerCount>0 用于 1..8 诊断覆盖；映射失败回退单线程。连续 float x/y/z 的 binary PLY 有内部 mapped 快速路径，其他布局走通用解析器。

PlyReadResult 关键字段：ok、error、cancelled、points、declaredPointCount、format、minimum、maximum、hasBounds、headerElapsedMs、boundaryScanElapsedMs、parseElapsedMs、totalElapsedMs、asciiWorkerCount。

调用规则：成功先检查 ok 再发布 points；取消保留 cancelled；共享读取器不访问 GUI；修改 PlyReadOptions/PlyReadResult 后必须完整重编译调用方。缓存校验 magic/version、源文件 size、mtime、point count、payload length，写入使用 QSaveFile。

### pcv::detail::filtering

文件：include/pcv/filtering/*、src/filtering/*

| API | defaults | result/fallback |
|---|---|---|
| proportionalDownsample | denominator<=1 等同 1 | 按输入顺序保留真实点；空输入返回空 |
| voxelDownsample | voxelSize<=0 等同不降采样；FirstInputPoint 或 Centroid | 返回点和首个源 sourceIndices；非法尺寸返回原点及索引；默认丢无效点 |
| removeStatisticalOutliers | meanK=45、stddevMultiplier=1.3、cellSize=0 自动估计 | 邻域不足或结果为空时保留原点云并返回 warning |

不得悄悄生成虚拟点；质心必须显式选择 VoxelRepresentative::Centroid。

### pcv::output

文件：include/pcv/output/plane_output.h、src/output/plane_output.cpp

关键结构：

    JobContext: runtimeRoot, jobId, workpieceId, baseName, destinationDirectory(optional)
    PlaneOutputMetadata: sourcePointCloud, sourcePlyEncoding, origin/axisX/axisY/axisZ,
      abcDeg, TBaseWorkpiece, TWorkpieceBase, planeEquation, rmsErrorMm,
      distanceToleranceMm, diagnostics
    PlaneOutputResult: success, errorCode, message, planePng, planeJson,
      planeRobotBasePly, exportedPointCount

validateJobContext 拒绝空组件、.、..、路径分隔符、绝对路径和包含 .. 的组件。

writePlaneOutput 状态机：

    validate context
      -> validate image / points / planeIndices
      -> create output directory
      -> write PNG: Grayscale8，非零 => 255
      -> write binary_little_endian PLY
      -> write JSON atomically with QSaveFile
      -> success=true

默认目录：runtimeRoot/jobs/<job_id>/point_cloud/plane/；有 destinationDirectory 时直接输出到该目录。PNG、PLY、JSON 必须成套，JSON 最后提交。

| error code | trigger |
|---|---|
| PCV_CONTRACT_001 | job 参数或路径组件非法 |
| PCV_IMAGE_001 | 图像为空或尺寸无效 |
| PCV_PLANE_001 | 平面点/索引不足或拟合失败 |
| PCV_FRAME_001 | 工件坐标矩阵不可逆 |
| PCV_OUTPUT_001 | 运行目录创建失败 |
| PCV_OUTPUT_002 | PNG、PLY 或 JSON 任一写入不完整 |

plane_output.h 还声明 PCV_INPUT_001/002、PCV_TRANSFORM_001/002、PCV_STITCH_001 等常量；新增错误码前先检查现有定义。

## 4. 应用级关键 API

### pointcloudview

文件：apps/pointcloudview/pointcloudprocessor.h

| API | purpose | result/failure |
|---|---|---|
| loadPlyResult | 直接后台读取源 PLY | LoadResult；失败不发布画布 |
| loadPlyCachedResult | 读取验证缓存 | LoadResult.usedCache；缓存失效自动重读 |
| extractPlaneFromThreePoints | 三点初始面 + RANSAC/PCA/连通域 | ThreePointPlaneResult；共线、内点不足、索引/网格非法失败 |
| buildWorkpieceCoordinateSystem | 建立右手工件坐标系 | WorkpieceCoordinateSystem；点近/共线/矩阵不可逆失败 |
| extractPlaneImage | 平面映射到工件 XY 图像 | PlaneImageResult；无效/非平面/越界/像素超限被拒绝 |
| detectObstacles | 2.5D 平面两侧障碍 | ObstacleDetectionResult；障碍是非阻断告警 |

默认值：障碍高度 1.0 mm、最少 30 点、面积 4.0 mm²；图像 margin 50 mm、pixel size 0.05 mm、round increment 10 mm、maximumImagePixels 100000000。

异步约束：工作线程不访问 QWidget/OpenGL；发布前校验画布/坐标系版本和关闭状态；关闭时断开 watcher 信号、等待任务、再释放大 QVector；UI 用 QMessageBox，算法层只返回结果。

### pointcloudstitch

文件：apps/pointcloudstitch/handeye_transform.h、pointcloudprocessor.h、seamfusion.h

| API | purpose | key contract |
|---|---|---|
| loadHandEyeCalibration | 读取 XML RTmatDepth2robot/RotMat/TVec | 不执行标定求解；失败返回 error |
| transformLineScanToRobotBase | 线扫点转 robot_base | T_base_flange(t) * T_flange_depth * p_depth；支持取消、源索引、旋转插值 |
| mergePlyCloudsInWorld | 多帧转换、相邻配准、ICP | 配准至少 2 帧；按输入顺序 N -> N-1 |
| applyTrajectorySeamFusion | ICP 后真实重叠接缝 | 无双侧真实重叠时禁止破坏性裁剪 |

验收：累计修正默认不超过 10 mm / 1°；所有相邻帧须通过结构点双向 3 mm 覆盖，最低 0.65；接缝默认 half width 8 mm、mutual distance 0.6 mm、decision cell 0.5 mm；失败只能保留诊断文件，不得生成/复用旧正式 stitched_robot_base.ply。

## 5. 错误、日志和失败处理

### 共享库

- 不弹窗、不访问 QWidget/OpenGL。
- 使用 Result.ok/error/cancelled 或等价字段返回状态。
- 保留 worker 具体错误，不用末尾通用错误覆盖。
- 可恢复问题写 warning/summary/diagnostics，但不能报告为成功。
- 跨模块判断使用稳定 PCV_* 错误码。

### 应用

- 输入、文件选择、参数错误：QMessageBox::warning。
- 不可继续的加载/配准/导出失败：主线程明确提示，必要时 QMessageBox::critical。
- 性能和生命周期用 qInfo；异常和丢弃结果用 qWarning。
- 后台任务不能直接更新 GUI。

### 输出

- PNG、PLY、JSON 任一失败，整体 success=false。
- JSON 最后写入并使用 QSaveFile。
- 路径必须经过 JobContext 校验；禁止路径穿越。
- 日志和报告不得写入受保护凭据。

## 6. 测试行为矩阵

| module | success | failure/boundary | target |
|---|---|---|---|
| PLY reader | ASCII、binary LE/BE、属性重排、法向、点序/bounds | 缺字段、坏数字、非有限、截断、取消 | ply_reader_tests |
| Cache | 首次解析、命中、源文件变化失效 | magic/version/size/mtime/payload 错误 | cloud_cache_tests |
| Downsample | 比例、首点体素、质心体素、source index | 空输入、非法尺寸、无效点 | downsample_tests |
| Statistical | 邻域充足、离群点移除 | 点数/邻域不足、空结果回退 | statistical_filter_tests |
| Plane output | 三件套、相对路径、JSON、Grayscale8、0/255 | 非法上下文、空图、空索引、不可逆矩阵、写入失败 | plane_output_tests |
| Pointcloudview app | 障碍区域、正/负侧、边界 | 无效平面、连通域边界、网格限制 | pointcloudprocessor_obstacle_tests |
| Stitch | XML、刚体矩阵链、姿态转换、旋转插值、源索引 | 非刚体、矩阵/姿态非法、取消 | handeye_transform_tests |

当前 Debug CTest 目标：ply_reader_tests、cloud_cache_tests、downsample_tests、statistical_filter_tests、plane_output_tests、pointcloudprocessor_obstacle_tests、handeye_transform_tests。

添加测试：

1. 在 tests/unit/<module>/ 新建源文件。
2. 在 tests/CMakeLists.txt 添加 add_executable、依赖和 add_test。
3. 直接编译 app 源文件时显式配置 include/source/link，并说明原因。
4. 覆盖成功、错误、边界、取消和回退路径。
5. 先跑单测，再跑完整 CTest。
6. 涉及公共头、CMake、UI 或 processor 时补充 Debug build；涉及发布时补充 Release build。

## 7. 风险与重编译

### P0：完整重编译 + 全量测试

| path | risk |
|---|---|
| include/pcv/io/ply_reader.h | 公共结构体 ABI；两个应用、测试、缓存调用方受影响 |
| apps/*/pointcloudprocessor.{h,cpp} | 体量大；算法/UI 集中；测试和工具直接编译 |
| include/pcv/output/plane_output.h、src/output/plane_output.cpp | 输出契约、错误码、格式和下游兼容 |
| 根 CMakeLists.txt、tests/CMakeLists.txt | 目标、依赖、测试注册变化 |
| .ui、watcher/closeEvent | uic、控件绑定、线程关闭、堆安全 |

### P1：模块测试 + 相关应用构建

src/io/ply_reader.cpp  
src/io/cloud_cache.cpp  
src/filtering/*  
apps/pointcloudstitch/handeye_transform.cpp  
apps/pointcloudstitch/seamfusion.cpp

### P2：静态检查或局部测试

docs/*、examples/*、非契约性 README、不改变读取语义的诊断报告字段。

重编译规则：

    public header changed    -> clean/reconfigure or full build
    CMake changed            -> reconfigure + target build
    processor header changed -> app + direct-source tests + tools
    output contract changed  -> output tests + compatibility review

两个应用的 pointcloudprocessor.cpp 通过 CMake OBJECT_DEPENDS 显式依赖 include/pcv/io/ply_reader.h；不要依赖本地化 MSVC /showIncludes 的隐式依赖。

## 8. 构建和验收命令

    .\scripts\build_windows.ps1 -Config Release
    .\scripts\build_windows.ps1 -Config Debug -BuildTests
    .\scripts\run_tests.ps1 -BuildDir C:\qt-build-pointcloudsuite

默认：QtDir=C:\Qt\6.8.3\msvc2022_64；BuildDir=C:\qt-build-pointcloudsuite；Generator=NMake Makefiles。

Preset：

    cmake --preset windows-msvc-debug
    cmake --build --preset build-debug
    ctest --preset test-debug

文档变更最低验收：

    git diff --check -- AGENTS.md
    git status --short

## 9. 入口、限制和修改汇报模板

推荐入口：README.md、CMakeLists.txt、CMakePresets.json、docs/architecture/*、docs/requirements/*、include/pcv/*、src/*、apps/pointcloudview/pointcloudprocessor.h、apps/pointcloudview/mainwindow.cpp、apps/pointcloudstitch/README.md、apps/pointcloudstitch/handeye_transform.h、apps/pointcloudstitch/stitchingwindow.cpp、tests/CMakeLists.txt、tests/unit/*。

限制：

- 千万级点云仍受 CPU 内存、映射能力和 OpenGL 显存限制；读取器计时不等于 GUI 总耗时。
- pointcloudstitch ICP 是局部优化；错误位姿、Start/End 方向或手眼矩阵不会自动修复。
- 当前 src/output/plane_output.cpp 和 plane_output_tests 以 PNG 前景 255、binary little-endian 平面 PLY 为准；旧文档中的 225 等描述可能过时。
- 当前实际共享模块包含 pcv_output；旧架构文档若只列四个模块，以 CMake 和目录为准。
- test_pointcloud_a/、mid_gap/ 是实验/回归产物；提交前检查是否应忽略。
- 任何操作前先执行 git status --short；不要使用 git reset --hard 或覆盖无关文件。

汇报模板：

    做了什么：
    - ...
    改了什么：
    - 文件：...
    - 关键行为/API：...
    如何验证：
    - 命令：...
    - 结果：...
    风险/未验证项：
    - ...
