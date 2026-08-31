# PointCloudSuite 点云查看与处理程序产品开发文档

> 状态：历史归档。当前项目基线请参阅 `docs/requirements/pointcloudview_v0.3.md`。本文仅保留 v0.1 历史事实，不作为当前实现规范。最后核对日期：2026-08-31。

版本：v0.1（历史版本）
日期：2026-08-24
适用范围：`pointcloudview` 点云查看处理程序及其共享点云模块

## 1. 项目背景

PointCloudSuite 面向机器人/工业视觉点云数据的查看、平面提取和二维图像导出。v0.1 的目标是建立一个可运行、可验证、可扩展的点云处理基线：输入 PLY 点云，经过真实点选择和算法处理后，输出平面结果、边缘结果及可追溯的运行文件。

本版本不是机器人控制器或自动加工系统，不包含机器人运动控制、手眼标定求解、轨迹规划或生产数据库。

## 2. 产品目标与边界

### 2.1 已实现目标

- 读取 ASCII、Binary Little Endian、Binary Big Endian PLY。
- 在 Qt/OpenGL 画布中旋转、平移、缩放和重置视角。
- 通过 GPU Picking 选择真实点，使用三点提取目标平面。
- 使用 RANSAC/PCA、距离分类和连通域过滤获得有效平面。
- 建立工件坐标系，将平面点映射为二维 Mask 并导出 PNG。
- 对 ASCII 读取提供后台、内存映射及自适应 `1/2/4` worker 分块解析路径；紧凑
  binary `float x/y/z` 布局使用内部映射快速路径。
- 通过 `pcv_output` 统一写出 PNG、robot_base 平面 PLY 和 JSON 三件套，并使用
  稳定错误码和 JSON 原子提交。
- 通过 `pcv_registration` 读取 Eye-in-Hand XML 并完成线扫点云到 `robot_base` 的刚体变换。
- 通过 `pcv_interface` 读取临时扫描信息，生成临时工件平面、ROI 和成套交换文件。
- 主界面提供“打开扫描 JSON”入口，后台执行临时工件生成并展示统计；该动作不替换当前画布缓存。

### 2.2 系统边界

| 范围内 | 范围外 |
|---|---|
| PLY 读取、缓存、显示、点云清理、平面/边缘处理 | 机器人通讯与运动执行 |
| 工件坐标计算与坐标变换记录 | 手眼标定求解与标定设备控制 |
| PNG、JSON、机器人基坐标平面 PLY 导出 | CUDA、PCL、VTK、Open3D |
| CMake 构建、单元测试和启动日志 | 多工位协同、MES/数据库追溯 |

## 3. 技术架构与代码说明

- `apps/pointcloudview/`：桌面程序入口、`MainWindow`、Qt Designer UI、OpenGL 画布和业务流程。
- `src/io/` + `include/pcv/io/`：PLY 头解析、ASCII/二进制读取、取消、进度和缓存文件。
- `src/filtering/` + `include/pcv/filtering/`：体素处理和统计离群值去除。
- `src/infrastructure/`：运行时目录（缓存、日志、导出）的平台无关管理。
- `src/output/` + `include/pcv/output/`：平面输出上下文校验、PNG/PLY/JSON 契约。
- `src/registration/` + `include/pcv/registration/`：手眼标定和线扫坐标转换。
- `src/interface/` + `include/pcv/interface/`：临时扫描输入和临时工件输出接口。
- `tests/`：PLY 读取、缓存、滤波、输出、临时工件接口和手眼转换测试。

主程序的 `m_points` 是后续处理唯一输入。新点云发布时必须先替换画布缓存，再清除旧平面、边缘和选点状态。

## 4. 默认用户流程

1. 启动程序并初始化 Qt Desktop OpenGL、运行目录和日志。
2. 打开单个 PLY，或递归扫描文件夹建立数据源列表。
3. 后台读取并校验点云，完成后发布到画布和 VBO。
4. 可选执行体素处理、统计离群值去除。
5. 开启取点，依次 GPU Picking 选择 P1/P2/P3，确认候选平面。
6. RANSAC/PCA 精拟合、距离分类和连通域过滤，显示平面与统计信息。
7. 按机器人基坐标轴投影自动生成右手工件坐标系。
8. 执行边缘分割和二维 Mask 生成，保存 PNG；同时可保存同名 JSON 和机器人基坐标平面 PLY。

异步任务只在工作线程读取和计算，不访问 QWidget 或 OpenGL；结果返回时校验画布版本，过期结果直接丢弃。

## 5. 核心功能与默认参数

### 5.1 点云读取与缓存

按属性名称读取 `x/y/z`，可选 `nx/ny/nz`，不依赖列顺序；缺少必需属性、类型不支持、非有限值或数据截断时拒绝发布。ASCII 优先使用 `QFile::map`，映射失败回退分块缓冲；根据数据规模自适应使用 `1/2/4` 个 worker 写入预分配数组的不重叠区间，保持点顺序。缓存和日志位于 `%LOCALAPPDATA%/PointCloudSuite/pointcloudview/`，不写入源码树。

### 5.2 点云清理

默认体素尺寸 `0.25 mm`；统计邻域 `K=45`、标准差倍数 `1.30`。每个体素保留真实输入点，不生成质心；结果为空或参数无效时保留原缓存并提示。

### 5.3 平面

三点计算初始平面，RANSAC 抵抗离群点，PCA/最小二乘完成精拟合，再按距离阈值分类并做 UV 连通域过滤。

### 5.4 工件坐标系

