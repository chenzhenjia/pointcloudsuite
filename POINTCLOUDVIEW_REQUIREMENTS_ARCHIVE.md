# pointcloudview 当前版本需求与实现档案

版本：基于 Git `e5239e4` 及其父提交的当前实现

文档用途：本档案描述当前代码的实际功能、接口、数据格式、算法、界面、线程模型、构建方式和限制。按照本档案重新建立 Qt 工程，应能够复现当前版本的主要行为。文档中的“已实现”以源码为准；“限制”和“未实现”不会被当作现成功能。

最后核对文件：

- `main.cpp`
- `mainwindow.h/.cpp/.ui`
- `pointcloudprocessor.h/.cpp`
- `registration_diagnostic_runner.cpp`（可选诊断工具）
- `tests/pointcloudprocessor_obstacle_tests.cpp`（障碍检测算法测试）
- `CMakeLists.txt`
- `CMakePresets.json`
- `build_windows.ps1`
- `clean_reconfigure.ps1`
- `QT_CREATOR_BUILD.md`
- `hand_eye/MV-DP2240-01P_Hand_Eye_Convert/standard_transform_gui.py`

---

## 1. 产品定义

### 1.1 产品名称

`pointcloudview`：Qt 6 点云加载、显示、清理、Eye-in-Hand 世界坐标转换、点云配准、2.5D 平面提取、平面边缘分割和二维图像导出工具。

### 1.2 当前版本目标

当前版本不是机器人运动控制器，也不直接控制机器人或相机 SDK。它处理已经落盘的 PLY 点云，提供：

1. 单个 PLY 打开和文件夹 PLY 扫描。
2. 当前画布缓存的 OpenGL 点云显示和交互视角。
3. 体素降采样和统计离群值处理。
4. 三点 GPU 精确拾取和 2.5D 平面提取。
5. 基于二维栅格的平面边缘、孔洞轮廓和 2D 图像。
6. Eye-in-Hand 标定文件和机器人扫描 Start/End 位姿驱动的世界坐标转换。
7. 机器人位姿粗配准后可选 ICP 精配准。
8. 几何特征、平面、圆、相似圆、倒角等处理 API，供后续业务集成。

### 1.3 明确不包含

- CUDA、PCL、VTK 或第三方点云库。
- 机器人底层通信、运动控制、IO、急停或工艺执行。
- 相机实时取流和采集触发。
- 自动获得每个 PLY 点的真实采集时间戳。
- MES、数据库、云服务和网络协同。
- 将配准结果导出为新的 PLY 文件的专用 UI。

---

## 2. 技术与运行基线

| 项目 | 当前约定 |
|---|---|
| 语言 | C++17 |
| UI | Qt 6 Widgets、Qt Designer `.ui` |
| Qt 版本 | Qt 6.8.3 |
| 编译器 | MSVC 2022 64 位 |
| 并发 | Qt Concurrent、`QFutureWatcher` |
| 点云显示 | `QOpenGLWidget` |
| OpenGL | Desktop OpenGL 3.3 Core Profile |
| GPU | VBO、VAO、Shader、Picking FBO；不承诺强制绑定某一块 GPU |
| 点元数据 | `float x,y,z,nx,ny,nz` |
| 单位 | 默认按毫米处理；PLY 和机器人位姿必须使用一致单位 |
| 路径要求 | Windows 源码、构建和 Qt 路径均使用 ASCII 字符 |
| Python 工具 | `py -3.13 standard_transform_gui.py` |

启动前必须设置：

```cpp
qputenv("QT_OPENGL", QByteArrayLiteral("desktop"));
QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
```

Windows 仍导出：

```cpp
NvOptimusEnablement = 0x00000001;
AmdPowerXpressRequestHighPerformance = 1;
```

OpenGL 初始化后记录 `GL_VENDOR`、`GL_RENDERER`、`GL_VERSION`；检测到 `llvmpipe`、`softpipe`、`software` 或 `gdi generic` 时只发出警告，不伪造独显已启用。

---

## 3. 工程结构与文件职责

```text
D:/workpiece/pointcloudview/
├─ pointcloudview/
│  ├─ CMakeLists.txt                 构建目标、Qt 模块、诊断和障碍检测测试
│  ├─ CMakePresets.json              ASCII 路径的 MSVC 配置预设
│  ├─ main.cpp                       程序入口、OpenGL 选择、启动日志
│  ├─ mainwindow.h                   主窗口声明、状态和异步任务成员
│  ├─ mainwindow.cpp                 主窗口、画布类、交互和任务编排
│  ├─ mainwindow.ui                  Qt Designer 主界面定义
│  ├─ pointcloudprocessor.h          点云数据结构和公共算法接口
│  ├─ pointcloudprocessor.cpp        PLY、缓存、滤波、几何和配准算法
│  ├─ registration_diagnostic_runner.cpp 机器人配准诊断工具
│  ├─ tests/pointcloudprocessor_obstacle_tests.cpp 障碍检测算法测试
│  ├─ build_windows.ps1              Windows 配置、编译和 windeployqt
│  ├─ clean_reconfigure.ps1          清理并重新配置
│  ├─ QT_CREATOR_BUILD.md             Qt Creator 构建说明
│  └─ REGISTRATION_DISTORTION_DEVELOPMENT.md 配准畸变诊断记录
└─ hand_eye/
   └─ MV-DP2240-01P_Hand_Eye_Convert/
      ├─ standard_transform_gui.py    外部标定转换 GUI
      └─ EyeInHand-*.xml              Eye-in-Hand 标定示例
```

