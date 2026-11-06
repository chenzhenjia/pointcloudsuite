# 应用入口与冗余 target 清理 v1

日期：2026-11-05。Owner：Codex（实施与静态检查）、用户（构建、CTest、GUI及设备验收）。本轮按明确授权删除废弃源码和无引用target；未修改软件版本、输出schema或领域算法，未提交/push/上传。

## 保留入口与功能

| entry | default | responsibility |
|---|---|---|
| scan_geometry_demo_v1 | ON | 当前扫描与几何选择GUI，真实会话、选面、拼接、修剪与独立拟合面 |
| pointcloudview | ON | 原工作台，相机/RAW、点云、CAD/模板、整帧及所选平面拼接等原有能力 |
| pointcloudcli | OFF | 只读摘要及临时工件命令，PCV_BUILD_CLI启用 |
| registration_diagnostic | OFF | 原诊断工具，PCV_BUILD_TOOLS启用 |
| depth_raw_to_ply_v1 | OFF | RAW转换，PCV_BUILD_TOOLS且SDK可用时构建 |
| validate_point_cloud_result_v1.py | 无编译target | 独立只读契约校验脚本，保留 |

模块10/20/30/50/60、core/infrastructure保留。`pcv_selected_plane_tasks_v2`供两个GUI使用；`pcv_stitching_ui`只在pointcloudview开启时构建，整帧窗口从工作台访问。相机/亮度/RAW、拟合、拼接与输出语义不变。新GUI target/目录中的demo命名保留，不在本轮重命名。

## 删除清单与迁移

本轮删除32个受版本控制文件：

- `apps/intensity_viewer`的5个源码/表单/CMake文件；不删除模块10亮度读取和相机代码。
- `apps/pointcloudstitch`的12个受版本控制文件：入口、旧窗口壳、processor/seam包装、`regressionrunner`、表单、README和CMake。其`--regression`独有功能按用户选择一并删除，不迁移、不声明现有诊断工具等价替代。
- 新GUI旧模拟数据/画布/模型各一对`.cpp/.h`，共6个文件；两份旧模拟专属测试源码。删除`pcv_scan_geometry_demo_model_v1`及其测试target，保留真实v2/契约测试、Designer表单和资源。
- `src/io`、`src/filtering`、`src/registration`、`src/interface`、`src/output`的5个纯转发CMake文件，以及模块70/80的2个CMake文件。

以下16个兼容target经仓库引用审计仅剩定义或转发链，已移除；未发现新增使用方需要保留：

| removed_target | link_instead |
|---|---|
| pcv_io、pcv_filtering、pcv_output、pcv_m05_camerainput、pcv_m10_pointcloudread、pcv_m80_plane_output、pcv_m80_planeoutput | pcv_m10_pointcloud_io |
| pcv_m20_pointcloudrender | pcv_m20_pointcloud_render |
| pcv_m30_pointcloudstitch | pcv_m30_pointcloud_stitch |
| pcv_m40_pointcloudregistration | pcv_m40_pointcloud_matching，或按实际使用直接链接30/60 |
| pcv_m50_coordinateconversion | pcv_m50_coordinate_conversion |
| pcv_m60_plane_fitting、pcv_m60_planefitting、pcv_m70_roi_template | pcv_m60_geometry_workpiece |
| pcv_registration | 按使用链接30/50/60 |
| pcv_interface | 按使用链接30/60 |

诊断工具仍引用的`pcv_m10_pointcloud_read`与`pcv_m40_pointcloud_matching`保留。公共兼容头全部保留，包括模块70/80，后者不再有target。外部CMake调用方若使用已删除名称须按表迁移；本轮不迁移公共include或改变namespace。

根CMake移除旧独立拼接构建开关、无实际用途的共享栈变量和独立亮度注册；当前脚本与使用说明同步。根CLI/Tools/Tests默认OFF；Debug preset保持Tests ON、Tools改OFF。两个GUI分别受其开关控制。20按GUI创建，30按GUI/Tools/Tests创建，40只在30存在时创建；测试配置包含所需30。基础模块始终查找Qt Core/Gui，需要30/CLI的配置查找Concurrent，Widgets/OpenGL仅随GUI。`-CliOnly`显式关闭两个GUI。

