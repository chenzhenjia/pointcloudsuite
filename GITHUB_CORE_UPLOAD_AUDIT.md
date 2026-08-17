# PointCloudView GitHub 核心文件上传审计

审计日期：2026-08-17  
工作区根目录：`D:/workpiece/pointcloudview`  
Git 仓库根目录：`D:/workpiece/pointcloudview/pointcloudview`  
当前分支：`fix/e11b12e-api`

## 1. 审计结论

整个工作区约有 3,130 个文件，占用约 2.47 GB。当前 Git 仓库实际只跟踪
16 个文件，合计约 461 KB。约 2.463 GB 都是 CMake、编译器、Qt 部署工具
生成的构建目录，可以从源码重新生成。

上传 GitHub 时应以内部的 `pointcloudview` 目录作为仓库根目录。外层
`hand_eye` 目录当前不属于 Git 仓库。

| 分类 | 大约大小 | 处理建议 |
|---|---:|---|
| Qt/C++ 核心程序 | 390 KB | 上传 |
| 当前全部 Git 跟踪文件 | 461 KB | 对例外文件脱敏后上传 |
| 构建目录 | 2,462.82 MB | 不上传；确认程序未运行后可删除 |
| 诊断日志、图片和文本 | 0.99 MB | 不上传；转移到私有归档或删除 |
| Qt Creator 本机状态 | 0.03 MB | 不上传 |
| 设备专用手眼标定 XML | 小于 0.01 MB | 禁止上传公开仓库 |

## 2. 必须保留并上传的核心文件

下面是正常配置和构建 GUI 程序所需的最小源码集合：

```text
.gitignore
CMakeLists.txt
main.cpp
mainwindow.cpp
mainwindow.h
mainwindow.ui
pointcloudprocessor.cpp
pointcloudprocessor.h
```

各文件用途：

- `main.cpp`：程序入口、桌面 OpenGL、高性能显卡导出标记和启动诊断。
- `mainwindow.cpp/.h`：Qt 主窗口、OpenGL 画布、PLY 加载、配准、去噪、
  平面提取、障碍检测、边缘处理和 2D 图像输出。
- `mainwindow.ui`：当前生效的 Qt Designer 原生界面。审计未发现依赖外部
  QRC、PNG、ICO 或 SVG 资源。
- `pointcloudprocessor.cpp/.h`：PLY/缓存 IO 和全部点云处理算法。
- `CMakeLists.txt`：Qt 6、C++17、可选测试目标和部署规则。
- `.gitignore`：阻止构建目录、Qt Creator 状态、可执行文件、DLL、PDB 和
  `.pcvbin` 缓存进入 Git。

## 3. 建议一并上传的开发支持文件

这些文件不是正常程序运行时的强制依赖，但有利于在其他电脑复现、维护和
验证项目：

```text
CMakePresets.json
build_windows.ps1
clean_reconfigure.ps1
QT_CREATOR_BUILD.md
POINTCLOUDVIEW_REQUIREMENTS_ARCHIVE.md
REGISTRATION_DISTORTION_DEVELOPMENT.md
tests/pointcloudprocessor_obstacle_tests.cpp
GITHUB_CORE_UPLOAD_AUDIT.md
```

说明：

- `CMakePresets.json` 和 PowerShell 脚本保存 Qt 6.8.3/MSVC 构建路线，路径
  均为默认值，可以通过参数覆盖。
- `POINTCLOUDVIEW_REQUIREMENTS_ARCHIVE.md` 是当前项目完整功能档案。
- `REGISTRATION_DISTORTION_DEVELOPMENT.md` 记录 Eye-in-Hand 配准假设和
  畸变诊断过程。
- `tests/pointcloudprocessor_obstacle_tests.cpp` 在启用
  `POINTCLOUDVIEW_BUILD_TESTS=ON` 时构建，覆盖障碍和断开平面判断。

## 4. 公开上传前必须脱敏或调整的文件

### 4.1 `registration_diagnostic_runner.cpp`

该文件已被 Git 跟踪，同时被 CMake 可选诊断目标引用，但不能原样上传公开
仓库，因为其中包含：