`mainwindow.cpp` 中 `PointCloudCanvas` 是文件内私有 `QOpenGLWidget` 子类，不单独拆文件。`buildUi()` 使用 `ui_mainwindow.h` 生成的 Designer 类；`buildUiLegacy()` 位于 `#if 0`，只作历史参考，不参与运行。

---

## 4. 数据模型与索引契约

### 4.1 点结构

```cpp
struct Point3D {
    float x, y, z;       // 点坐标
    float nx, ny, nz;    // 可选法向量；缺失时为 0
};
```

所有处理 API 接收 `QVector<Point3D>`。函数不得假定点一定有有效法向量；需要法向时应重新估计或使用默认方向。

### 4.2 主窗口缓存

| 成员 | 含义 |
|---|---|
| `m_rawPoints` | 当前加载/合并后的恢复缓存；用于恢复原始显示和导出基础 |
| `m_points` | 当前画布实际显示缓存，所有后续平面、边缘和图像处理以此为输入 |
| `m_pointCloudIds` | 合并点对应的输入 PLY 序号 |
| `m_pointSourceIndices` | 当前点对应源 PLY 的顶点索引 |
| `m_pointSourceFiles` | 合并结果中的源文件列表 |
| `m_canvasRevision` | 每次发布新画布缓存递增，用于丢弃过期后台结果 |

单文件加载时源索引为 `0..N-1`。世界合并时，体素代表点保留实际源点索引，不生成质心虚拟点。

### 4.3 缓存发布规则

`publishCanvasCache(QVector<Point3D>)` 必须在 GUI 线程调用，执行：

1. 替换 `m_points`。
2. 递增 `m_canvasRevision`。
3. 清除已选三点、平面、边缘和二维图像结果。
4. 关闭画布选择模式。
5. 调用 `PointCloudCanvas::setCloud()` 上传标记为待更新。

异步任务保存启动时的 revision；完成时若 revision 已变化，结果必须丢弃。

---

## 5. PLY 读取与二进制缓存

### 5.1 支持范围

`loadPly()` 读取 PLY header，寻找 `element vertex` 及其属性，识别 `x/y/z` 和可选 `nx/ny/nz`。ASCII、binary little-endian、binary big-endian 记录均按属性顺序解析；face 等非 vertex 元素不会进入点数组。无法解析的点、截断文件或缺失坐标会报错并清空部分结果。

### 5.2 解析要求

- ASCII 行使用数值解析，不依赖固定列数。
- 二进制按 PLY 声明的 scalar 类型读取：char/int8、uchar/uint8、short/int16、ushort/uint16、int/int32、uint/uint32、float/float32、double/float64。
- PLY 中不存在法向属性时法向量保持 0。
- 坐标必须有限，零点是否作为可用点由 `usablePoint()` 判断。

### 5.3 `.pcvbin` 缓存

旁车缓存名称为：

```text
<source>.ply.pcvbin
```

当前格式字段：

```text
magic       quint32 = 0x31564350  // PCV1
version     quint32 = 1
count       quint64
sourceSize  quint64
sourceStamp qint64   // lastModified().toMSecsSinceEpoch()
payload     count * sizeof(Point3D) 原始连续二进制
```

缓存只有在源文件大小和修改时间同时匹配时才使用；失败则重新解析 PLY 并尝试重写缓存。该缓存不改变 PLY 协议。

### 5.4 异步加载

单文件和数据源列表当前行加载均通过：

```cpp
QtConcurrent::run(pointcloud::loadPlyCachedResult, path)
```

完成信号在主线程更新画布，加载期间显示无范围进度条，避免阻塞 UI。

---

## 6. 数据源和文件夹工作流

点击“打开点云”后弹出两个选项：

1. **打开单个 PLY**：选择文件，建立单文件数据源并异步加载。
2. **扫描文件夹 PLY**：扫描所选目录的 `*.ply` 和 `*.PLY`，按文件名排序，把全部文件放入左侧 `lw_files` 并保持扩展多选；扫描完成后明确异步加载第一个文件到中央画布，但不自动合并。列表填充期间阻断 `currentRowChanged`，随后直接调用加载流程，避免 `selectAll()` 改变当前项后信号不再触发而导致画布空白。

左侧列表使用 `ExtendedSelection`，支持 Ctrl/Shift 多选。当前行变化只加载该文件预览；“确认点云配准”读取所有选中行并生成姿态输入表。

---

## 7. OpenGL 画布实现

### 7.1 资源

`PointCloudCanvas` 创建：

- 点云位置/法向 VBO：`QOpenGLBuffer::StaticDraw`。
- 点状态 VBO：`QOpenGLBuffer::DynamicDraw`，0 普通、1 平面、2 边缘、3 障碍。
- 点云 VAO。
- 轮廓线 VBO/VAO。
- Picking FBO：RGBA8 颜色纹理 + depth attachment。
- 主渲染、Picking、轮廓三组 shader。

资源创建和销毁必须在有效 OpenGL context 中进行；析构期间检查 `QCoreApplication::closingDown()`、context 有效性和 `makeCurrent()` 成功状态。