## 工作区保护与静态检查

实施前的status、diff名称清单、已有修改/未跟踪文件SHA-256及删除清单位于源码外：`C:/Users/HP/AppData/Local/Temp/pcv-application-target-cleanup-v1-20261105-104316/`。这是差异清单，不是完整备份。未覆盖既有RAW采集、契约适配、配色或输入配置实现；在原已修改的CMake/脚本/文档上仅追加本任务改动。

不递归删除应用目录：`apps/pointcloudstitch/build`、`.qtcreator`等Git忽略的本机文件保留，因此磁盘上仍可能看到旧同名目录。根build、Qt Creator配置、外部构建/EXE/数据均未清理。旧构建缓存可能仍显示旧程序，需在全新目录重新配置。

本轮仅静态审阅、有效源码/构建引用搜索、CMake源文件路径核查、preset JSON/PowerShell语法解析、当前文档本地链接及`git diff --check`；不执行CMake配置、build、CTest、GUI或设备检查。静态检查不能证明所有配置均能编译或运行。

## 用户构建命令与验收矩阵

在MSVC x64开发环境中指定Qt前缀，为每个组合选未存在的源码外构建目录。以下为待用户执行的命令模板，本轮未运行：

```powershell
$pcvSource = 'D:/workpiece/pointcloudview/pointcloudview'
$pcvQtPrefix = 'C:/Qt/6.8.3/msvc2022_64'
$pcvBuild = 'D:/pcv-build/application-target-cleanup-v1-dual-debug'
$env:VSLANG = '1033'
cmake -S $pcvSource -B $pcvBuild -G 'NMake Makefiles' "-DCMAKE_PREFIX_PATH=$pcvQtPrefix" -DCMAKE_BUILD_TYPE=Debug -DPCV_BUILD_POINTCLOUDVIEW=ON -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON -DPCV_BUILD_CLI=OFF -DPCV_BUILD_TOOLS=OFF -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build $pcvBuild
ctest --test-dir $pcvBuild --output-on-failure
```

按表替换四个入口/工具开关与构建目录；每行覆盖Tests OFF/ON，Debug/Release，默认相机OFF先验证不依赖设备的组合。Release需新目录并替换`CMAKE_BUILD_TYPE=Release`。SDK/相机另做双GUI及旧工作台ON组合，确认SDK实际发现状态，不把OFF通过视为SDK通过。

| combination | POINTCLOUDVIEW | SCAN_GEOMETRY_DEMO_V1 | CLI | TOOLS |
|---|---|---|---|---|
| 两个主GUI | ON | ON | OFF | OFF |
| 仅新GUI | OFF | ON | OFF | OFF |
| 仅旧工作台 | ON | OFF | OFF | OFF |
| CLI-only | OFF | OFF | ON | OFF |
| Tools-only | OFF | OFF | OFF | ON |

可用`scripts/build_windows.ps1 -CliOnly -BuildTests -Config Debug -QtDir <QtPrefix> -BuildDir <NewBuildDir>`验证CLI脚本入口。定向CTest可加`-R 'scan_geometry_.*v2|scan_geometry_contract_tests_v1|selected_plane_dialog_tests_v1|stitching_.*|intensity_image_tests|cli_'`；完整验收仍需各组合全量CTest，不固定测试数量。

人工验收：Qt Creator默认只见两个主GUI运行入口；旧独立程序不再构建/安装/部署。新GUI真实数据加载→选面/匹配→拼接→修剪与配色切换；旧工作台整帧拼接成功发布、取消/失败保持旧会话，相机/RAW入口保留；CLI与工具能按开关启用；CLI-only不链接Widgets/OpenGL。真机、输出质量、DPI和性能仍单独验收。

风险：外部依赖无法仅凭仓库引用归零证明不存在；CMake组合与实际GUI尚未运行。回滚仅恢复本条删除文件及target/注册/文档增量，保留其他工作区修改；不使用破坏性Git回退，不删除已有构建和正式产物。