- 本机 PLY 数据绝对路径；
- 路径中的相机/设备序列信息；
- 一组实际 Eye-in-Hand 标定矩阵；
- 实际机器人 Start/End 位姿；
- 固定的诊断输出文件名。

公开上传前应二选一：

1. 将数据路径、标定矩阵和机器人位姿改为命令行参数或中性合成示例，保留
   诊断目标；或
2. 删除该文件，并从 `CMakeLists.txt` 的测试块中删除
   `registration_diagnostic_runner` 目标。

如果直接推送现有 Git 历史，仅删除当前文件是不够的，因为旧提交中仍然存在
这些实际参数。

### 4.2 `POINTCLOUDVIEW_REQUIREMENTS_ARCHIVE.md`

该档案应保留，但当前包含一条实际验证数据目录和设备编号。公开上传前应将
实际路径替换成通用示例路径。

### 4.3 `hand_eye/.../standard_transform_gui.py`

当前路径：

```text
D:/workpiece/pointcloudview/hand_eye/
  MV-DP2240-01P_Hand_Eye_Convert/standard_transform_gui.py
```

主程序进行批量点云世界坐标转换时并不调用该脚本，实际转换算法已经在 C++
中实现。该脚本只供“打开标定工具”按钮启动，是可选辅助工具。

如果要把辅助工具上传 GitHub：

- 将脱敏副本移入仓库，例如 `tools/hand_eye/standard_transform_gui.py`；
- 删除脚本中的默认 XML 绝对路径和生产示例坐标；
- 写明 Python 3.13、Tkinter 和 NumPy 依赖；
- 增加代码所有权或许可证说明；
- 修改 `openHandEyeTool()`，使其指向新的仓库/部署相对路径。

如果不上传该脚本，主 GUI、C++ 世界坐标转换和点云配准仍可正常构建运行，
但“打开标定工具”按钮无法启动 Python 工具。

## 5. 禁止上传的文件

### 5.1 设备专用标定 XML

```text
D:/workpiece/pointcloudview/hand_eye/
  MV-DP2240-01P_Hand_Eye_Convert/
  EyeInHand-INOVANCE-MV-DP2240-01P-20260811170638.xml
```

这是相机和机器人专用标定数据，文件名还包含设备身份。应放在安全的部署配置
目录中。GUI 已支持运行时选择 XML，源码仓库不需要携带生产标定文件。

### 5.2 Qt Creator 本地状态

```text
.qtcreator/
```

其中是本机 Kit 和构建设置，已经被 `.gitignore` 忽略。重新用 Qt Creator
打开 `CMakeLists.txt` 后会自动生成。

### 5.3 全部构建目录

以下目录都属于 CMake、编译器或 `windeployqt` 生成物，可由源码重建：

| 目录 | 大约大小 |
|---|---:|
| `build/` | 416.33 MB |
| `build-api-fix/` | 187.11 MB |
| `build-api-release/` | 76.83 MB |
| `build-api-tests/` | 209.06 MB |
| `build-clean/` | 235.12 MB |
| `build-cycle/` | 6.41 MB |
| `build-diagnostic/` | 7.92 MB |
| `build-diagnostic-ninja/` | 91.21 MB |
| `build-diagnostic-real/` | 0.21 MB |
| `build-fix/` | 183.79 MB |
| `build-gpu/` | 175.41 MB |
| `build-no-opengl/` | 156.94 MB |
| `build-plane/` | 200.24 MB |
| `build-tests-current/` | 309.94 MB |
| `build-tests-msvc1452/` | 206.29 MB |

构建目录合计约 **2,462.82 MB**。删除前必须确认没有从目标目录启动的程序，
否则 Windows 会锁定 EXE。删除它们只会移除二进制文件、Qt DLL、OBJ、PDB
和 CMake 缓存，不会删除源码。

### 5.4 诊断输出文件

下面 12 个未跟踪文件由配准诊断程序或构建日志捕获生成，不是程序输入：

