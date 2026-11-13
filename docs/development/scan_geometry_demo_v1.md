# 独立扫描与几何选择演示 GUI v1

## 2026-10-30 SDK深度缓存与按需物化

输入配置新增SDK会话帧目录；仍沿用逐帧扫描JSON、共用手眼XML与现有PLY兼容路径。深度轮次载入调用模块10 `readCapturedFrame(...PreviewOnly)`，保留原始C16/辅助Mono8并在深度画布预览；明确点击“开始当前帧处理”后才调用 `materializeCameraFrame()`，保留SDK原序映射，再由模块50 `FullXyz` 栅格行进度按 `row/(height-1)` 变换至 `robot_base` 并建完整选面索引。缺失亮度/转换不兼容/校验错误时保留源深度并拒绝物化。

逐帧确认或批量确认后释放深度帧活动XYZ、索引、展示子集和预算租约，保留源深度、来源指纹及已确认完整平面快照。再次处理和自动匹配从已保留的源帧重建；轮次浏览仅显示原始深度。PLY路径行为不变。深度SDK转换当前在模块10调用完成后仍需模块50生成robot_base完整点集，因此处理时单帧峰值仍包含XYZ、转换点集、选面索引和显示数据；多帧常驻收益及分配器工作集变化尚未实测。

验证：2026-10-30，Qt 6.8.3/MSVC 19.51/NMake，源码外 Debug `D:\pcv-build\scan-geometry-depth-cache-v1-20261030-debug` 与 Release `D:\pcv-build\scan-geometry-depth-cache-v1-20261030-release` 全目标构建均成功，`PCV_BUILD_EXPERIMENTAL_CAMERA=OFF`。Debug、Release CTest 均 34/36；`handeye_transform_tests` 与 `scan_geometry_session_tests_v2` 通过。两个未通过目标为 `plane_mask_tests_v1`（测试退出码8，各向异性采样断言）和 `scan_geometry_view_tests_v2`（测试进程失败且无输出诊断）；尚未证明它们与本次功能有关或无关。Debug/Release 两个 EXE 均通过 `PCV_SCAN_GEOMETRY_SELFTEST_CLOSE=1` 原生启动/关闭冒烟测试，但这不是深度帧交互检查。已编译无SDK配置，但未实测真实深度会话浏览/转换；真实相机、SDK转换精度、Unicode样例、逐阶段耗时、Working Set/Private Bytes及原生交互仍待验收。Release 部署时 windeployqt 报本机缺少 Visual Studio Release redistributable；本轮不验收便携部署。

> 历史记录：本页下方保留 2026-10-26 的模拟 UI 记录；当前真实离线 PLY/扫描 JSON、所选平面拼接和几何结果行为见下方 2026-10-29 章节及 [真实流程说明](scan_geometry_real_selected_plane_v2.md)。历史截图不作为当前验收证据。