数据源加载与障碍结果清理必须保持单向关系：PLY 加载完成后由主线程调用
`publishCanvasCache()` 上传完整点云，随后才清空旧平面、边缘和障碍状态；
障碍检测层只能更新点状态 VBO，不能清空位置 VBO、替换画布缓存或决定文件
是否加载。文件夹扫描必须显式启动首个文件加载，不能仅依赖列表当前行信号。
`PointCloudCanvas::setCloud()` 接管独立的显示缓存；如果调用来自非画布线程，
必须通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 排队回画布所属
GUI 线程，禁止直接返回并静默丢弃已加载点云。发布点数写入启动日志，便于
区分 PLY 解析问题与画布缓存发布问题。

### 7.2 视图和导航

- 左键拖动（普通模式）：绕 X/Z 旋转，pitch 限制 `[-89°,89°]`。
- 右键拖动：平移。
- 滚轮：缩放，范围 `0.08..30`。
- 初始 yaw `38°`、pitch `-28°`、zoom `1.0`。
- 点云包围盒中心用于归一化，最大 XYZ 范围作为 `cloudSpan`。
- 正交投影根据画布宽高比生成，近远裁剪 `-4..4`。

### 7.3 主点渲染

OpenGL 3.3 Core 使用 `GL_POINTS`，开启 `GL_PROGRAM_POINT_SIZE`。默认颜色是高度渐变蓝→绿→橙；可切换灰阶或法向量颜色。平面点灰阶，边缘点亮灰/白色；选中点单独放大绘制。透明度由 `overlay` 控制。

不使用 QPainter 绘制点；QPainter 仅用于画布文字、坐标轴和边缘框选矩形。

### 7.4 GPU Picking

Picking 与正常绘制使用相同的包围盒、旋转、缩放、平移、viewport、点大小和 device pixel ratio：

1. FBO 尺寸为 `round(width * devicePixelRatioF)` × `round(height * devicePixelRatioF)`。
2. 关闭混合和抖动，开启深度测试。
3. Picking vertex shader 以 `gl_VertexID + 1` 作为点 ID。
4. Fragment shader 用 RGBA8 四通道编码 32 位 ID，0 表示空白。
5. 鼠标坐标转换为左下角 OpenGL 像素坐标。
6. 读取中心像素；中心为空时读取 3×3 区域，优先像素距离，再以深度决定重叠点。
7. 空白区域返回 `-1`，不进行 CPU 最近点反投影。
8. 返回 `displayIndex = id - 1`，直接对应 `m_points` 当前画布缓存。

边缘框选也使用同一个 Picking FBO，读取矩形内所有有效 ID 并去重排序。

---

## 8. 噪点和降采样

### 8.1 体素降采样

`voxelFilter()` 将点按 `floor(x/cell), floor(y/cell), floor(z/cell)` 哈希分格，每格只保留遇到的第一个真实点。它不是质心法，不生成虚拟点；因此下游可继续追溯源点。

### 8.2 统计离群值

默认：

```text
meanK = 45
stddevMultiplier = 1.30
```

流程：

1. 先按设置执行体素预处理。
2. 自动估计统计网格尺寸，至少不小于用户体素尺寸。
3. 空间哈希建立点到网格单元的索引。
4. 按预计算的 0..8 壳层访问邻域，收集距离，取 K 个最近邻平均距离。
5. 计算全局均值 `mean` 和样本标准差 `stddev`。
6. 阈值 `mean + stddevMultiplier * stddev`。
7. 保留平均邻距不超过阈值的真实点。

邻居不足、点数太少或最终为空时保留当前点云并返回中文警告，禁止把全体点误删。离群处理后台运行，输入为当前 `m_points`，不是重新读取原始 PLY。

---

## 9. Eye-in-Hand 世界坐标转换

### 9.1 输入

每个选中 PLY 在姿态表中有 12 个数：

```text
Start X Y Z RX RY RZ
END   X Y Z RX RY RZ
```

RX/RY/RZ 是用户界面保留的字段名，当前 Start/End 位姿按 ZYX 组合，角度单位为度：

```text
R = Rz(RZ) · Ry(RY) · Rx(RX)
T_base_flange = [ R  translation ]
```

### 9.2 标定 XML

XML 必须包含 `DepthInRobotPose`。该节点的 tx/ty/tz 与 rx/ry/rz 当前按标定工具约定作为固定 XYZ 角（XML 示例为弧度），代码实现等价于：

```text
T_flange_depth = T(DepthInRobotPose)
R = Rz(rz) · Ry(ry) · Rx(rx)
```

若 XML 包含 `RTmatDepth2robot/RotMat`，程序将其与欧拉角旋转矩阵比较；最大差异大于 `0.01` 时拒绝标定文件，防止错误转换。

### 9.3 每点变换

扫描进度 `t` 的来源可选：

- PLY 顶点顺序：`index/(N-1)`。
- 相机局部 X/Y/Z：该轴值在当前 PLY 最小值和最大值之间归一化。

平移线性插值，旋转四元数 SLERP。当前为性能预计算 2048 个姿态查找表：

```text
T_base_flange(t) = interpolate(Start, End, t)
P_world = T_base_flange(t) · T_flange_depth · P_depth
```

法向量使用 `mapVector()` 旋转，不应用平移。

### 9.4 重要限制

程序没有 PLY 点时间戳、扫描线号或机器人轨迹采样数据。上述进度是近似模型；如果点序/局部轴不是采集时序，或机器人非匀速，单片点云会产生非刚体拉伸、剪切或弯曲。该问题不能由刚体 ICP 完全修复。

---

## 10. 点云配准与缓存

### 10.1 流程

