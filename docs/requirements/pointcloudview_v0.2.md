# PointCloudSuite 点云查看与处理程序需求文档

> 状态：历史归档。当前项目基线请参阅 `docs/requirements/pointcloudview_v0.3.md`。本文仅保留 v0.2 历史事实，不作为当前实现规范。最后核对日期：2026-08-31。

版本：v0.2
日期：2026-08-28
适用范围：`pointcloudview` 点云查看处理程序及其共享点云模块

## 1. 版本定位

v0.2 以 v0.1 的可运行功能和输出契约为基线，包含维护清理、文档同步以及第一组 n 点平面拟合增强。当前实现仍以源码、CMake 和测试为真相源；历史 v0.1 文档保留为基线归档。

## 2. v0.1 功能基线（保持不变）

- 读取 ASCII、Binary Little Endian、Binary Big Endian PLY，并在 Qt/OpenGL 画布中查看。
- 支持真实点选择、第一组 n 点（`n >= 3`）平面提取、RANSAC/PCA 精拟合、连通域过滤和 WObj1 坐标系计算。
- 第一组全部控制点参与初始 PCA/最小二乘平面拟合；少于 3 点、重复、越界、非有限或近似共线的控制点被拒绝。RANSAC 候选和最终连通域必须包含全部控制点。
- 第二组固定 3 点只用于验证第一拟合平面，不替换模型；验证保留 `1.0°` 法向夹角和 `0.4 mm` 最大距离阈值。
- 输出 Grayscale8 的 PNG Mask、robot_base 平面 PLY 以及 `sr2026-temp-workpiece-info-mvp-2` JSON。
- 读取临时扫描 JSON，执行 camera 到 `robot_base` 的转换，并生成临时工件四件套。
- 共享模块保持 `pcv_output`、`pcv_registration`、`pcv_interface` 的依赖边界；工作线程不得访问 QWidget/OpenGL。

## 3. v0.2 变更

本版本删除以下不参与编译或无仓库内引用的内容：

- `src/interface/temp_workpiece_interface.cpp` 中的历史 `#if 0` 实现、`vectorArray` 和 `matrixArray`；
- `tests/unit/interface/temp_workpiece_interface_tests.cpp` 中的历史 `#if 0` 测试块和旧 `main`；
- `apps/pointcloudview/mainwindow.cpp` 中的 `buildUiLegacy()` 历史 UI；
- `include/pcv/interface/temp_workpiece_interface.h` 中的 `kTempPlaneName`。

上述删除不得改变活动 API 的运行逻辑、输出字段、路径规则、错误码或测试语义。源码、头文件、测试和 CMake 构建范围必须不再出现这些符号和 `#if 0`；版本文档可保留删除记录。

第一组平面取点在达到 3 点后继续保持 Picking 状态，用户可继续添加任意数量的控制点，点击“确定平面”后才启动后台拟合；撤销每次移除最后一个控制点。拟合失败保留当前控制点并恢复选择，允许继续取点或撤销后重试。

## 4. 接口、契约与兼容性

- 临时工件输入继续使用 `sr2026-temp-scanning-info-mvp-2`；输出继续使用 `sr2026-temp-workpiece-info-mvp-2`。
- 位姿数组保持 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`；矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- PNG 前景为 `255`、背景为 `0`，像素尺寸默认为 `0.05 mm/px`；四件套采用临时目录成套提交和失败回滚。
- `kTempPlaneName` 是公开头文件符号。本版本假设不存在仓库外编译消费者；若发现外部依赖，应在后续兼容性修订中恢复弃用别名，不得静默破坏外部构建。

## 5. 非目标与延期事项

- 不统一 `apps/pointcloudstitch/handeye_transform.{h,cpp}` 与共享 `pcv_registration` 的 P3 副本语义。
- 不决定 `plane.wobj_num=1` 与 `plane.name="WObj1"` 的 P4 正式契约。
- 不新增机器人控制、手眼标定求解、ICP、数据库或生产追溯功能。

## 6. 验收要求

### 静态验收

- `git diff --check` 通过；
- `pointcloudprocessor_obstacle_tests` 覆盖 3 点兼容入口、n 点拟合、控制点退化/越界/非有限值、控制点容差和 n 点参考组三点验证；
- 在 `apps/`、`src/`、`include/`、`tests/` 和 CMake 文件中扫描不到 `vectorArray`、`matrixArray`、`kTempPlaneName`、`buildUiLegacy` 和 `#if 0`；
- staged diff 仅包含本次 4 个代码文件及 v0.2 文档文件；
- 不产生源码树内构建、缓存、日志或点云文件。

### 构建与测试验收

在 Windows Developer PowerShell 或具备授权的本机环境执行：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release
```

Debug 全量构建、全部 CTest（重点是 `temp_workpiece_interface_tests`）和 Release 构建均成功后，才可创建本地发布 commit。GUI 手工点击流程仍需人工验收，不能由单元测试替代。

## 7. 构建错误处理

若系统找不到 `cmake.exe`、`ctest.exe` 或 Qt 6，应使用本机 Qt Creator Kit/Developer PowerShell，或通过 `-QtDir`、`CMAKE_PREFIX_PATH` 或 `Qt6_DIR` 指定 Qt。`cl.exe` 不在 PATH 时构建脚本会通过 `vswhere.exe` 初始化 MSVC。若出现编译器、链接器或 CTest 错误，须保留完整错误文本并在修复前暂停提交。

## 8. 本地 Git 发布边界

- 在当前 `master` 分支创建一个本地 commit，建议消息为 `chore: clean dead code and document v0.2 maintenance baseline`。
- 不创建 tag，不执行 `git push`，不操作任何 remote。
- 提交前后均检查 `git status --short`、提交统计和最近一次 commit；不得使用 `git reset --hard` 覆盖用户修改。

## 9. v0.2 版本结论

v0.2 保持既有输出契约，并将第一组平面拟合增强为 n 点控制。其发布条件是静态残留检查、Debug/Release 构建和 CTest 全部通过；在构建工具权限或其他错误未闭环时，版本状态必须标记为未验证。

## 10. 变更记录

- 2026-08-27：删除历史死代码和零引用符号，新增 v0.2 维护需求与本地上传清单，补充构建权限错误处理和兼容性风险说明。
- 2026-08-28：第一组平面提取改为手动结束的 n 点控制，全部控制点参与初始 PCA/最小二乘拟合；第二组继续固定三点验证。