日期：2026-10-26。owner：主线 / Codex 实施，用户接手后续验证。软件版本、数据 schema 均不变。对应 [target](../../target.md)、[集中日志](../changelog/v0.5.md#scan-geometry-demo-v1)、[应用运行说明](../../apps/scan_geometry_demo_v1/README_v1.md)、[视觉记录](../../apps/scan_geometry_demo_v1/design-qa.md)。

## 2026-10-29 几何结果接入 v1（当前实现）

本轮正式入口采用离线 `PLY + 手眼 XML + Start/End 位姿`，并通过共享任务 `parseSelectedPlaneScanJsonV2` 接受现有扫描 JSON。JSON 帧转换为同一 `InputFrameV2`，读取、模块 50 坐标转换、六分屏展示抽样、完整索引、选面、参考面匹配和模块 30 所选平面拼接共用同一路径；本期不接相机 SDK、机器人控制、IK、碰撞检测或质量报告。

模块 60 提供 `pcv::roi::buildWorkpieceCoordinateSystemFromRobotAxes`。`ScanGeometrySessionV2::generateWobj1()` 使用完整参考候选点集计算包围盒中心并投影到确认平面，再将 `robot_base` X/Y 投影到平面建立右手系，显示原点、三轴、`pose_xyzabc` 和校验状态。重选、重载、跳过、干涉标注或修剪都会使 WObj1 与几何确认失效。

`confirmGeometry()` 会先生成 WObj1，再异步保存新的 `geometry_<timestamp>_<uuid>` 目录。目录包含完整拼接 PLY/PCVMAP、修剪 PLY/PCVMAP、manifest、report 和 `selected_plane_geometry_v1.json`；完整拼接目录由模块 30 先独立提交，几何目录由应用随后使用 `ArtifactTransaction` 独立提交，取消或失败不会发布半成品。sidecar 保存 WObj1、参考帧、来源帧/scan JSON、结果 revision、删除的结果索引和每帧完整干涉索引。磁盘完整结果始终保留，修剪结果是派生版本。

本轮 Debug 源码树外构建目录为 `D:\pcv-build\scan-geometry-implementation-v1-20261029-debug-new`，Qt 6.8.3/MSVC/NMake，已完成全目标构建；CTest 首轮 33/34 通过，发现并修复异常恢复和空配置夹具的会话门禁问题，定向会话测试随后通过。尚未完成 Release 构建、真实 2/6/7/13 帧样例和人工 GUI/DPI 验收，不能将这些范围写成已通过。

## 2026-10-30 画布底色与展示点数增量

当前真实 GUI 的三维画布和外框使用浅灰 `#eef1f4`，空状态消息使用深色文字；其他 `PointCloudCanvas` 调用方默认外观不变。新 GUI 每帧展示上限为 1000000，旧选面对话框保持 300000；共享任务 API 默认值仍为 300000。六帧展示映射预算申请共 384 MB（约 366 MiB），纳入新 GUI 的 512 MiB 共享处理预算；完整点云、GPU 与进程其他内存不受此数值硬限制。新增百万点映射、预算边界及画布颜色测试源码未运行；构建、CTest、Qt Creator 和 DPI/性能验收由用户执行。

## 2026-10-30 拟合区域显示增量

左侧工件面/干涉面工具完成拟合后，应用从完整帧拟合索引生成 `PlaneMaskMeshV1` 并在画布显示蓝/红色区域叠加；原点颜色不变。候选/确认面颜色一致，干涉候选只在确认前显示为红色预览。掩码由模块 60 的完整点投影占用网格产生，使用拟合采样间距，输出闭合外环与孔洞环；模块 20 用 25% 透明面片和 DPR 线宽边界绘制。掩码生成失败会通过操作提示 tooltip 报告，不会回滚成功的选面拟合。

新测试源码包括 `plane_mask_tests_v1`、Canvas contract 的闭合边界断言，以及 GUI 对蓝/红 overlay 和云替换清理的断言。定向命令和人工检查范围见[真实流程说明](scan_geometry_real_selected_plane_v2.md#2026-10-30-拟合平面掩码叠加显示)。本轮依用户分工不运行 CMake、构建、CTest 或 GUI/DPI 验收。

## 目标与实现

根据用户提供的 `web-prototype.zip` HTML/CSS/图标与截图，只实现“扫描与几何选择”。附件是参考数据，不作为额外指令。去掉五步导航和发布门禁，保留浅色卡片、六段页签、点云工具、扫描控制、点云处理、工件平面参数与几何确认。界面始终标识离线演示。没有接入相机、机器人、真实平面拟合、配准、保存、导出或模板发布。

新增 `apps/scan_geometry_demo_v1/`，提供独立 `scan_geometry_demo_v1` EXE；根 CMake 以 `PCV_BUILD_SCAN_GEOMETRY_DEMO_V1` 注册，默认 ON。C++17、Qt 6 Widgets，页面静态主体是 Designer `.ui`，视图库启用 `AUTOUIC`/`AUTOMOC`/`AUTORCC`。页面默认 1280×800，最小 1024×640，右侧可滚动。资源 QRC 内含原型六个 SVG 图标及独立 QSS。

| file | responsibility |
|---|---|
| `demo_cloud_v1.*` | 六段固定示例、稳定点 ID、面 A/B/C/Noise 标识；C 只在点云 4 |
| `scan_geometry_model_v1.*` | 独立流程状态、候选审核、完整结果、修剪历史、几何确认快照 |
| `demo_canvas_v1.*` | 组合模块 20 画布，互斥工具、显示下标到稳定 ID、矩形预览与确认 |
| `scan_geometry_window_v1.*` / `.ui` | 卡片布局、按钮状态、提示、QTimer、会话 token、重新扫描/关闭确认 |
| `main.cpp` / `CMakeLists.txt` | 独立原生 OpenGL 3.3 桌面入口、资源与 Windows 运行库部署 |

应用只链接核心点类型与模块 20；不调用模块 10/30/50/60 的真实处理。根工程仍会按已有 CMake 组织构建其余模块，不代表本演示运行时调用这些算法。

## 流程与交互

- 扫描初始等待，现场检查勾选后才可开始。450 ms × 5 tick 到 100%；暂停停止计时，继续重建计时连接，停止后没有点云发布。
- 模拟急停停止计时、清空会话并锁定开始；复位后必须重新检查。重新扫描有失效确认；忙碌关闭有停止确认。
- 六段原始点云在扫描完成后启用，拼接结果在处理完成前禁用。浏览/工件拾取/干涉拾取/矩形擦除互斥；右键平移、滚轮缩放，窗口级 `Esc` 退出工具，`Ctrl+Z` 撤销。
- 点击点按示例面标识选整面，不执行真实面识别。工件与直接选中的干涉面不能相同；重选清理派生状态。A/B 覆盖六段，C 只在第 4 段；将 C 选作工件面会在处理时定位点云 1 缺失并失败。
- 处理以 250 ms 定时器推进 25 个工作单元，显示坐标统一、平面匹配与拟合、提取目标点云、配准拼接、生成结果五阶段。工作单元 12 检查各段并按首次出现顺序逐项审核未选候选；审核期间暂停，不自动忽略。
- 取消保留原始六段数据，不发布半成品。完成后仅将工件面与纳入的干涉面汇合，按面标识及坐标确定性去重；这是示例编排，不是真实配准。
- 六段原始数据总共 1465 点。A+B 去重结果 1178 点，加入 C 后 1203 点；只保留 A 时 930 点。稳定 ID 独立于每次显示下标。
- 擦除只在结果可用。小于 3×3 逻辑像素的误拖取消；其余矩形由既有 GPU 深度拾取返回可见点，先高亮，再点击确认。移动相机、缩放、调整尺寸或切换页面清空待擦除选区。
- 以被删除稳定 ID 集合保存多层撤销。恢复全部也压入历史；撤销/恢复后仍需重新确认。至少三个非共线工件点才允许确认；确认保存当前内存快照及 revision，关闭程序重置。
- 历史模拟版的 Wobj 六字段保持“待获取”；当前真实流程由上方 2026-10-29 章节生成并保存 WObj1，但不提供机器人控制、安全或配准质量结论。

## 模块 20 增量

只修改 `pointcloud_canvas.h/.cpp`：

1. `setAppearance(const QColor &background, const QColor &selection)`：GUI 线程设置实例背景/选中点色；默认仍黑底白选中，普通渲染及两种拾取恢复均使用当前实例背景。
2. `setPointColorOverlay(const pcv::render::PointColorOverlay &overlay)`：复用已有 DTO/验证和颜色缓冲；输入按完整点索引，内部映射至展示下标。非法覆盖返回 false 并保留旧覆盖；显示需切换现有 color mode 3。
3. 点数提示文字根据实例背景亮度选深色/浅色，浅底可读，默认黑底用原颜色。没有改变其他调用方的默认配色和点云算法。

修剪映射与业务颜色属于演示应用，模块 20 不依赖应用，也没有新增穿透选择算法。

## 已发生的验证记录

用户随后要求“验证部分由我来做”。以下只记录此前实际发生的操作；之后未继续运行构建、CTest、截图/拾取或 Qt Creator 自动操作。Debug 末轮正在执行的验证已停止。Computer Use 被物理 Esc 停止后未继续调用。

构建环境：Windows，Qt `C:\Qt\6.8.3\msvc2022_64`，MSVC `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`，CMake/CTest `C:\Qt\Tools\CMake_64\bin`，`NMake Makefiles`，`VSLANG=1033`。源码树外新目录 `D:\pcv-build\scan-geometry-demo-v1-20261026\Debug` / `Release`；Tests/CLI/Tools ON，相机 OFF，STEP 默认 OFF。

| operation | observed_result | evidence |
|---|---|---|
| 首轮 Debug、Release 全量构建 | 成功 | `build-debug.log` / `build-release.log` |
| 首轮 Debug、Release 全量 CTest | 各 47/47 通过 | `ctest-debug.log` / `ctest-release.log` |
| 修正全窗口 Esc、示例位置/比例、浅色提示与状态标签后全量增量构建 | Debug / Release 成功 | `build-debug-final.log` / `build-release-final.log` |
| 末轮 Release CTest | 47/47 通过，17.08 s | `ctest-release-final.log` |
| 末轮 Debug CTest | 前 46 项通过，第 47 项原生 view 测试期间按用户接手要求停止；非完整通过 | `ctest-debug-final.log` |
| Qt Creator Designer | 已打开 `.ui`，临时修改标题并撤销，未保存测试文本 | 原生桌面观察；尚未完成根项目选 target 运行 |
| 首轮原生截图 | 空态/选面/候选/完成/修剪/最小窗口已生成，实际打开完成态及对照 | `evidence-debug` / `evidence-release`、`comparison-v1-before.png` |
| 最终视觉/100%与150%独立验收/完整人工链路 | 用户接手，未完成 | `apps/scan_geometry_demo_v1/design-qa.md` |

表中相对日志均位于 `D:\pcv-build\scan-geometry-demo-v1-20261026\`。首次 shader uniform 声明错误及工具栏伸展错误已修正后进入上述首轮全量测试；首轮截图发现的 A/B 上下位置、文字对比度及状态标签宽度也已改代码，后续视觉复核尚未执行。不能把首轮截图视为最终视觉验收。

停止验证后，仅将新 view 测试的 `QT_QPA_PLATFORM=windows` 等环境追加移到通用 PATH 配置之后，避免被 `set_tests_properties(ENVIRONMENT ...)` 覆盖。此最后 CMake 调整未重配置/重跑，用户验证前须重新运行 CMake。应用代码与已有 EXE 对应的最后构建一致；最后文档更新也未再执行 `git diff --check`。

新增 `scan_geometry_demo_model_tests_v1` 覆盖流程、失败/取消/重试、候选、稳定 ID、撤销恢复及几何退化；`scan_geometry_demo_view_tests_v1` 覆盖原生上下文、布局、真实计时器、鼠标选面/旋转、修剪、全窗口 Esc、遮挡可见拾取、默认外观、非法覆盖保留与映射。测试源码可由用户继续运行，不替代人工验收。

## 用户验证入口

以下是供用户执行的命令，未因写入本文而再次运行：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set VSLANG=1033
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S "D:\workpiece\pointcloudview\pointcloudview" -B "D:\pcv-build\scan-geometry-demo-v1-20261026\Debug" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_TESTS=ON -DPCV_BUILD_CLI=ON -DPCV_BUILD_TOOLS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "D:\pcv-build\scan-geometry-demo-v1-20261026\Debug"
"C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "D:\pcv-build\scan-geometry-demo-v1-20261026\Debug" --output-on-failure
```

Release 将构建目录和 `CMAKE_BUILD_TYPE` 同时改为 `Release`。只运行新功能测试可在 CTest 加 `-R scan_geometry_demo`。原生 GPU 测试需要未锁定的 Windows 桌面和有效显卡驱动。

100%/150% 进程级验证可分别设置 `QT_SCREEN_SCALE_FACTORS=1` 与 `QT_SCALE_FACTOR=1` / `1.5`，不更改系统设置；同时设置 `PCV_SCAN_GEOMETRY_EXPECTED_DPR=1` / `1.5`，让 view 测试记录并检查实际 DPR。`PCV_SCAN_GEOMETRY_EVIDENCE` 指向源码外截图目录。只设置 `QT_SCALE_FACTOR` 会与系统 DPR 相乘，不能据此宣称准确的 100%/150%。这些独立 DPI 运行本轮尚未执行。

人工需继续检查：重新扫描确认、忙碌关闭确认、模拟急停及复位、候选卡拖动/切段、失败及取消重试、框选空白/小误拖/旋转后拾取、遮挡不穿透、多次撤销及恢复可撤销、全部擦除禁止确认、最小窗口与侧栏滚动、键盘焦点和禁用提示、旧工作台黑底默认外观及两个旧 GUI 原生启动。Qt Creator 打开根项目选择新 target 运行，最终参考区域并排截图对照及干净机部署也待用户完成。

## 工作区与风险

任务开始已有 25 个修改文件及 3 个未跟踪文件（含主窗口/模块 20/测试/文档）。基线只读快照位于外部目录的 `baseline.diff`、`baseline-status.txt`、`baseline-files.json`。本次追加应用、新测试及必要 CMake/模块 20/文档增量，没有提交、推送、上传或覆盖原始附件，根目录已有 `design-qa.md` 保留，演示 QA 写入应用目录。

剩余风险：最终人工交互及视觉未验收；GPU/DPI/驱动差异；相机 ON/STEP ON 等额外构建组合、真实硬件/数据、干净机器运行库均未验证。复用模块 20 的兼容性不能仅由默认成员值或自动小夹具测试作最终担保。

回滚只针对本次新增 `apps/scan_geometry_demo_v1`、两个新测试、根/模块/tests CMake 注册及模块 20 这三处兼容增量；对照任务前基线逐块处理，不能整体恢复模块 20、tests 或文档而覆盖已有用户修改。删除/回滚应另行取得明确授权。

## 2026-10-29 画布与操作区精简

- 主画布恢复深色背景和模块 20 的高度彩虹点色，未选点使用稀疏 overlay，确认工件面及干涉标注继续显示业务色；实例外观不会改变旧工作台。
- 右侧统一使用“导入数据”进入 PLY/扫描 JSON 配置；候选逐帧确认即为审核完成，满足至少两帧和异常/冲突门禁后可直接拼接。独立批量确认入口移除。
- 三点选面、干涉标注确认/清除、异常重试与重置移入“更多”；忙碌时取消仍直接可见。几何结果通过“确认并保存几何”一次执行 WObj1 自动生成和事务保存，避免重复保存入口。左侧工具栏、Esc、Ctrl+Z 保持。
- 本轮验证：源码树外全新 Debug、Release 构建成功；两种配置全量 CTest 均为 34/34。原生 GUI/DPI 验收未完成：Computer Use 返回用户按 Escape 停止状态，之后没有继续操作桌面；因此 1024×640、1280×800、100%/150% DPI、长路径和真实扫描样例仍待验收。Release 部署日志提示本机缺少 Visual Studio Release Redistributable，构建和 CTest 通过不代表干净机器部署已验证。

## 2026-10-29 右侧步骤式操作区

- 右栏顶部沿用原 HTML 五步名称“工件信息 / 工件位置及姿态 / 扫描与几何选择 / 探针测量 / 发布模板”；仅“扫描与几何选择”可用，其余步骤禁用并标注未接入当前离线 GUI，不显示发布门禁。
- 在可用步骤内分为“导入数据 / 选面与审核 / 拼接 / 确认并保存”四个子阶段。前进按钮由已加载帧数、`canStitch()` 和正式拼接结果驱动；已到达子阶段可回退查看，后台任务运行时切换被禁用。拼接成功自动转入结果保存阶段。
- 导入与匹配/审核复用现有按钮及会话状态；取消在任务运行时保持可见。“更多”仍收纳低频操作。步骤切换不清空数据、不取消任务；修剪和几何变化仍按 session 现有逻辑使保存确认失效。
- 验证：源码树外 Debug/Release 全量构建成功；全量 CTest 两种配置均 34/34。新增原生视图会话回归实际加载两帧测试 PLY，覆盖导入门控、参考确认、匹配审核、直接拼接、自动进入保存、修剪使确认失效及回看拼接阶段。首次测试发现回看时被重复自动前进，已改为仅在结果 revision 更新时自动进入保存阶段；修复后 Debug/Release 全量 CTest 均通过。启动 Debug GUI 并检查可访问控件树，确认五步名称、仅第三步可用、初始显示导入阶段；未完成窗口尺寸/DPI视觉检查、长路径和生产扫描样例验收。
- Release 构建日志提示本机缺少 Visual Studio Release Redistributable；本机编译与 CTest 已通过，干净机器运行仍未验证。没有更改坐标、点序、输入/输出契约或模块 20 渲染逻辑。
