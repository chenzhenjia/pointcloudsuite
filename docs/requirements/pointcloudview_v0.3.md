# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
最后核对日期：2026-08-31
状态：迁移完成，发布验收待完成

## 1. 目标和事实来源

v0.3 是 PointCloudSuite 当前唯一统筹基线。项目按十个固定编号模块治理；源码、CMake 和测试优先于历史文档。

当前未实现模块为 `90_interferenceplane` 和 `100_qualityreport`。本版本不为这两个模块建立功能 target，也不报告虚假成功。

## 2. 模块基线

| 模块 | 职责 | 规范 target | 状态 |
|---|---|---|---|
| `10_pointcloudread` | PLY 读写、缓存、校验 | `pcv_m10_pointcloudread` | 已实现 |
| `20_pointcloudrender` | Qt/OpenGL 画布、VBO、点选 | `pcv_m20_pointcloudrender` | 已实现，桌面验收待完成 |
| `30_pointcloudstitch` | 拼接流程编排、结果管理 | `pcv_m30_pointcloudstitch` | 已实现 |
| `40_pointcloudregistration` | ICP、结构校验、seam fusion | `pcv_m40_pointcloudregistration` | 已实现 |
| `50_coordinateconversion` | 手眼标定、位姿插值、坐标转换 | `pcv_m50_coordinateconversion` | 已实现 |
| `60_planefitting` | 三点/n 点平面算法和诊断 | `pcv_m60_planefitting` | 已实现 |
| `70_roi_template` | ROI、模板、临时工件接口 | `pcv_m70_roi_template` | 已实现，部分 UI 编排保留 |
| `80_planeoutput` | PNG、PLY、JSON 成套输出 | `pcv_m80_planeoutput` | 已实现 |
| `90_interferenceplane` | 干涉平面检查 | 无 | 未实现 |
| `100_qualityreport` | 质量报告 | 无 | 未实现 |

迁移期兼容 target：`pcv_io`、`pcv_registration`、`pcv_interface`、`pcv_output`。兼容层在所有调用方迁移并完成独立审计前保留。

## 3. 数据流程

单帧流程：输入校验 → `10` 读取/缓存 → `20` 渲染和点选 → `50` 坐标转换 → `60` 平面拟合 → `70` ROI/模板 → `80` 成套输出。

多帧流程：`30` 组织输入和输出，调用 `50` 转换、`40` 配准与 seam fusion，成功后提交正式 PLY。取消或失败时不得发布部分结果或复用旧正式输出。

## 4. 稳定契约

- 长度单位为 mm，角度单位为 deg。
- 位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`。
- 平面输出为 Grayscale8 PNG（背景 0、前景 255）、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- PNG、PLY、JSON 先写入 staging 目录，全部成功后成套提交；任一失败必须回滚。
- 共享库不弹窗、不访问 QWidget；应用负责提示和线程生命周期。
- `PointCloudCanvas` 实现位于 `modules/20_pointcloudrender/src/pointcloud_canvas.cpp`。
- 诊断工具通过 `pcv::interface::stitchRawLineProfiles` 使用统一拼接接口。

详细字段和错误码以模块公共头、模块文档和测试源码为准。

## 5. 开发和验收

每次需求登记主模块编号，修改公共头、CMake 或跨模块接口时列出影响模块和回归测试。模块文档追加日期、需求、文件、行为、验证和风险。

验证顺序：模块测试 → 直接依赖构建 → 应用构建 → Debug/Release 全量 CTest → GUI 或真实夹具验收 → 文档和 Git 检查。

当前自动化测试用于验证共享算法和接口。Windows 桌面点选、矩形选区、旋转/平移/缩放、关闭生命周期以及真实生产拼接夹具仍需单独验收。

## 6. 兼容层清理条件

只有在所有公共 include 和生产调用方切换到规范模块路径、诊断工具仅使用模块 API、应用不再包含重复算法、Debug/Release 构建和全量 CTest 通过，并完成 GUI/夹具验收后，才能单独删除兼容 target 和 forwarding header。

## 7. 发布状态

当前状态为“迁移完成，发布验收待完成”。未完成真实 GUI 验收和生产夹具逐字段比对前，不标记为 Release Candidate。

## 8. 变更记录

### 2026-08-31

- 完成 v0.3 模块目录、规范 target、UI 独立文件和模块文档治理。
- 完成共享 seam fusion、平面拟合和渲染组件实现迁移。
- 完成 `registration_diagnostic` 到模块接口的迁移。
- Debug/Release 构建和全量 CTest 已执行；GUI 人工验收、生产夹具验收和兼容层删除仍待完成。