```text
左侧多选 PLY
→ 输入每个文件 Start/End 位姿
→ 读取 Eye-in-Hand XML
→ 每点转换到机器人世界坐标
→ 可选世界坐标体素真实点降采样
→ 第一个点云作为参考
→ 后续点云顺序累计 ICP
→ 合并并发布画布缓存
```

### 10.2 ICP

默认参数：

```text
enabled = true
maximumIterations = 30
maximumCorrespondenceDistance = 2.0 mm
convergenceTolerance = 0.001 mm
maximumSamples = 120000
minimumCorrespondences = 24
maximumCorrectionTranslation = 100 mm
maximumCorrectionAngleDegrees = 45°
```

实现方法：

1. 对参考合并点建立以最大对应距离为单元尺寸的三维空间哈希。
2. 对移动点按步长采样，查相邻 27 个单元中的最近参考点。
3. 计算两组对应点质心和 3×3 协方差。
4. 用 4×4 Horn 四元数矩阵的幂迭代求刚体旋转。
5. 平移为 `cq - R*cp`，将修正施加到全部移动点和法向。
6. 对应数不足、修正平移/旋转过大时拒绝本轮。
7. RMS 变化小于收敛阈值时停止。

这是顺序累计、点到点 ICP，不是全局位姿图优化，也没有点到平面法向 ICP。

### 10.3 合并缓存

ICP 关闭时使用目录下：

```text
.pointcloudview-merge.pcvbin
```

缓存版本当前为 2，校验每个输入的路径、文件大小、修改时间、Start/End 矩阵、手眼矩阵、扫描进度来源、体素开关和体素尺寸。ICP 开启时不读取该缓存。

---

## 11. 三点 2.5D 平面提取

### 11.1 UI 操作

平面页只保留以下操作：

- 取点
- 放弃取点
- 撤销选择的点
- 确定平面
- 确定候选平面
- 取消确定平面

Esc 等价于放弃取点，Backspace 等价于撤销上一个点。三点均从当前画布 GPU Picking 返回，保存为 `m_selectedPointIndices`。

### 11.2 拟合流程

默认参数：

```text
initialTolerance = 1.0 mm
surfaceTolerance = 0.4 mm
RANSAC iterations = 300
minInliers = min(100, max(3, pointCount/4))
useZAxisResidual = true
maxNormalTiltDegrees = 45°
PCA refinement iterations = 2
```

1. 三点计算初始法向 `cross(P2-P1, P3-P1)`。
2. 法向长度阈值按点云尺寸判断，过近或共线拒绝。
3. 候选点取初始平面容差内点。
4. RANSAC 从候选点随机取三点估计模型。
5. PCA/最小特征值特征向量修正平面法向。
6. 强制法向指向正 Z，适配 2.5D 高度平面。
7. 用最终距离阈值分类。
8. 将点投影到平面局部 UV，按估计点距构建半径连通域。
9. 当 `keepSeedComponentOnly=true` 时，仅保留同时包含 P1/P2/P3 的连通组件。
10. 输出平面方程 `a*x+b*y+c*z+d=0`、候选点、最终点、RMS、平面性和控制点。

“确定平面”先执行快速候选拟合并延迟全量分类；“确定候选平面”再次执行完整分类后才允许边缘处理。

---

## 12. 平面边缘分割和 2D 图像

### 12.1 栅格投影

将已确定平面的点投影到由平面法向构造的局部 `axisU/axisV` 坐标系，按用户栅格尺寸建立 2D Mask。默认自动估计点间距，最大栅格数 `4,000,000`，过大时自动增大栅格尺寸。

### 12.2 Mask 和边缘

1. 平面点占用栅格置 1。
2. 按开闭运算半径进行膨胀/腐蚀组合。
3. 8 邻域检查缺失邻居，生成外边界 mask。
4. 外边界对应真实 `edgeIndices`，画布中显示为黄色。
5. Marching Squares 提取连续轮廓。
6. 根据轮廓嵌套深度标记孔洞。
7. 轮廓转换回三维平面坐标，用 OpenGL `GL_LINE_STRIP` 绘制。

默认边缘参数：

```text
edgeGridSize = 0（自动）
morphologyCloseRadius = 1
morphologyOpenRadius = 1
maximumEdgeGridCells = 4,000,000
```

### 12.3 二维图片

“提取平面 2D 图像”只投影当前已确定平面，不改变边缘状态；“执行边缘分割”同时生成边缘、轮廓和带边缘颜色的图像。图片的非工件区域必须使用纯黑色 `RGB(0,0,0)`（像素值为 0），以便 OpenCV 后续直接按黑色背景建立 mask；平面占用格为灰色，边界格为黄色。支持 PNG/BMP 保存。任何图像缩放或保存操作均不得把黑色背景改成深灰或非零颜色。

### 12.4 黄色边缘框选

边缘选择模式中：

- 左键单击选择 Picking 返回的黄色真实边缘点。
- 左键拖动框选矩形，读取矩形��� GPU ID 并只保留 `edgeIndices`。
- 右键仍用于平移。
- Esc 由画布快捷键取消三点选择；边缘选择取消主要通过“清除边缘选择”。

### 12.5 第一阶段障碍物检测

障碍物检测必须在用户确定候选平面后执行，输入为当前画布缓存、已确定平面索引和归一化平面模型。算法将法向统一指向正 Z，计算点到基准平面的有符号高度，仅保留高度达到阈值且 UV 投影位于平面测量范围（允许一个栅格边距）内的点。