```text
diagnostic_build.log
main_build.log
phase1_build.log
phase2_build.log
phase3_build.log
phase4_build_final.log
Point_Cloud_A_robot_world_fixed_mid_top.png
Point_Cloud_A_robot_world_icp_top.png
Point_Cloud_A_robot_world_swapped_top.png
Point_Cloud_A_robot_world_top.png
robot_world_diagnostic.txt
robot_world_diagnostic_fixed.txt
```

配准故障分析结束后可以删除。如果需要保留为工程证据，应转移到独立私有归档，
不要放入源码仓库。

### 5.5 点云数据和运行缓存

生产数据和缓存不应上传：

```text
*.ply
*.pcd
*.pcvbin
.pointcloudview-merge.pcvbin
startup.log
```

`.pcvbin` 已经被忽略。PLY/PCD 文件通常很大，还可能包含真实工件几何，应存放
在外部或私有数据集仓库。

## 6. `.git` 目录如何处理

`D:/workpiece/pointcloudview/pointcloudview/.git` 是本地仓库元数据，不是需要
复制到上传包中的普通文件。如果还需要当前分支和提交历史，不要删除它。正常
执行 `git push` 时会自动使用该目录，但 `.git` 本身不会被提交。

如果目标是建立公开的“核心源码仓库”，建议新建一个只包含审核通过清单的干净
仓库，而不是直接推送现有全部历史。现有历史中包含已删除文件，以及带真实路径
和标定参数的诊断程序。

## 7. 建议补充的 `.gitignore` 规则

当前 `.gitignore` 已覆盖 `/build*/`、`/.qtcreator/`、二进制文件、PDB 和
`*.pcvbin`。公开上传前建议增加：

```gitignore
# Local datasets and device calibration
*.ply
*.pcd
/hand_eye/**/*.xml

# Runtime/build diagnostics
*.log
/Point_Cloud_A_robot_world_*.png
/robot_world_diagnostic*.txt

# Local runtime state
/startup.log
```

不要使用全局 `*.png`，否则以后真正属于 UI 的图片资源也会被排除。

## 8. 当前未提交修改

当前分支还有自动障碍检测和断开平面阻断相关修改未提交：

```text
POINTCLOUDVIEW_REQUIREMENTS_ARCHIVE.md
mainwindow.cpp
mainwindow.h
mainwindow.ui
pointcloudprocessor.cpp
pointcloudprocessor.h
tests/pointcloudprocessor_obstacle_tests.cpp
```

这些修改已经通过障碍专项测试和独立 GUI 完整构建，但在制作 GitHub 快照前
必须先提交，或者明确决定不纳入上传版本。

## 9. 推荐的公开仓库结构

完成脱敏后，建议 GitHub 仓库保留：

```text
pointcloudview/
├─ .gitignore
├─ CMakeLists.txt
├─ CMakePresets.json
├─ build_windows.ps1
├─ clean_reconfigure.ps1
├─ main.cpp
├─ mainwindow.cpp
├─ mainwindow.h
├─ mainwindow.ui
├─ pointcloudprocessor.cpp
├─ pointcloudprocessor.h
├─ POINTCLOUDVIEW_REQUIREMENTS_ARCHIVE.md
├─ QT_CREATOR_BUILD.md
├─ REGISTRATION_DISTORTION_DEVELOPMENT.md
├─ GITHUB_CORE_UPLOAD_AUDIT.md
├─ tests/
│  └─ pointcloudprocessor_obstacle_tests.cpp
└─ tools/                         可选
   └─ hand_eye/
      └─ standard_transform_gui.py
```

只有在完成脱敏并保留 CMake 诊断目标时，才加入
`registration_diagnostic_runner.cpp`。

## 10. 上传前检查清单

1. 提交当前需要保留的源码修改。
2. 脱敏诊断程序和需求档案中的真实路径、设备编号及标定值。
3. 保证生产手眼 XML、PLY 和 PCD 数据位于 Git 之外。
4. 检查 `git status --short`，确认不存在日志、图片、XML、PLY 或构建文件。
5. 使用最终上传清单在全新构建目录配置并编译一次。
6. 运行障碍检测测试，并从全新构建启动 GUI 一次。
7. 将审核后的文件推送到新的 GitHub 仓库或干净分支。