自动建立 WObj1：最终提取平面 XYZ 包围盒中心作为 O/WObj1 原点；根据机器人基坐标 X/Y 轴投影自动选取 X+/Y+ 辅助点，再使用 O/X+/Y+ 三点法计算最终坐标系。Z 轴使用拟合平面法向并调整至机器人基坐标 +Z 同向。输出 `T_base_workpiece`、逆矩阵及控制器 `[X,Y,Z,A,B,C]` 姿态角（A=Rx、B=Ry、C=Rz，矩阵为 `Rz(C)×Ry(B)×Rx(A)`），JSON `plane.equation` 只取该三点法最终结果。

### 5.5 边缘与二维输出

确认平面与 WObj1 后，导出矩形由平面点 WObj1 XY 范围自动计算，四边增加 `50 mm`，向上取整到 `10 mm`，固定 `1 px = 0.05 mm`。JSON 流程的 `plane_mask.png` 使用工件坐标系下的边缘分割 `Format_Grayscale8` Mask：背景为 `0`，边缘前景为 `255`。边缘分割完成后自动进入四件套事务提交。

### 5.6 临时工件接口

接口读取 `temp_scanning_info.json`（schema `sr2026-temp-scanning-info-mvp-2`），要求扫描坐标系为 `camera`、标定方向为 `camera -> robot_base`，并校验点云、XML、位姿、三点索引和必填的 ISO 8601 `created_at`。临时扫描接口只接受 `LineProfileXz`；缺少 `point_cloud_layout` 时默认该布局并提示，显式使用 `FullXyz` 时拒绝输入。转换使用点内 `y` 作为扫描比例，并按 Start/End 位姿插值；无效点、行程范围外点、无效刚体矩阵或缺少时间戳分别按稳定错误码拒绝。用户完成平面二次验证和 WObj1 后，边缘分割与四件套提交自动执行，输出 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、边缘 `plane_mask.png` 和 `temp_workpiece_info.json`。四个文件先写入临时目录，再一次性提交；任一失败均回滚。

## 6. 输出与数据契约

- 二维 PNG：平面或边缘 Mask，左上角为像素原点，前景 `255`、背景 `0`。
- JSON：统一 schema `sr2026-temp-workpiece-info-mvp-2`，只允许顶层字段 `schema_version`、`kind`、`created_at`、`plane`、`image`、`roi`、`outputs`。`plane` 只包含 `name`、`equation`，`image` 只包含名称、像素尺寸、物理尺寸和像素尺寸，`roi` 固定为字符串 `rectangle`，`outputs` 只包含三条规范化绝对路径；`plane.equation` 顺序为控制器格式 `[X, Y, Z, A, B, C]`，A=Rx、B=Ry、C=Rz，矩阵约定为 `Rz(C)×Ry(B)×Rx(A)`。临时工件 `created_at` 原样沿用扫描 JSON，普通平面输出使用生成时间。
- `_plane_robot_base.ply`：最终连通域平面点，binary little-endian，顶点属性仅为 `float x/y/z`，坐标保持机器人基坐标；来源和坐标系元数据通过 PLY `comment` 与同名 JSON 保存。
- 输出由 `pcv_output` 统一写出；PNG、PLY、JSON 先写临时目录后成套提交并支持回滚，任一文件失败均视为整体失败。
- `cache/*.pcvbin`：运行缓存；`logs/startup.log`：启动诊断日志。

## 7. 构建、测试与验证

环境为 Windows 10/11、Qt 6.8.3 MSVC x64、C++17、OpenGL 3.3 Core。构建入口为 `scripts/build_windows.ps1`，测试入口为 `scripts/run_tests.ps1`；根目录还提供 `windows-msvc-debug` CMake preset。当前工作区包含 `pcv_output`、`pcv_registration`、`pcv_interface` 及对应测试。

## 8. 非功能需求与已知限制

- 设备/文件错误不能导致界面崩溃；耗时操作不得阻塞主线程。
- 共享模块不得依赖 Qt Widgets，运行文件不得写入源码树。
- 千万级点云受 CPU 内存和显存限制；ASCII 首次读取仍可能较慢。
- Designer 存在历史重复 `objectName` 警告；平坦 2.5D 表面可能缺少 XY 方向约束。

## 9. 待确认事项

1. 手眼标定和机器人基坐标输入的正式接口。
2. 平面/边缘输出与下游 OpenCV 或机器人程序的最终验收格式。
3. 超大图像分块写出、缓存清理周期和运行数据保留策略。
4. 生产环境的误差阈值、点云规模和显卡配置。

## 10. v0.1 版本结论

v0.1 已形成从 PLY 输入、OpenGL 查看、真实点选、平面/边缘处理到 PNG/JSON/PLY 输出的完整点云处理基线，为后续算法增强、机器人接口和产品化追溯提供稳定文档契约。

## 11. 本版本变更记录

- 建立 PointCloudSuite 主程序与共享模块的产品边界和代码说明。
- 固化 PLY 读取、后台解析、平面/边缘算法及默认参数。
- 固化工件坐标、PNG/JSON/机器人基坐标 PLY 输出契约。
- 增加临时扫描到临时工件的手眼转换和四件输出契约。
- 汇总当前构建、测试、限制和下一版本待确认事项。

### 2026-08-24：接口、边缘 Mask 与统一 JSON 契约

- 删除障碍检测及其非阻断告警，不再把障碍状态写入主程序流程或文档验收项。
- 新增 `pcv_registration` 和 `pcv_interface`，统一临时扫描输入、手眼转换、平面/ROI 提取和输出提交。
- 平面输出 JSON 使用 `sr2026-temp-workpiece-info-mvp-2`，输出路径为规范化绝对路径，姿态顺序固定为控制器格式 `[X,Y,Z,A,B,C]`。
- 用户选择输出目录时直接写入该目录；PNG、PLY、JSON 以及临时工件四件套均采用临时目录提交和失败回滚。
