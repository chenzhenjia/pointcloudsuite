# STEP 与 CAD 模板点云 v1

日期：2026-10-23；owner：Codex。独立 CAD 设计基准，不是实测 robot_base 模板，不代表匹配、定位或几何质量验收。

## 使用流程

启用 STEP 的 pointcloudview：工具 → 从 STEP 生成模板点云 → 点击/面列表多选 → 设置目标间距 → 生成并替换当前点云 → 创建工件模板。只读取 `.step/.stp` 的有效闭合单实体，拒绝装配（包括单零件装配）、多实体、开放壳体、额外曲面和坏几何。不自动居中、旋转、修复、模拟遮挡或补边界点。

独立窗口初始不选面；点击切换整面，列表/高亮同步，全选/清空；左拖旋转、右拖平移、滚轮缩放。显示原单位、转换比例、包围盒、面编号/类型/面积及预计点数。支持明确的 mm/m/cm/inch/foot 单位，全部转换为 mm；未知或混合单位拒绝。

每面目标点数 `max(1, ceil(area_mm2 / spacing_mm²))`，默认间距 0.5 mm，总上限 10,000,000，超限要求增大间距。面积加权低差异序列辅助三角网格分配，参数点回到原始裁剪曲面求值；不保证严格点间等距。按 face ID 和确定性面内顺序输出，锐边两侧不合并法向。法向为材料外侧单位法向；奇异候选不输出，无法完成目标数量则整次失败。精确曲面点经裁剪域检查后，验证 float32 点到该精确点距离 ≤0.01 mm，作为到所属裁剪面的保守误差上界。不能满足时失败，不悄悄改变坐标或容差。

生成不写正式文件；只有成功且 revision 有效才替换当前和原始缓冲，清旧平面/ROI/WObj1/纹理/轨迹。失败、取消和迟到结果保留当前数据。CAD 来源允许显示/去噪/恢复/保存，禁止机器人平面/WObj1/轨迹/工序流程；去噪使用现有保留原始代表点策略，不移动点或平均法向。恢复还原原始 CAD 来源。

## 模块/API

60：`pcv/workpiece/cad_template_v1.h`，`pcv::workpiece::readStepModel/generateCadTemplateCloud/saveCadTemplate/loadCadTemplate/loadAnyWorkpieceTemplate`。OCCT 类型仅在私有实现，模型句柄对外不可变。20：`pcv::render::CadMeshCanvasV1` 接收 core `CadMeshV1`，不依赖60或OCCT。应用负责窗口、线程、互斥、revision和发布。保持60→10/50、30不依赖60。

STEP 读取的同一字节快照参与 SHA-256 和流式解析，兼容中文路径，不改原文件。后台任务只读共享 Qt 容器，GUI/OpenGL只在主线程；OCCT翻译器串行保护，取消在阶段/采样检查点生效，内核单次调用可能延迟。关闭取消并join，commit前取消回滚，commit后报告保存成功。

## 文件契约

schema_version=`pcv-workpiece-cad-template-v1`，kind=`workpiece_cad_template_3d`，coordinate_frame=`cad_model`，units=`mm`。目录 `<名称>_cad_v1_<UTC时间戳>`，重名追加序号，不覆盖。默认运行数据目录/workpiece_templates，禁止源码树内输出。

`workpiece_template_cad_model.ply`：Binary Little Endian、float32 x/y/z/nx/ny/nz；`workpiece_cad_template_info.json`：名称/版本/时间/坐标/点数/包围盒/相对PLY路径/source。`source.cad` 包含 original_step、sha256、original_units、unit_scale_to_mm、occt_version、algorithm_version=cad_surface_area_v1、selected_faces（id/surface_type/area_mm2）、spacing_mm、geometry_tolerance_mm=0.01、edge_sampling=false、generated_point_count。后续过滤写 source.processing_steps，generated_point_count保留原生成数量，不冒充当前点数。

包内不复制STEP，不保存逐点face映射。face ID只对该文件内容/解析版本有效。PLY+JSON搬迁后可独立打开；原STEP缺失不影响回读，但无法重新选面或重新证明曲面误差。读取检查路径/链接边界、固定PLY布局/长度、点数、有限数据、单位法向、来源和包围盒。沿用稳定PCV错误族。原实测模板v1和其他JSON契约不变。

## 构建

`PCV_BUILD_STEP_IMPORT` 默认ON；关闭时仅隐藏导入入口，CAD模板包读写仍可用。ON时严格需要OCCT7.9.3动态库、MSVC x64匹配运行库；不调用FreeCAD。依赖放源码树外，使用vcpkg `opencascade:x64-windows@7.9.3` 对应port；固定port或使用已安装7.9.3的OpenCASCADE_DIR，不随意升级。

本轮依赖基线：vcpkg commit `56bb2411609227288b70117ead2c47585ba07713`，`opencascade[core,freetype]:x64-windows@7.9.3`，依赖目录 `D:/Dev/vcpkg/installed/x64-windows`。安装时使用 `VCPKG_FORCE_SYSTEM_BINARIES=1` 及已有 CMake/Ninja。项目通过 CMake `EXACT` 拒绝其他 OCCT 版本；运行库复制按此 vcpkg 配置布局，从匹配 Debug/Release 的 TKernel 所在 bin 复制 DLL，并携带 OCCT LGPL/exception 及附带依赖许可证。

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -StepImport -OcctDir D:\Dev\vcpkg\installed\x64-windows\share\opencascade -BuildDir D:\pcv-build\step-cad-v1-on-debug-20261023 -QtDir C:\Qt\6.8.3\msvc2022_64 -Generator "NMake Makefiles"
ctest --test-dir D:\pcv-build\step-cad-v1-on-debug-20261023 -C Debug --output-on-failure
```

Release使用独立目录及 `-Config Release`；OFF版省略-StepImport/-OcctDir。脚本相机默认OFF。OCCT运行库及许可证随STEP版构建产物部署；不修改本机FreeCAD。

## 验证与风险

实际命令、结果、性能及回滚点统一追加 [changelog](../changelog/v0.5.md#step-cad-template-v1)，未执行项不算通过。自动测试覆盖合成几何、单位、非法输入、确定性、点数预算、曲面法向、模板往返/取消，以及画布拾取/高亮和对话框生命周期。真实工件、真实选面交互及视觉体验按用户安排后续验收，无真实STEP样例。大坐标float32限制、复杂模型导入兼容性、内核取消延迟及千万点内存仍是实测风险；没有物理精度承诺。
