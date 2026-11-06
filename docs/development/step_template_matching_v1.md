# STEP 选面点云模板与自动匹配 v1

日期：2026-11-05。Owner：Codex（源码与静态检查），用户（构建、测试与真实样本验收）。本轮无编译结果、无新版 EXE、无速度或定位精度结论。

## 入口与操作

`scan_geometry_demo_v1` 标题区新增“STEP 模板与自动匹配…”。独立 Designer 对话框 `apps/scan_geometry_demo_v1/step_template_dialog_v1.ui` 提供“制作 STEP 模板”和“使用模板与审核匹配”两个模式；不替换扫描会话。扫描任务未收敛时模板任务按钮禁用，随后沿用同一512MiB预算，单个模板后台任务、内部最多2计算worker。

1. 开启 STEP 构建后导入 `.step/.stp`。CAD 仍为 `cad_model`、mm，不居中、不翻转；沿用闭合单实体和非装配限制。
2. 使用角色下拉框，点击面或激活列表项目分类。多个工件面蓝色，多个干涉面红色，互斥；再次点击取消当前分类。曲面明确拒绝，灰色为未选面。
3. 设置采样间距（默认0.5mm），生成模板；完整工件和干涉采样总数上限10000000，超限报错并要求增大间距。列表显示面积与预计点数。
4. “另存模板包”选择源码树外根目录，独立版本目录默认不覆盖。可在没有原 STEP 时回读、显示和匹配。重新选面需“从模板重新打开原 STEP”，SHA256必须一致。
5. 在扫描主界面完成真实拼接/修剪，打开模板窗口读取模板JSON、模板目录或已保存几何结果目录。目标仅为当前结果保留的完整实测点；展示抽样最多100万，计算抽样独立。
6. 调整2mm/30%/100点及4096/16搜索限制，自动匹配。列表显示XYZABC、RMS、模板覆盖、有效匹配点数和边界可观测性。灰点不染色，蓝/红模板面片按候选刚体变换显示，填充25%，闭合边界不透明。
7. 切换候选后必须人工确认；只有合格且当前revision的已确认候选能保存。保存独立匹配JSON，不改模板、拼接产物、点坐标，也不下发机器人动作。

## 模块与接口

| owner | 新增或变化 | 责任 |
|---|---|---|
| 20 | `CadMeshCanvasV1::setFaceColors/setAppearance/clearMesh` | 实例级灰蓝红CAD显示；旧调用方默认颜色不变 |
| 50 | `rigid_estimation_v1.h/.cpp` | Horn刚体对应点估计、坐标架/双平面初始变换、组合/角差、6DoF约束秩 |
| 60 | `template_features_v1.h/.cpp` | 完整实测点复用单击选面与区域掩码；CAD精确解析平面和裁剪三角网格边界，保留孔洞 |
| 60 | `template_package_v1.h/.cpp` | 角色分离、旧模板/几何目录回读、点摘要、原事务保存扩展 |
| 40 | `template_match_v1.h/.cpp` | 确定性全局假设、评分、6DoF精化、完整核验、多解；STATIC保留原PUBLIC聚合 |
| app | `step_template_dialog_v1.*` | 参数、后台任务、revision/token、人工确认、源码外事务与JSONL |

模块30拼接、现有手眼链、原模板schema和正式输出格式不变；无新依赖。`PCV_BUILD_STEP_IMPORT=OFF`仍编译模板包和匹配，禁用STEP读取/采样。

## 包与结果

旧CAD/实测模板的PLY和JSON保持原契约。新增同事务 `template_roles_v1.json`（`pcv-template-roles-v1`）和可选 `template_interference_v1.ply`。伴随信息包括坐标/单位、工件/干涉点数与SHA256、逐面角色、点范围或完整索引、解析模型、中心/轴、三角网格和闭合边界。回读核对摘要、点数、模型、索引和闭合边界，拒绝目录逃逸。旧包没有伴随文件时从完整工件点提取平面，无干涉标注。

新GUI几何目录读取 `selected_plane_geometry_v1.json` 与 `selected_plane_trimmed.ply`，核对robot_base/mm和保留点数。旧几何目录只记录源帧干涉下标，未保存可独立恢复的干涉点云及变换面片；此类目录按无干涉区域读取，不借原帧下标编造结果区域。

匹配另存 `template_match_v1_<UTC>_<uuid>/template_match_v1.json`，schema `pcv-template-match-v1`。矩阵为逐行4×4，列向量语义 `p_target=T_target_from_template*p_template`；CAD明确 `T_robot_base_from_cad_model`。包含完整模板/目标点摘要、来源、结果/修剪revision、删除下标、参数、候选、搜索限制和确认时间。原文件保持不变。

## 搜索、预算与限制

完整目标建立确定性排序空间格索引，粗搜索使用最多2048模板点，精化最多20000点；最终覆盖率、匹配点数与RMS扫描完整模板。用平面法向及其反号、面间夹角、双平面偏移、主轴/边界方向、边界极值和孔洞中心产生假设。最近邻精化最多25轮，无拼接修正幅度限制。最终至少100点、覆盖30%，还需边界分布、归一化6DoF约束秩为6和面内双向扰动检查。

可选索引/工作存储先申请共享预算，申请失败明确报错；不降低完整点数或偷偷放宽阈值。预算不等于进程内存/GPU硬上限；六帧缓存已占较多预算时匹配可能失败。大规模JSON、CAD三角化/排序及个别底层调用只能在阶段间取消，关闭等待可能需要时间。

