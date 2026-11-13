# 独立 STEP 数模模板制作程序 v1

日期：2026-11-06。Owner：Codex（实现与静态检查），用户（Windows 构建、CTest、GUI、DPI、真实 STEP 验收）。

`apps/step_template_builder_v1` 是独立 Qt Widgets 应用，界面主体由 `step_template_builder_window_v1.ui` 提供，Qt Creator 可直接打开编辑。流程只有三步：工件信息、导入数模、创建模板草案。HTML 只作为浅色背景、蓝色操作、三步导航和检查栏的视觉参考；点云交互采用 `scan_geometry_demo_v1` 的画布、角色颜色、矩形工具、撤销/恢复方式，程序不运行网页，也不复制网页模拟数据。

## 当前实现

- `StepTemplateBuilderSessionV1` 持有模型、完整采样点、不可变来源下标、角色集合、修剪历史和模板确认状态。导入后台读取所有 CAD 面，默认间距 0.5 mm，完整点云上限 10,000,000；展示最多 1,000,000 个确定性抽样点。
- 曲面可显示但不能选为工件面或干涉面；平面角色互斥。工件点蓝色、干涉点红色、其他点灰色。
- 矩形修剪把当前视图快照交给后台，对完整点集做沿视线贯穿投影筛选，显示擦除确认后才写入修剪集合。Esc 清除框选，撤销逐次移除最近批次，恢复全部清空修剪。展示抽样从完整保留点重新生成。
- 处理使用模块 60 的 `generateStepTemplatePackageFromSampledV1`，不重新采样 STEP；修剪区域固定原点重建平面掩码，保留孔洞和边界信息。几何处理成功后还需用户确认，模型、间距、选面、修剪或工件信息变化均使确认失效。
- 保存继续使用 `pcv-workpiece-cad-template-v1`、`cad_model/mm`、既有 PLY 和 `ArtifactTransaction`；工件编号由会话 UUID 生成，材料/草案状态写入 `source.workpiece`，实际版本来自保存结果。输出目录必须在源码树外。
- `PCV_BUILD_STEP_IMPORT` 默认 ON；关闭时页面保留但显示能力缺失，不能进入导入流程。

## 代码边界

新应用只依赖模块 20、60 和 Qt；20 提供画布和投影 DTO，60 提供 STEP 读取、CAD 采样、平面掩码、模板特征和事务保存。应用不复制坐标数学或 CAD 算法。旧 demo1 的 STEP/自动匹配窗口构建注册、入口和测试注册已移除（旧源码保留但不编译），40/50/60 的底层模板及匹配能力保留，方便后续接入。

单个会话最多一个后台任务，处理特征使用共享 512 MiB 可选工作存储预算及模块内部最多两个 worker。预算不代表进程内存上限；OCCT 采样、点云副本和 OpenGL 缓冲仍需单独观察。

## 静态验收

本轮只执行 XML / Qt6.8.3 uic 解析、116个Designer对象唯一性/控件引用、7个QRC路径、CMake/测试注册、旧入口引用清理、182个文档本地链接、`git diff --check` 和工作区检查；本轮已完成 Debug/Release 配置与编译；Debug CTest 为 27/28，通过新 STEP 相关测试；真实轴承 STEP 被既有闭合单实体契约拒绝，未完成 OpenGL/DPI 视觉验收。用户应在源码树外 Debug/Release 目录构建。

## 构建命令

在 MSVC x64 Developer PowerShell 中执行，替换本机 Qt/OCCT 路径；使用全新的源码树外目录。以下命令本轮未执行。

```powershell
cmake -S . -B C:\pcv-build\step-template-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" -DOpenCASCADE_DIR="<OCCT-7.9.3-config>" -DPCV_BUILD_POINTCLOUDVIEW=OFF -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=OFF -DPCV_BUILD_STEP_TEMPLATE_BUILDER_V1=ON -DPCV_BUILD_STEP_IMPORT=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF -DPCV_BUILD_TESTS=ON
cmake --build C:\pcv-build\step-template-debug --parallel 2
ctest --test-dir C:\pcv-build\step-template-debug -C Debug --output-on-failure
& C:\pcv-build\step-template-debug\apps\step_template_builder_v1\step_template_builder_v1.exe
```

