# PointCloudSuite v0.2 本地 Git 上传清单

用途：核对 v0.2 维护清理版的本地 commit 范围和验证状态。本清单不授权任何 remote push。

## 应提交

- 代码清理：`apps/pointcloudview/mainwindow.cpp`、`include/pcv/interface/temp_workpiece_interface.h`、`src/interface/temp_workpiece_interface.cpp`、`tests/unit/interface/temp_workpiece_interface_tests.cpp`。
- v0.2 文档：`docs/requirements/pointcloudview_v0.2.md`、`README.md`、`docs/README.md`、`docs/development/build-windows.md`。
- 本清单：`UPLOAD_MANIFEST_v0.2.md`。

## 不应提交

- `build*/`、`build-*`、`.qtcreator/`、`.codex-build-*`；
- `.pcvbin`、`cache/`、`logs/`、`exports/`、`.exe`、`.dll`、`.pdb`；
- 生产点云、诊断输出、临时报告和个人路径下的数据；
- P3/P4 未授权的源码或契约变更。

## 变更摘要

- 删除未编译的历史 `#if 0` 块、旧测试入口、旧 UI 构建函数和零引用辅助符号；
- 保持 v0.1 的 PLY、平面、Mask、JSON、临时工件和错误码契约；
- P3 手眼副本统一、P4 `wobj_num`/`name` 决策继续延期；
- 本地 Git 仅创建 commit，不创建 tag、不执行 `git push`。

## 上传前验证

```powershell
git diff --check
git status --short --untracked-files=all
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1 -BuildDir C:\qt-build-pointcloudsuite
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
.\scripts\build_windows.ps1 -Config Release
```

还需确认 `apps/`、`src/`、`include/`、`tests/` 和 CMake 文件中无 `vectorArray`、`matrixArray`、`kTempPlaneName`、`buildUiLegacy` 和 `#if 0` 残留（版本文档中的变更记录除外），并检查 staged diff 只包含本清单列出的文件。

## 当前会话验证记录

- `git diff --check`：已通过；
- 工作树范围：原有 4 个代码文件，未发现其他用户修改；
- Debug 构建：通过；`build_windows.ps1 -Config Debug -BuildTests` 成功生成 `pointcloudview`、`pointcloudstitch` 和 8 个测试目标；
- CTest：通过，`8/8` 测试通过（总耗时约 `1.93 sec`）；
- Release 构建：通过；`build_windows.ps1 -Config Release` 成功生成两个桌面目标；部署阶段提示缺少 Visual Studio release redistributable 文件，但不影响构建产物生成；
- 构建提示：Vulkan headers 未找到、Qt Designer 存在既有重复 `objectName` 警告，均未导致失败；
- GUI 手工流程：未验证；
- remote push：未执行。

## 本地提交记录

提交消息：`chore: clean dead code and document v0.2 maintenance baseline`
Commit hash：以 `git log -1 --oneline` 为准
提交后状态：本地 commit 已创建；未创建 tag，未执行 remote push
