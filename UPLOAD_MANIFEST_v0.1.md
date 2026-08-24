# PointCloudSuite v0.1 手动上传清单

用途：本文件用于核对本期 Gitee `develop` 分支上传范围和验证结果。

## 应上传

- 根工程：`CMakeLists.txt`、`CMakePresets.json`、`.gitignore`、`AGENTS.md`、`README.md`。
- 应用：`apps/pointcloudview/`、`apps/pointcloudstitch/` 及各自的 CMake、UI、源码和 README。
- 共享模块：`src/`、`include/pcv/`，包括 `pcv_output`、`pcv_registration`、`pcv_interface` 实现和头文件。
- 测试：`tests/`，包括 `plane_output_tests.cpp`、`temp_workpiece_interface_tests.cpp`、边缘 Mask 回归及根测试注册文件。
- 工具和脚本：`tools/`、`scripts/`。
- 文档：`docs/` 下的架构、需求、契约、用户指南和开发说明。
- 示例：`examples/` 下的配置和最小平面输出示例；不上传运行结果。

## 不应上传

- `test_pointcloud_a/`：本地回归数据，已移出工作区并加入 `.gitignore`；当前归档在
  `C:\Users\HP\.codex\visualizations\2026\08\21\01a0231b-2a06-7af1-91f7-102acca8fd25\test_pointcloud_a_v0.1_archive`。
- `build*/`、`build-*`、`.qtcreator/`、`mid_gap/`：本地构建、实验或 IDE 产物；
- `.pcvbin`、`cache/`、`logs/`、`exports/`、`.exe`、`.dll`、`.pdb` 等运行和构建文件；
- 任何生产点云、诊断 PLY、临时报告或个人路径下的输入数据。

## 本期工作区摘要

- 新增 `pcv_output`、`pcv_registration`、`pcv_interface` 共享模块和对应测试；
- 删除障碍检测及旧障碍测试语义，保留兼容目标名用于边缘 Mask 回归；
- 平面 PNG/PLY/JSON 输出统一到 `sr2026-temp-workpiece-info-mvp-2`，PNG 前景为 `255`、背景为 `0`；
- 临时工件输出新增 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png` 和 `temp_workpiece_info.json`；
- `pointcloudstitch` 回归报告升级为 `pointcloudstitch-regression-v2`，支持任务输出参数；
- PLY ASCII 读取采用自适应 `1/2/4` worker，紧凑 binary XYZ 使用内部快速路径；
- README、架构、需求、契约、用户指南和示例已按当前源码事实同步。

## 上传前验证

```powershell
git diff --check
git status --short --untracked-files=all
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

## 本轮验证结果（2026-08-21）

- `git diff --check`：通过；
- 文档残留扫描：通过，未发现已废弃的旧输出值、旧图像格式、旧并行策略或旧回归 schema 描述；
- `cmake --preset windows-msvc-debug`：默认路径无写权限，改用等价的仓库内
  `build-v0.1-upload` + Visual Studio 2026 Developer Command Prompt 配置通过；
- Debug 构建：通过，`pointcloudview`、`pointcloudstitch`、诊断工具及 7 个测试目标均成功生成；
- CTest：`7/7 passed`；
- `test_pointcloud_a`：源目录已移除，归档目录 16 个文件、`1,028,473,767` bytes，
  移动前后一致。

本次上传目标：`gitee` remote 的 `develop` 分支。提交和推送结果以本次操作后的
Git 状态和远端确认信息为准。