Release 使用新目录并把 `CMAKE_BUILD_TYPE`/`-C` 改成 Release。STEP OFF 仅用于兼容验证：另建目录显式指定 `PCV_BUILD_STEP_IMPORT=OFF`，无需 OCCT；界面仍可打开但流程禁用。只构建应用可关闭 `PCV_BUILD_TESTS`，并使用 `--target step_template_builder_v1`。Tests 开启时会创建30/40后端供既有测试使用；应用本身不链接30/40。独立配置还需实际验收，不代表已验证支持。

脚本 `-CliOnly` 现在也显式关闭新增 GUI，避免默认 ON 引入 Widgets。

## 用户验收清单

| 场景 | 预期与记录 |
|---|---|
| Designer / 构建组合 | 三页可编辑；独立应用 STEP ON/OFF，两个旧 GUI 关闭可配置；Debug/Release 全量重编和 CTest分别记录 |
| 身份 / 草案 | 八个预设名、名称必填、材料默认铸铁、会话编号唯一；像素不适用；没有位置/姿态/离地/探针/WObj数据 |
| STEP 读取 | 中文路径、毫米/英寸换算、非法STEP、装配/开放实体拒绝；完整面含曲面可见；点数超限要求增大间距，不自动降低密度 |
| 面角色 | 多个平面互斥，重复点击取消角色；曲面提示拒绝；完整索引拾取与面列表一致 |
| 修剪 | 前后深度重叠点同时框选；非展示点也删除；Esc取消、逐次撤销、恢复全部、全部工件擦除后不能确认保存 |
| 模板一致性 | 修剪后角色点范围连续，PLY点数、法向、摘要、区域网格与闭合外环/孔洞一致；原始采样保持不变 |
| 生命周期 | 导入/处理/修剪取消，关闭等待、过期任务丢弃、异常恢复；参数/角色/修剪变动清旧确认 |
| 保存 | 源码树内拒绝、目录失败可重试；每次新版本不覆盖；角色JSON/干涉PLY同事务；提交后取消仍报告实际保存路径 |
| 视觉 | 与HTML同状态对照导航、卡片、检查栏、底部操作；Windows 100%/150% DPI、最小窗口、中文字体/OpenGL实测 |
| 性能 | 10M完整点/1M显示点的采样、投影、修剪和保存耗时/峰值内存；512MiB预算失败提示，不能声称进程上限 |

`step_template_builder_session_tests_v1` 覆盖完整投影（含未显示点）、互斥、撤销/恢复/全擦、过期、异常、取消和关闭join；`step_template_builder_contract_tests_v1` 检查三页及门控；`step_sampled_template_tests_v1` 检查修剪范围/区域/摘要、输入共享副本不变、事务新版本与回读。模块20原契约测试追加投影坐标/前后点检查。以上测试只交付源码，尚未运行。旧窗口专属测试停用，后端 CAD/匹配测试保留。

## 风险与回滚

完整采样和 through-selection 可能增加等待与内存；掩码预算或稀疏边界可能明确失败；采样密度影响修剪边界；Qt 字体和 HTML 浏览器渲染存在差异；当前未进行 OCCT/Windows 编译验证。回滚本轮可移除 `apps/step_template_builder_v1`、新增 20/60 API 与根开关，同时保留原有模块 60 模板/匹配接口；不恢复已授权清理的旧入口。

已知实现限制：既有掩码为每个面按点数×256申请预算，512MiB下约超过209万点的单面修剪可能明确失败，需增大采样间距后重导；未修改既有掩码预算规则。OCCT读取/三角化与既有特征统计存在取消响应延迟，关闭会等待实际任务退出；真实延迟由用户验收。显示颜色按显示revision/角色集合缓存，纯状态变化不重复上传百万点颜色。