候选点投影到平面 UV 栅格，使用 8 邻域组成连续区域，再按最小点数和占用栅格物理面积过滤。默认参数：

```text
minimumHeight = 1.0 mm
gridSize = 0（按平面点密度自动估计）
minimumPointCount = 30
minimumArea = 4.0 mm²
maximumGridCells = 4,000,000
```

`detectObstacles()` 输出候选点数、有效障碍点索引、栅格尺寸和每个区域的点数、面积、中心、平均高度、最大高度及三维包围盒。有效障碍点在画布中以红色显示，优先级高于灰色平面和黄色边缘；检测到区域时状态栏、障碍检测页和警告对话框同时提醒用户移除障碍物并重新扫描。平面或画布缓存变化时必须清除旧检测结果。

---

## 13. 几何处理公共 API

这些接口已在 `pointcloudprocessor.h` 中声明，当前主要由测试或后续业务调用，主 UI 不全部暴露。

### 13.1 局部 PCA 特征

`extractGeometryFeatures()`：空间哈希邻域搜索，计算协方差矩阵和 Jacobi 3×3 特征分解。最小特征值方向为法向，输出曲率、线性度、平面度、散乱度、特征值和邻居数。

`extractFeatures()`：可按比例抽样后包装 PCA 结果，输出 curvature、linearity、planarity、sphericity/scattering、roughness。

### 13.2 几何补全

`completeGeometry()`：PCA 建立支撑平面，在局部二维极坐标中以半径中位数识别圆周；超过角度缺口阈值的区域插入圆弧点。生成点标记为参数化点，不应与实测点混淆。

### 13.3 平面和倒角

`segmentPlanes()`：多轮 RANSAC 平面提取，完成模型回投全点统计，输出每点 label、平面方程、内点数、平均/最大距离。

`completeChamfers()`：先分割支撑平面，再按平面交线、法向夹角、两侧边界支撑拟合线性倒角或圆角倒角；输出候选、置信度、不确定度和参数化补点。

### 13.4 圆相关

- `detectCircleOnPlane()`：投影到平面，RANSAC 三点圆模型、角度覆盖约束、代数最小二乘细化。
- `cleanCircleInterior()`：按目标平面、半径和保护带清理圆孔内部，支持表面层或投影清理。
- `findSimilarCirclesOnPlane()`：二维栅格边界提取、半径/圆心候选投票、圆拟合和相似度排序。

结果包含半径、圆心、RMS、角度覆盖、置信度和不确定度。

---

## 14. Qt Designer UI 规格

### 14.1 主窗口

- `main_window`：1400×860，最小 1080×680。
- `splitter_main`：左数据源、中画布、右控制区，初始尺寸约 245/850/290。
- `wgt_canvas_host`：代码创建 `PointCloudCanvas` 嵌入。
- `lw_files`：左侧 PLY 列表，ExtendedSelection。
- `tw_main`：右侧标签页。

### 14.2 标签页和对象

| 标签页 | 关键对象 | 功能 |
|---|---|---|
| 点云配准 | `le_hand_eye_xml`, `btn_browse_hand_eye`, `cb_scan_progress`, `tbl_registration`, `chk_registration_voxel`, `chk_registration_icp`, `spb_icp_iterations`, `dsb_icp_distance`, `dsb_icp_tolerance` | 输入 XML、扫描进度、姿态和 ICP 参数 |
| 显示 | `spb_point_size`, `btn_reset_view`, `cb_color_mode`, `dsb_overlay`, `dsb_map_min`, `dsb_map_max` | 点大小、视角、颜色、透明度、高度映射 |
| 点云清理 | `chk_voxel_noise`, `dsb_voxel_size`, `chk_statistical_noise`, `spb_mean_k`, `dsb_stddev`, `btn_apply_noise`, `btn_restore_cloud` | 当前画布缓存滤波和恢复 |
| 2.5D 平面提取 | `btn_pick_points`, `btn_abandon_points`, `btn_undo_point`, `btn_determine_plane`, `btn_confirm_candidate`, `btn_cancel_candidate`, `pte_plane_output` | 三点采样、候选拟合、确认/取消 |
| 障碍检测 | `dsb_obstacle_height`, `dsb_obstacle_grid`, `spb_obstacle_min_points`, `dsb_obstacle_min_area`, `btn_detect_obstacles`, `btn_clear_obstacles`, `lbl_obstacle_status`, `pte_obstacle_output` | 平面上方凸起、连续区域过滤、红色高亮和用户提醒 |
| 平面边缘分割 | `dsb_edge_grid`, `spb_edge_close`, `spb_edge_open`, `btn_extract_plane_image`, `btn_apply_edge`, `btn_select_edge`, `btn_clear_edge`, `btn_save_plane_image`, `lbl_plane_image_preview`, `pte_edge_output` | Mask、轮廓、黄色边缘、2D 图像 |

菜单：`act_open` 打开点云，`act_exit` 退出。底部 `pbar_progress` 为后台任务指示，`statusbar_main` 显示状态。

### 14.3 注意事项

UI 文件中存在重复的通用名称（例如多个 `lbl_subtitle`、控制区容器名称），复现时应以对象所在页面和生成的 `ui_mainwindow.h` 为准。代码中 `#if 0` 的旧 UI 不应重新启用。

---

## 15. 异步任务和生命周期

任务类型：

