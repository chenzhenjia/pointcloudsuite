# PointCloudView 第二阶段开发交接记录

更新时间：2026-08-25  
工作区：`D:\workpiece\pointcloudview\pointcloudview`  
当前状态：第二阶段部分实现，尚未构建或测试，不得视为完成。

## 新对话接续提示

在新对话中先发送以下内容，并让开发 Agent 先阅读本文件和当前 `git diff`：

```text
继续 PointCloudView 第二阶段开发。请先阅读：
D:\workpiece\pointcloudview\pointcloudview\docs\pointcloudview_phase2_development_handoff_2026-08-25.md

必须先执行 git status --short，保留当前全部未提交修改，不修改 workpiece_list.json。
按交接记录完成“平面二次验证与 WObj1 坐标系”，然后进行完整 Debug 构建、CTest 和 git diff --check。
不要开始第三阶段 ROI、2D 正式输出或四件套提交。
```

## 阶段目标和边界

第二阶段接收第一阶段已经转换到 `robot_base` 的 `LineProfileXz` 点云，完成：

```text
第一组三点取平面
    -> 候选/全量平面分类
    -> 第二组三点强制验证
    -> 平面包围盒中心作为 WObj1 原点
    -> 按机器人基坐标轴投影自动建立 WObj1
    -> 建立并显示 WObj1
```

本阶段禁止自动执行矩形 ROI、2D 图像正式输出和临时工件四件套提交；不得自动调用 `finalizeTempWorkpiece()`。

第一阶段约束已经调整为只采用 `LineProfileXz`，不采用 `FullXyz`。手眼转换公式仍为：

```text
p_robot_base = T_base_flange(t) * T_flange_depth * p_depth
```

## 冻结的业务规则

- 二次验证适用于临时扫描和普通 PLY 的全部三点取平面流程。
- 第一组三点是平面种子，第二组三点只用于平面一致性验证；WObj1 轴不依赖任何取点方向。
- 第二组三点内部不得重复且不能共线。
- 法向夹角使用 `acos(abs(dot(normal1, normal2)))`，方向相反视为同一法向。
- 法向夹角阈值固定为 `1.0 deg`。
- 第二组三点到第一拟合平面的最大距离阈值固定为 `0.4 mm`。
- 共线、非法输入或复用点：只清除第二组三点，允许重新选择第二组。
- 夹角或距离超限：清除第一平面候选、第二组三点和 WObj1，强制返回第一组三点流程。
- 快速候选和全量分类生成新平面结果时都必须重置二次验证状态。
- WObj1 原点固定为最终平面点 XYZ 包围盒中心 `m_planeCenter`；原点 `[0,0,0]` 合法。
- 自动辅助点仅用于显示和距离诊断，不参与 WObj1 轴计算。
- Z 轴使用最终拟合平面单位法向，继续复用现有右手系和 Y 点定向逻辑。
- WObj1 失败时保留已经通过的平面二次验证，但禁止进入后续流程。

## 当前未提交修改

执行 `git status --short` 时共有以下 10 个修改文件：

```text
M apps/pointcloudview/mainwindow.cpp
M apps/pointcloudview/mainwindow.h
M apps/pointcloudview/pointcloudprocessor.cpp
M apps/pointcloudview/pointcloudprocessor.h
M docs/contracts/point_cloud_plane_output_v0.1_proposal.md
M docs/requirements/pointcloudview_v0.1.md
M include/pcv/interface/temp_workpiece_interface.h
M src/interface/temp_workpiece_interface.cpp
M tests/unit/interface/temp_workpiece_interface_tests.cpp
M tests/unit/pointcloudview/pointcloudprocessor_obstacle_tests.cpp
```

这些文件包含用户已有的第一阶段和临时流程改动。禁止 `git reset`、`git restore` 或覆盖式回退。

## 已落地但尚未验证的第二阶段改动

### 纯几何 API

`apps/pointcloudview/pointcloudprocessor.h` 已新增：

- `PlaneConsistencyStatus`
- `PlaneConsistencyResult`
- `PlaneBoundsCenterResult`
- `validatePlaneConsistency(...)`
- `calculatePlaneBoundsCenter(...)`

`apps/pointcloudview/pointcloudprocessor.cpp` 已初步实现：

- 两组三点索引、有限值、组内重复和跨组复用校验；
- 第二组三点共线校验；
- `abs(dot)` 法向夹角计算；
- `1.0 deg` 和 `0.4 mm` 阈值判断；
- 最终平面点 XYZ 包围盒中心计算，并允许中心为 `[0,0,0]`。

实现尚未编译。继续开发时优先收敛参考平面的归一化和距离计算：

```cpp
const float planeNorm = referenceNormal.length();
const float normalizedD = referencePlane.d / planeNorm;
referenceNormal /= planeNorm;
distance = std::abs(QVector3D::dotProduct(referenceNormal, point) + normalizedD);
```

### UI 门禁

`MainWindow::updatePlaneExtractionUi()` 已部分修改：

- `verificationPassed = m_secondPlaneValidated && m_secondPlaneSamePlane`；
- 第二组三点按钮依赖候选存在，不应依赖候选已经确认；
- X/Y 轴点按钮不再作为 WObj1 建立条件；
- “确定候选平面”只要求候选平面和二次验证通过。

此部分尚未编译，也尚未完成周边状态同步。

## 尚未完成的实现

按以下顺序继续，避免状态机死锁。

