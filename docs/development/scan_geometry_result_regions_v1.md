# 最终工件模板蓝色、干涉面红色显示 v1

日期：2026-11-05。Owner：Codex（实现/静态检查）、用户（构建/CTest/原生GUI/DPI/性能）。本轮无新EXE，无运行通过或性能结论。

## 当前行为

新GUI拼接结果、修剪、撤销恢复和保存后使用灰色实测点 `#59636f`，隐藏高度色条；工件模板蓝色 `#1675e8`、已确认干涉面红色 `#f04452`，25%填充、不透明闭合边界。点不染色，不使用marker或修剪点高亮。原始帧仍按工件候选状态使用高度色/灰色。浅灰背景、完整数据和原拾取深度规则保持。

颜色仅供GUI显示；未修改输出schema、PLY/PCVMAP或0/128/255灰度掩码，不增加模板发布或彩色导出。

## 区域生成与接口

应用新增 `result_regions_v1.h/.cpp`，定义ResultRegionBasisV1、ResultInterferenceFrameV1、ResultRegionsRequestV1/ResultV1。完整拼接实测点通过60的buildPlaneMaskV1生成统一工件区域，使用已确认参考平面经过该参考帧累计修正后的法向/中心；仅显示面片投影，原点保持实测坐标。采样间距及投影网格原点从完整结果求取并缓存，本轮结果的修剪/撤销/恢复固定使用；全部擦除返回空蓝色区域。

60的PlaneMaskResultV1追加有效双精度spacingUMm/spacingVMm及gridOriginU/gridOriginV；PlaneMaskOptionsV1将间距存储改为double，追加默认关闭fixedGridOrigin及原点。既有调用方继续自动原点/原采样计算；这是公共DTO/ABI变化，需要完整重编，不改变拟合算法或正式输出契约。固定原点避免删除最边缘点后网格重新锚定，使区域跳动。

每个有效拼接帧占用一个干涉输入槽，包括无标注帧；按拼接有效帧顺序与corrections一一对应。仅加入已确认interferenceMask；pending和高侧提醒不参与。红色顶点和边界均应用同一累计修正，保留真实高度，跳过帧不进入槽。合并后仍保留各帧红色区域，不做去重或压平。坏标注报告帧号，合法帧仍可显示；内存/取消不提交部分新网格。

应用Session维护ResultRegionSnapshotV1，包含蓝/红网格、basis、Empty/Building/Ready/Failed/Cancelled状态、错误和result/trim/annotation revision。共享任务枚举尾部追加BuildResultRegions，不改既有kind数值；新GUI以私有Task派生包装携带区域，领域/共享任务不依赖apps。

## 发布与生命周期

- 初次区域在拼接worker事务成功后生成，再一起发布点云与区域。stitch_transaction_ms独立计时；区域失败不改拼接成功，明确显示“拼接成功，区域显示失败”，可重建。取消发生于事务已提交而区域仍运行时，保留本次完整结果与落盘路径，标明区域取消；关闭仍按既有取消/join处理。
- 修剪立即清空失效蓝色区域，保留有效红色区域并后台重建；任务期间可旋转缩放，禁用继续修剪、Ctrl+Z、恢复和几何保存。重建失败/取消保留修剪和点云，显示“重建区域”。区域失败不作为几何数据有效性的新增门禁，原数据确认规则继续适用。
- 确认替换或清除干涉标注推进annotation revision，清空旧红色汇总后重建；工件修剪不删除干涉面。重新拼接、工件重选/来源失效/重置清理区域。数据接收核对input/reference revision、token及result/trim/annotation revision，过期结果不得发布。
- 画布结果视图与editable状态分离：后台重建和保存期间仍显示点云，单纯区域更新不改变展示点/映射identity，不重传点缓冲。灰色模式沿用现有接口，25%和深度测试不变。
- 使用现有512MiB预算生成网格；临时占用可能超预算而失败，不降低精度。新增GPU缓冲范围发布前检查。该预算不等于进程内存/GPU硬上限，永久面片与合并副本仍增加内存。

日志result_regions包含result/trim/annotation revision、状态、错误及performance。新增regions_interference_ms/full_basis_ms/retained_ms/total_ms、完整/剩余点数、蓝色顶点/三角形和红色顶点数；沿用mask细项。worker_ms包含事务及区域，不能视为单纯拼接时间；包含/并行阶段不可相加。

## 测试源码与用户命令

新增result_regions_tests_v1覆盖完整凹形/孔洞/孤立区域、非单位红色修正及边界、高度保留、修剪/全部擦除、修正后参考平面、固定间距/原点、低预算和取消无部分网格。补session测试：操作门控、迟到三种revision、取消重试、失败保留结果及事务已提交后取消；补GUI测试：蓝红25%、灰点无色条/marker、区域重建不改点revision、原始帧状态回归；已有修剪测试改为等待后台重建。

以下命令由用户执行；在MSVC x64+Qt6.8.3+NMake环境使用全新源码外目录。将Qt路径替换为实际Kit路径。

```powershell
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/result-regions-v1-debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/result-regions-v1-debug
ctest --test-dir D:/pcv-build/result-regions-v1-debug --output-on-failure
cmake -S D:/workpiece/pointcloudview/pointcloudview -B D:/pcv-build/result-regions-v1-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_TESTS=ON -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF
cmake --build D:/pcv-build/result-regions-v1-release
ctest --test-dir D:/pcv-build/result-regions-v1-release --output-on-failure
ctest --test-dir D:/pcv-build/result-regions-v1-release -R "result_regions_tests_v1|plane_mask_tests_v1|scan_geometry_(session|view|contract)_tests_v[12]" --output-on-failure
```

在Qt Creator打开根项目，Designer编辑resultRegionStatus/rebuildResultRegions，运行新GUI。至少用两帧以及含跳过帧输入：标注干涉面→拟合审核→精配准拼接→检查灰点蓝红→擦除/撤销/恢复→保存→回原始帧修改/清除标注→看结果→重选工件失效。检查红色真实高度、非单位修正位置、孔洞/凹边界、全部擦除、100%/150%DPI、可见框选不穿透；区域生成期间能浏览且不能继续编辑/保存。对照正式文件哈希/点数/来源映射，颜色不改变磁盘数据。Release大样本记录区域阶段耗时和峰值内存，不承诺速度。

## 验证、风险与回退

本轮只静态检查，未构建/CTest/GUI/性能测量。区域生成增加CPU/内存与初次结果等待；红色多帧重叠可能叠色。蓝色是统一参考平面的覆盖，灰色点保留真实帧间几何差异，不能以区域平整外观判断拼接质量。

本轮实际静态结果：11份相关C++头/实现/测试词法括号检查通过（非编译）；Designer XML解析通过、新控件存在且widget名称无重复；CMake新源码及CTest注册存在；12份涉及文档的相对文件链接均存在；git diff --check通过。实施后删除集合与实施前一致，仍为32项。新增4份文件：应用区域头/实现、区域测试源码和本说明，均未提交。

保留工作区既有修改及32项历史删除，不提交/推送/上传/清理缓存。实施前差异保存于源码外临时目录 `C:/Users/HP/AppData/Local/Temp/pcv-result-regions-v1-20261105-144318/`。回退仅撤本功能的应用区域任务/界面/测试和60掩码显示元数据增量，完整重编；不回退既有v3/RAW/契约/应用清理。