| Watcher | 后台函数 | 完成槽 |
|---|---|---|
| `m_loadWatcher` | `loadPlyCachedResult` | `loadFinished` |
| `m_worldMergeWatcher` | `mergePlyCloudsInWorld` | `worldMergeFinished` |
| `m_noiseWatcher` | `removeNoise` | `noiseFinished` |
| `m_threePlaneWatcher` | `extractPlaneFromThreePoints` | `planeExtractionFinished` |
| `m_obstacleWatcher` | `detectObstacles` | `obstacleDetectionFinished` |
| `m_edgeWatcher` | `segmentPlaneEdges` | `planeEdgeSegmentationFinished` |
| `m_planeImageWatcher` | `extractPlaneImage` | `planeImageExtractionFinished` |

所有后台 lambda 捕获输入数据副本，不访问 UI。析构时等待仍在运行的 watcher，断开信号；`closeEvent()` 设置 `m_closing` 并等待任务完成，避免后台结果触碰已销毁画布。

---

## 16. 外部手眼脚本

脚本：

```text
D:/workpiece/pointcloudview/hand_eye/MV-DP2240-01P_Hand_Eye_Convert/standard_transform_gui.py
```

程序按钮使用：

```text
py -3.13 standard_transform_gui.py
```

脚本读取 XML 的 `DepthInRobotPose`，输入机器人当前基座位姿和深度图点位姿，构造：

```text
matrix1 = robot base-to-flange pose (fixed ZYX, degrees)
matrix2 = DepthInRobotPose (fixed XYZ, radians)
matrix3 = depth point pose (fixed XYZ, degrees)
matrix4 = matrix1 · matrix2 · matrix3
```

脚本主要用于人工验证和单点转换；主程序批量处理直接在 C++ 中执行，避免逐点启动 Python 进程。

---

## 17. 验证和验收

### 17.1 当前验证状态

旧版综合测试已经移除；当前保留独立的
`tests/pointcloudprocessor_obstacle_tests.cpp`，覆盖连续凸起识别、孤立高点过滤、
高度阈值和基准平面范围过滤。机器人配准仍以 MSVC 编译、
`registration_diagnostic_runner` 真实数据诊断和 GUI 手动验收为准。

### 17.2 构建命令

在 Visual Studio Developer PowerShell 中：

```powershell
cd D:/workpiece/pointcloudview/pointcloudview
./build_windows.ps1 `
  -QtDir C:/Qt/6.8.3/msvc2022_64 `
  -BuildDir C:/qt-build-pointcloudview `
  -Config Release
```

如需构建机器人配准诊断工具，可启用可选目标：

```powershell
cmake -S . -B C:/qt-build-pointcloudview-diagnostic `
  -G "NMake Makefiles" `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Debug `
  -DPOINTCLOUDVIEW_BUILD_TESTS=ON
cmake --build C:/qt-build-pointcloudview-diagnostic
```

该选项生成 `registration_diagnostic_runner` 和
`pointcloudprocessor_obstacle_tests`，并注册障碍检测 CTest 用例。

构建自动运行 `windeployqt`，运行目录应包含 Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets DLL 和 `platforms/qwindows*.dll`。

### 17.3 手动验收

1. 单个 ASCII、binary PLY 打开。
2. 文件夹扫描后左侧显示全部 PLY、支持多选，并自动在中央画布显示第一个文件；切换当前行时显示对应 PLY。
3. 旋转、平移、缩放后 GPU 点选不发生明显偏移。
4. 空白区域不误选，重叠点取深度最前点。
5. 三点共线或过近时提示并允许重选。
6. 平面点灰阶、三点放大、法向朝正 Z。
7. 确认平面后边缘黄色、轮廓连续、孔洞可识别。
8. 2D 图像预览和 PNG/BMP 保存。
9. 噪点处理不删除全部点，后台运行期间窗口可操作但相关按钮禁用。
10. Start/End 位姿、XML、进度来源改变后不读取旧合并缓存。
11. ICP 开关对照检查：先关闭 ICP 检查世界坐标转换，再开启 ICP 检查片间残差。
12. OpenGL 启动日志中可看到 vendor/renderer/version。
13. 确定平面后执行障碍检测，连续凸起显示为红色并弹出提醒；降低凸起高度或移到平面范围外后不应误报。

---

## 18. 已知限制、风险和后续需求

### 18.1 配准畸变风险

当前每点姿态来自 Start/End 插值和四种进度估计之一。没有真实点时间戳或机器人轨迹时，扫描运动非匀速、点序重排、轴选错都会造成非刚体畸变。必须增加以下任一数据才能实现精确补偿：

- 每点/每扫描线时间戳和同步机器人位姿。
- 激光触发索引与机器人轨迹采样表。
- 扫描仪厂商确认的点序时序和匀速运动证明。

### 18.1.1 机器人位姿未正确拼接时的强制诊断要求

机器人位姿是最终世界坐标基准，ICP 只能在其基础上做受限局部修正，不能用 ICP 掩盖错误的手眼矩阵、单位或扫描时序。当前转换模型为：

```text
P_world(i) = T_base_flange(t_i) · T_flange_depth · P_depth(i)
```

其中 `T_base_flange(t_i)` 由 Start/End 位姿插值得到，平移线性插值、旋转四元数 SLERP；`T_flange_depth` 读取 XML 的 `DepthInRobotPose`。实现和现场标定必须确认该矩阵确实表示“深度相机坐标到法兰坐标”；若 XML 表示反向矩阵，必须先求逆。机器人角度当前按 `Rz(RZ)·Ry(RY)·Rx(RX)`、单位度处理，手眼 XML 旋转按 XML 声明的弧度处理。