这是有限平面/边界结构搜索，并非完备全局优化。对局部缺失、近圆形边界、对称结构、复杂孔洞/跨度或噪声，可能失败或产生多解。实际成功率、误接受、精度、运行时间和峰值内存均待用户真实样本验收。约束检查是工程门控，不能宣称数学上的全局唯一性。未找到并不证明不存在合法位姿。

## 日志与生命周期

日志位于 `QStandardPaths::AppLocalDataLocation/logs/step_template_match_v1/`，可从窗口打开。记录操作/任务、模板与目标revision、取消/过期、落盘状态、读取/验证/三角化、采样、面片分离、摘要、目标特征、搜索、精化核验及worker总耗时。阶段包含/重叠，不相加冒充总耗时。任务不使用定时进度；搜索显示实际已评估假设数。

模板、参数、目标结果/修剪revision及任务序号门控；失效清空候选与确认。制作任务不因目标变化被废弃。关闭/Esc先取消并join；事务已提交如实报告路径，不能将迟到取消解释为未落盘。

## 用户构建命令（本轮未执行）

从仓库根目录 PowerShell 执行；目录须全新且位于源码树外。公共头及40库类型变化要求全量重编。

```powershell
.\scripts\build_windows.ps1 -QtDir 'C:\Qt\6.8.3\msvc2022_64' -Config Debug -BuildDir 'D:\pcv-build\step-match-v1-debug' -BuildTests -StepImport -OcctDir 'D:\Dev\vcpkg\installed\x64-windows\share\opencascade' -Generator 'NMake Makefiles'
ctest --test-dir 'D:\pcv-build\step-match-v1-debug' --output-on-failure
.\scripts\build_windows.ps1 -QtDir 'C:\Qt\6.8.3\msvc2022_64' -Config Release -BuildDir 'D:\pcv-build\step-match-v1-release' -BuildTests -StepImport -OcctDir 'D:\Dev\vcpkg\installed\x64-windows\share\opencascade' -Generator 'NMake Makefiles'
ctest --test-dir 'D:\pcv-build\step-match-v1-release' --output-on-failure
```

另在MSVC开发终端使用CMake构建新GUI单独/STEP关闭组合：

```powershell
cmake -S . -B 'D:\pcv-build\step-match-v1-newgui-off' -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH='C:/Qt/6.8.3/msvc2022_64' -DPCV_BUILD_POINTCLOUDVIEW=OFF -DPCV_BUILD_SCAN_GEOMETRY_DEMO_V1=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF -DPCV_BUILD_STEP_IMPORT=OFF -DPCV_BUILD_TESTS=ON
cmake --build 'D:\pcv-build\step-match-v1-newgui-off'
ctest --test-dir 'D:\pcv-build\step-match-v1-newgui-off' --output-on-failure
```

ON使用原有OCCT7.9.3精确版本及部署函数，OFF回读CAD模板并匹配。Qt Creator打开根CMake，选择 `scan_geometry_demo_v1`；可直接编辑新对话框 `.ui`，无需Designer插件。

## 测试源码与人工验收

新增 `template_match_tests_v1`（刚体方向、已知任意位姿、法向反号、干涉排除、低点数/无边界拒绝、上限、取消、预算、中文模板往返、坏摘要/索引）与 `step_template_view_tests_v1`（角色互斥、曲面拒绝、参数失效、迟到任务、关闭等待、STEP开关）。扩展现有 `cad_template_tests_v1` 检查解析面、角色包、互斥、总点数限制；原测试继续覆盖单位、中文路径、非法/开放/多实体和旧包。

以上测试源码未运行。额外人工/真实样例必须检查：

- 单面凹边界、孔洞、平行多面/非平行多面；局部遮挡与缺失、噪声、错误模板；不足100点、低覆盖和无边界均不能误报成功。
- 非单位任意XYZABC变换方向；蓝红区域随候选对齐，干涉面不影响分数；对称工件保留多个不同位姿并逐个确认。
- 导入/生成不改变扫描内容；模板重载/重采样/参数变化/修剪/重拼接后旧确认失效；取消/关闭中途无半成品发布；事务提交路径真实。
- 原STEP移走后回读可显示和匹配，重新选面要求同摘要STEP；旧CAD/实测模板和几何目录可读；恶意路径、坏伴随文件拒绝。
- 100%/150%DPI、1024×640、密集点与孔洞边界、25%透明度；候选切换只更新叠加，不重新上传目标点。
- Release同样本记录阶段耗时、CPU/峰值内存及匹配质量；六帧预算紧张场景明确失败且不修改缓存/输入。

## 本轮静态检查与回滚边界

已执行新旧 `.ui` XML结构/对象唯一性检查、新增C++括号扫描；其余交付前静态结果在集中日志记录。未运行配置、编译、CTest、GUI/截图/DPI、OCCT真实样本或性能采样。工作区既有修改和删除保留，不提交/推送。

回滚只移除本轮模板对话框/入口、新增40/50/60文件及对应CMake/测试增量，并恢复40原聚合类型；不得重置整个工作区。旧应用与模块30算法未迁移。

交付静态记录（2026-11-05）：`git diff --check` 无输出；两份UI XML/唯一对象名及新增UI控件引用通过；12份新增C++/头括号扫描通过；公共include路径和16份本任务文档链接通过；原有32项删除保持，未新增删除。`git status --short`已复核，既有和本轮未提交/未跟踪文件均保留。未执行构建或任何测试。
