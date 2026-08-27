# PointCloudSuite

PointCloudSuite 是基于 Qt 6 和 C++17 的完整点云处理项目，包含点云查看处理
主程序、点云拼接程序、共享算法源码、诊断工具和自动化测试。本仓库以源码工程
形式交付，不是 SDK 安装包。

## 项目组成

- `pointcloudview`：点云读取、显示、处理和分析主程序。
- `pointcloudstitch`：手眼转换、多帧配准、拼接和融合程序。
- 共享模块：PLY 读取与缓存、降采样、统计滤波、运行目录管理、手眼变换、临时工件接口和平面输出契约。
- 辅助能力：注册诊断工具、单元测试、构建脚本和产品文档。

## 目录结构

```text
PointCloudSuite/
|-- apps/                  # 两个桌面程序的入口、界面和专属业务代码
|   |-- pointcloudview/
|   `-- pointcloudstitch/
|-- src/                   # 可复用算法和基础设施实现
|-- include/pcv/           # src 中共享模块对应的项目头文件
|-- tests/                 # 单元测试
|-- tools/                 # 可选诊断工具
|-- docs/                  # 架构、需求、开发和用户文档
|-- examples/              # 示例配置与非生产样例
|-- configs/               # 项目级配置与示例
|-- scripts/               # Windows 构建和测试脚本
|-- cmake/                 # 项目级 CMake 辅助文件
|-- CMakeLists.txt
`-- CMakePresets.json
```

各目录中的 `README.md` 说明该部分的职责和维护边界。

## 环境要求

- Windows 10/11
- CMake 3.19 或更高版本
- 支持 C++17 的 MSVC
- Qt 6.5 或更高版本，所需组件为 Core、Gui、Widgets、Concurrent、
  OpenGL 和 OpenGLWidgets

源码路径和构建路径必须只包含 ASCII 字符。

## 构建

在 Visual Studio Developer PowerShell 中执行：

```powershell
.\scripts\build_windows.ps1 -Config Release
```

脚本也可在普通 PowerShell 中运行：当 `cl.exe`/`nmake.exe` 不在 PATH 时，会尝试通过
`vswhere.exe` 或标准安装路径加载 `vcvars64.bat`，并明确校验 Qt、CMake 和 CTest 路径。

构建调试版本并启用测试：

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
```

也可以使用根目录的 `CMakePresets.json`。构建输出应放在源码目录之外，
运行产生的缓存、日志和导出文件位于操作系统应用数据目录。

Debug 预设会启用测试和诊断工具：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure
```

## 源码边界

- 通用算法放入 `src/`，对应头文件放入 `include/pcv/`。
- 界面、程序入口和仅被单个程序使用的流程放入对应的 `apps/<name>/`。
- `apps` 可以依赖共享模块，共享模块不能反向依赖 `apps`。
- 两个程序保持独立可执行目标，不复制共享算法实现。

详细架构见 [docs/architecture/overview.md](docs/architecture/overview.md)，
各程序需求见 [docs/requirements](docs/requirements)，平面输出契约见
[docs/contracts](docs/contracts)。当前版本为 `v0.2` 维护清理版；`v0.1` 文档保留为历史基线，
不包含机器人控制、手眼标定求解或生产数据库接入。

v0.2 仅删除未编译的历史代码和零引用符号，保持 v0.1 的活动功能、错误码和输出契约不变。
构建工具若位于工作区外并被执行环境拦截，请在 Windows Developer PowerShell 或具备授权的本机环境执行构建。

## 2026-08-24 实现同步

- 删除 `pointcloudview` 的障碍检测、红色障碍显示和非阻断告警；当前流程保留平面提取、边缘分割和二维 Mask 输出。
- 新增 `pcv_registration` 手眼变换共享模块：读取 `RTmatDepth2robot`，校验刚体矩阵，按 `T_base_flange(t) * T_flange_depth` 将线扫点转换到 `robot_base`。
- 新增 `pcv_interface` 临时工件接口：读取 `sr2026-temp-scanning-info-mvp-2`，生成 `baseline_robot_base.ply`、`roi_template_robot_base.ply`、`plane_mask.png` 和 `temp_workpiece_info.json`。
- 平面与边缘 Mask 输出统一使用 `sr2026-temp-workpiece-info-mvp-2`；所有位姿数组统一使用控制器顺序 `[X, Y, Z, A, B, C]`（A=Rx、B=Ry、C=Rz），矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`，输出路径写入规范化绝对路径。
- 边缘 Mask 导出复用真实平面坐标与物理栅格信息；用户选择 `destinationDirectory` 时直接写入该目录，PNG、PLY、JSON 使用临时目录成套提交并在失败时回滚。
- 主界面新增“打开扫描 JSON”入口：后台调用 `pcv_interface`，显示 `scan_id`、布局、转换点数、平面点数、ROI 点数和输出 JSON 路径；成功后不替换当前画布，关闭窗口时等待后台任务结束。