出现片间无法拼接或工件弯曲时，界面/日志必须支持以下逐项对照，不得直接盲目调 ICP：

1. 关闭 ICP，仅检查机器人位姿和手眼转换后的世界坐标。
2. 将 `Start=END`，验证逐点扫描姿态插值是否是畸变来源。
3. 依次比较 PLY 顶点顺序、局部 X/Y/Z 作为扫描进度的结果。
4. 输出 Start/中点/End 位姿矩阵、手眼矩阵、变换前后包围盒和单位警告。
5. 用 `(0,0,0)`、三个单位轴点与 Python 3.13 转换脚本逐点核对轴向、平移和矩阵方向。
6. 确认 PLY、机器人 XYZ、手眼平移、ICP 距离使用一致的毫米/米单位。

若固定姿态或关闭 ICP 后结果改善，应优先修正扫描进度、手眼方向或单位；只有机器人变换已通过上述对照后，才允许启用受限 ICP、多尺度 ICP 或位姿图优化。没有点时间戳、扫描行号或编码器同步数据时，逐点姿态只能是近似模型，可能产生非刚体拉伸、剪切和弯曲，刚体 ICP 无法完全恢复真实几何。

对于当前 `Point_Cloud_A` 示例，PLY 局部 Y 从约 `0` 逐行下降到 `-300`，与扫描行顺序一致，而机器人给出的 Start→End 是扫描运动方向。因此选择“相机局部 Y”时，程序会根据首个/末个有效轴值自动反转进度，使局部 Y 的下降方向对应 Start→End；不再错误地把最小轴值直接当作进度 0。合并缓存版本同步升级，旧方向生成的 `.pointcloudview-merge.pcvbin` 不会继续复用。

本版本继续补充机器人变换诊断输出：每个输入 PLY 记录 Start/Mid/End 插值位姿、手眼矩阵、扫描进度来源和范围、转换前后包围盒，以及是否使用逐点姿态。该诊断只读输出，不改变世界坐标算法；用于在启用 ICP 前确认矩阵方向、单位和扫描进度假设。命中 `.pcvbin` 合并缓存时也必须输出缓存命中提示、每个文件的世界点数和世界包围盒，不能因缓存而跳过诊断。

合并开始前会验证 Start、End 和（启用机器人转换时）手眼矩阵：所有元素必须为有限值，齐次底行必须为 `[0 0 0 1]`，旋转列必须近似正交单位矩阵且行列式接近 `+1`。无效矩阵立即拒绝合并并提示具体文件和矩阵类型，避免 NaN、镜像变换或退化旋转污染世界坐标。

### 18.2 ICP 风险

当前 ICP 是局部点到点、顺序累计算法。低重叠、重复结构、大片平面或初始位姿错误时可能收敛到错误局部最优。后续建议加入重叠率、双向对应、法向一致性、鲁棒核和全局位姿图优化。

### 18.3 Point_Cloud_A 实测映射与简化验证流程

验证目录 `D:\workpiece\ply\MV-DP2240-01P(DB1723975) (1)\Point_Cloud_A` 包含
`Point_Cloud_A01.ply` 至 `Point_Cloud_A04.ply`，每个约 630 万个实测点。
局部范围约为 X=-94..96、Y=0..-300、Z=-82..18；PLY 按扫描行组织，局部
Y 沿扫描方向递减。

```text
A01 Start X500 Y150 Z700 RX0 RY0 RZ180 ; END X500 Y-150 Z700 RX0 RY0 RZ180
A02 Start X600 Y150 Z700 RX0 RY0 RZ180 ; END X600 Y-150 Z700 RX0 RY0 RZ180
A03 Start X700 Y150 Z700 RX0 RY0 RZ180 ; END X700 Y-150 Z700 RX0 RY0 RZ180
A04 Start X800 Y150 Z700 RX0 RY0 RZ180 ; END X800 Y-150 Z700 RX0 RY0 RZ180
```

该数据必须选择“相机局部 Y”作为扫描进度。程序根据首末有效轴值自动
判断方向；末值较小时反转归一化进度，使 PLY 首行对应 Start、末行对应
End。不得使用“PLY 顶点顺序”替代扫描进度，因为同一扫描行内的点不是
机器人运动采样点，会造成单帧剪切、拉伸或弯曲。旧方向生成的合并缓存由
版本号失效。

简化操作顺序：选择文件（或文件夹后多选）→ 自动识别并确认 A01-A04 的
Start/End → 以 Local Y 执行机器人世界坐标转换（默认关闭 ICP）→ 检查世界
坐标诊断与重合区域 → 确认无误后再启用受限 ICP。机器人位姿始终是最终
世界坐标基准，ICP 只能做小范围修正。

### 18.3 性能风险

点云主体 VBO 全量上传，CPU 算法仍会复制后台输入 `QVector`；千万级点云可能受内存和 OpenGL 单缓冲区限制。当前没有分块渲染、GPU Compute、增量索引或磁盘映射。

### 18.4 索引风险

单文件噪点处理返回 `QVector<Point3D>`，主窗口需要同步维护源索引；若未来增加复杂滤波或合并后再处理，必须同时更新 `m_pointSourceIndices`，禁止仅替换点数组。

