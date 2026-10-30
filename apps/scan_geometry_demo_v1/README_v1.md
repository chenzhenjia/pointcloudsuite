# 扫描与几何选择 · 离线演示 v1

> 历史说明：2026-10-26 当前同名 target 的源码已升级真实离线 PLY 所选平面拼接 v2，见 [README_v2.md](README_v2.md)。本文及下列 EXE 路径仅记录原模拟版，不作为 v2 运行或验收证据。

独立 Qt Widgets 程序，target `scan_geometry_demo_v1`。扫描、平面标识、干涉面检查及处理均为确定性演示，不连接相机或机器人，不执行真实配准。关闭后不保留会话。

## 直接运行

本机 Release 程序：

`D:\pcv-build\scan-geometry-demo-v1-20261026\Release\scan_geometry_demo_v1.exe`

Debug 程序：

`D:\pcv-build\scan-geometry-demo-v1-20261026\Debug\scan_geometry_demo_v1.exe`

Qt 运行库由 `windeployqt` 复制到构建目录。移动程序时需保留同目录 DLL 及 `platforms`、`imageformats` 等插件目录；单独复制 EXE 不构成便携部署。干净机器部署未验收。

## Qt Creator 编辑与运行

1. 打开仓库根 `CMakeLists.txt`，选择 Qt 6.8.3 / MSVC x64 Kit。
2. 在源码树外设置新的构建目录；使用 `NMake Makefiles`，不要选择 JOM。
3. 设置 `PCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON`。只体验演示时建议 `PCV_BUILD_EXPERIMENTAL_CAMERA=OFF`，避免实验相机 SDK 依赖。
4. 在项目树打开 `apps/scan_geometry_demo_v1/scan_geometry_window_v1.ui`，切换 Design。主体控件与布局可直接编辑，`canvasHost` 是运行时挂载三维画布的普通容器，无需 Designer 自定义插件。
5. 重新运行 CMake，在运行配置选择 `scan_geometry_demo_v1`，构建后运行。

`.ui` 定义静态布局，`resources/scan_geometry_v1.qss` 定义运行时样式，因此 Designer 中的默认控件样式与运行窗口不同。动态候选卡和擦除确认按钮由画布适配层创建。Designer 已实际完成标题临时编辑及撤销；根项目选 target、构建和运行的人工验收由用户执行。

## 操作顺序

1. 勾选工件位姿与环境检查，点击“开始演示扫描”；450 ms 增加 20%，可暂停、继续、停止。
2. 扫描完成后切换六段点云。点击左侧蓝色工具，再点击较大的点阵选择工件面 A；点击红色工具并点击窄点阵，可选择干涉面 B。
3. 点击“开始处理点云”。未提前选 B 时先审核 B；C 仅在点云 4 出现。逐项选择“加入干涉面”或“忽略此区域”，才能继续。候选卡标题可拖动。
4. 处理完成自动进入“拼接结果”。蓝色为工件点，黄色为已纳入干涉面，灰色为未选原始点；审核中的候选为橙色。
5. 选择矩形擦除工具，在画布拖框；红色高亮仅表示预览。点击“擦除框内点云”后才删除。工具只命中深度可见的点，不穿透遮挡。
6. 使用撤销或 `Ctrl+Z` 撤销修剪；“恢复全部”也可撤销。确认几何后再次修剪、撤销、恢复或重选平面会使确认失效。

浏览模式左键旋转；选面模式左键拾取；矩形模式左键拖框。右键平移，滚轮缩放。`Esc` 取消当前工具与选区。“顶视图”/“适配视图”及右上方向控件复用模块 20。

停止不会自动完成；模拟急停立即清除任务并锁定开始，复位后须重新勾选检查。重新扫描会先确认失效范围。Wobj X/Y/Z/A/B/C 一律显示“待获取”。

## 用户接手验证

用户于 2026-10-26 明确指定后续验证自行执行；已停止后续自动验证和 Computer Use。已发生的结果、测试入口、待验收清单与风险见 `docs/development/scan_geometry_demo_v1.md`；视觉对照状态见本目录 `design-qa.md`。不把已有自动测试结果当作最终人工验收。