1. 修正 `validatePlaneConsistency()` 的平面归一化和距离实现，然后先编译处理器测试目标。
2. 重写 `MainWindow::validateSecondPlaneSelection()`，统一调用 `pointcloud::validatePlaneConsistency(...)`。
3. 实现验证结果分流：
   - `Passed`：保存角度/距离并允许按机器人基坐标轴投影自动建立 WObj1；
   - `Collinear`、`InvalidInput`、`ReusedPoint`：只清除第二组三点；
   - `AngleExceeded`、`DistanceExceeded`：清除当前候选、两组三点相关状态、WObj1、边缘/图像状态并回到第一组三点选择。
4. 在 `startPlanePointSelection()`、`abandonPlanePointSelection()`、`runPlaneExtraction()` 或 `planeExtractionFinished()`、`cancelPlaneCandidate()` 和新画布发布路径中统一重置二次验证状态；删除目前明显重复的 reset 语句。
5. 修改 `startSecondPlanePointSelection()`：候选存在即可开始，不能再要求 `m_planeCandidateConfirmed`；开始时关闭轴点模式并重置旧验证状态。
6. 修改 `handleCanvasPointPicked()`：轴点额外排除 `m_secondPlanePointIndices`，画布标记保留第一组、第二组和 X/Y 轴点。
7. 修改 `startWorkpieceAxisSelection()`：要求二次验证通过；使用候选存在和中心有限值判断，不能使用 `m_planeCenter.isNull()`。
8. 修改 `clearWorkpieceAxisSelection()`：只清除轴点和 WObj1，保留第一/第二组三点及验证通过状态。
9. 在 `planeExtractionFinished()` 中调用 `calculatePlaneBoundsCenter()`，失败时拒绝候选；新候选必须重置二次验证和 WObj1。
10. 修改 `confirmPlaneCandidate()`：所有会话统一要求二次验证和独立 X/Y 轴点；删除临时会话用第一组三点的 `P1/P2/P3` 自动建立 WObj1 的旧分支；统一调用：

```cpp
pointcloud::buildWorkpieceCoordinateSystem(
    m_points,
    m_planeCenter,
    fittedNormal,
    m_xAxisPointIndex,
    m_yAxisPointIndex,
    true);
```

11. 删除 `confirmPlaneCandidate()` 成功后自动调用 `extractPlaneImage()` 的临时会话分支。
12. 删除 `planeImageExtractionFinished()` 中临时会话自动调用 `finalizeTempWorkpiece()` 的逻辑；保留函数本体给第三阶段显式调用。

## 当前代码中已确认的旧逻辑问题

- `startSecondPlanePointSelection()` 仍要求 `m_planeCandidateConfirmed`，与新状态机冲突。
- `validateSecondPlaneSelection()` 仍在 UI 层手工计算，角度阈值仍为旧值，且失败行为未按结构化状态区分。
- `startWorkpieceAxisSelection()` 仍使用 `m_planeCenter.isNull()`，会错误拒绝合法原点 `[0,0,0]`。
- 轴点选择只排除了第一组三点，尚未排除第二组三点。
- 轴点清除后画布只恢复第一组三点标记，未保留第二组三点。
- `planeExtractionFinished()` 仍手工计算中心，尚未调用纯几何 API。
- `confirmPlaneCandidate()` 仍允许临时会话复用第一组三点建立 WObj1。
- `planeImageExtractionFinished()` 仍会自动调用 `finalizeTempWorkpiece()`。
- 多个状态重置位置存在重复的 `m_secondPlaneValidated = false` 和 `m_secondPlaneSamePlane = false`。

## 待补测试

在现有 `tests/unit/pointcloudview/pointcloudprocessor_obstacle_tests.cpp` 中补充纯几何覆盖：

- 同向同平面通过；
- 反向点序产生反向法向仍通过；
- 夹角 `0.99 deg` 通过；
- 夹角 `1.01 deg` 返回 `AngleExceeded`；
- 最大距离 `0.4 mm` 通过；
- 最大距离大于 `0.4 mm` 返回 `DistanceExceeded`；
- 第二组三点共线返回 `Collinear`；
- 两组复用点返回 `ReusedPoint`；
- 空输入和索引越界返回 `InvalidInput`；
- 包围盒中心计算正确；
- 包围盒中心为 `[0,0,0]` 时仍 `ok == true`；
- 使用独立 X/Y 点建立 WObj1，原点、Z 轴、矩阵及逆矩阵正确；
- X/Y 点过近、轴向退化或共线时拒绝。

角度用例可令第一平面为 `z = 0`，第二组三点满足 `z = tan(theta) * x`。

## 验证要求

由于修改了 P0 级 `apps/pointcloudview/pointcloudprocessor.{h,cpp}` 和 UI 状态机，完成后必须完整 Debug 重编译和全量 CTest。

当前可用命令：

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B .codex-build-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" -DPCV_BUILD_TESTS=ON -DPCV_BUILD_TOOLS=ON && "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build .codex-build-debug'

& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir .codex-build-debug --output-on-failure

git diff --check
git status --short
```

最低验收目标包括：

```text
handeye_transform_tests
temp_workpiece_interface_tests
pointcloudprocessor_obstacle_tests
ply_reader_tests
cloud_cache_tests
```

第一阶段此前记录为 `8/8 passed`，但第二阶段新增修改后尚未重新构建，不能沿用该结果宣称当前代码通过。

## 风险和人工验收

- 当前改动量较大且夹杂第一阶段未提交代码，继续开发必须逐段检查 `git diff`。
- 真实 GUI 点击流程仍需使用可控 PLY fixture 人工验证：候选生成、二次验证失败回退、二次验证通过、独立轴点选择、WObj1 显示。
- 关闭窗口时后台任务和 QWidget/OpenGL 线程约束仍需烟测。
- 未使用真实 XML/PLY fixture 验证数值精度时，不得宣称生产级验证。
- 不修改 `workpiece_list.json`，不开始第三阶段 ROI 和正式四件套输出。