### 18.5 UI/接口风险

Designer 是运行时 UI 唯一来源；任何在代码中重新创建同名控件或重新启用 `buildUiLegacy()` 都可能造成连接重复和空指针。新增控件必须同步 `.ui`、生成头文件和 `buildUi()` 绑定。

### 18.6 障碍物识别限制

第一阶段只依据已确定基准平面的正向高度、二维连通性、点数和面积进行规则检测。没有 CAD、标准件模板或历史无障碍扫描时，算法无法自动区分工件设计中的正常凸台与临时障碍物；检测结果用于提醒和人工复核，不允许简单删除障碍点后假定被遮挡表面完整。

---

## 19. 复现最低实现顺序

如需从零复现当前项目，按以下顺序实现：

1. 建立 Qt 6 CMake 工程和 `Point3D` 数据结构。
2. 实现 PLY ASCII/二进制解析及 `.pcvbin` 校验缓存。
3. 用 Qt Designer 建立主窗口六个标签页和对象名。
4. 实现 `PointCloudCanvas`、VBO/VAO、OpenGL 3.3 shader 和视角控制。
5. 实现 Picking FBO、RGBA32 ID、深度和 3×3 读取。
6. 实现当前画布缓存发布、revision 和异步 watcher 生命周期。
7. 实现体素/统计离群值。
8. 实现三点初始平面、RANSAC/PCA、Z 轴约束和连通域。
9. 实现平面栅格、形态学、8 邻域边缘、Marching Squares 和图片。
10. 实现平面高度差、UV 栅格连通区域过滤和障碍点红色高亮。
11. 实现 XML 手眼矩阵读取、Start/End 插值、世界转换和缓存。
12. 实现空间哈希 ICP 和质量阈值。
13. 通过算法测试、真实 PLY、机器人位姿和诊断工具完成构建部署验收。

完成以上步骤后，再实现几何特征、圆、相似圆和倒角等公共 API，不改变主窗口已有缓存和索引契约。

---

## 20. 当前 Git 基线

本档案对应的本地分支提交链最近记录：

```text
937c826 Fix registration progress status update
56b2751 Reject inconsistent hand eye calibration matrices
e83fd52 Harden registration cache and speed up motion transform
ef55dc6 Document registration distortion diagnosis
30604f5 Use scan progress for Eye-in-Hand point transforms
2e85d98 Apply Eye-in-Hand pose along each scan
9985508 Integrate Eye-in-Hand calibration transform
46fd619 Add ICP refinement to point cloud registration
```

任何复现或后续开发应从该 Git 状态开始，并保持 Qt、MSVC、ASCII 路径和无 CUDA 约束不变。

真实数据测试表明，不能仅依据“局部 Y 递减”决定是否反转进度。给定手眼旋转与机器人姿态组合后，局部 Y 与机器人世界 Y 已经方向相反；额外反转会把相机 300 mm 扫描跨度和机器人 300 mm 运动相加，造成约 602 mm 的世界 Y 拉伸。将每帧 Start/End 交换后，世界 Y 跨度约 3 mm，证明主要故障是位姿与扫描行的方向配对，而不是 ICP 或图像分辨率。后续实现必须分别计算正向和反向两种端点变换残差，选择残差较小者，并将选择结果写入诊断和缓存键；不能只看局部轴值升降。

### 18.4 第一阶段实现：显式扫描方向与缓存隔离

`WorldCloudInput` 新增 `ScanDirection::{Auto,Forward,Reverse}`。生产界面使用 `Auto`，分别评估正向和反向映射的变换端点残差，并把最终选择写入诊断；测试和现场排查可强制指定 Forward 或 Reverse。合并缓存版本已升级且序列化方向字段，方向不同的结果不能复用。

### 18.5 第二阶段：首尾区域方向评分与重合验证

方向判定改为统计每帧首尾约 1% 的有效点（限制最大采样数），求首尾区域
平均位置，再用中点手眼/法兰旋转得到相机扫描位移。分别计算“相机位移 +
机器人 Start→End 平移”和“相机位移 - 机器人平移”的长度，选择较小者；两
个分数和选择结果写入诊断。该方法用于判断 Eye-in-Hand 扫描中相机运动与机
器人运动的抵消关系，不依赖单个端点或局部轴数值升降。

转换后对相邻 PLY 计算 XY 包围盒重合率，重合率仅用于验证和报警，不删除重
合实测点，也不生成虚拟点。

### 18.6 第三阶段：相邻帧 XY 重合检查

每帧完成机器人世界坐标转换和体素代表点处理后，计算当前帧与已累计参考
的 XY 包围盒交集。输出交集面积、当前帧面积、交并比和重合率；低于阈值时
在配准诊断中报警，提示检查文件顺序、Start/End、扫描方向和手眼矩阵。该
阶段只做几何检查，不删除重合实测点，也不让 ICP 掩盖无重合问题。

### 18.7 第四阶段：ICP 前置质量门控

机器人世界坐标转换完成后，ICP 开始前计算当前帧与累计参考的 XY 包围盒
重合率。默认低于 10% 时跳过该帧 ICP，并在诊断中明确提示检查 Start/End、
扫描方向、手眼矩阵和文件顺序；实测点不删除、不移动。该门控可通过
`rejectIcpWhenLowWorldOverlap=false` 关闭，仅用于受控诊断。ICP 实际运行时
仍执行 Fitness、RMSE、对应点重合率、重复对应率及修正量限制。
