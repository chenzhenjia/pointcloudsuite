# PointCloudSuite 点云处理项目需求文档

版本：v0.3  
日期：2026-08-31  
状态：活动基线

## 1. 目标

v0.3 将当前 PointCloudSuite 统一为企业级模块化项目。源码、CMake target、测试和文档按十个稳定编号治理；已有 v0.2 功能作为实现基线，后续需求必须可追溯到模块、接口、测试和变更记录。

当前事实来源仍为源码、CMake 和测试。v0.1/v0.2 文档只作历史参考。

## 2. 模块基线

| 编号 | 模块 | 当前实现映射 | 状态 |
|---|---|---|---|
| `10_pointcloudread` | 点云读写 | `src/io`、`include/pcv/io`、`pcv_io` | 已实现，迁移中 |
| `20_pointcloudrender` | 点云渲染 | `apps/pointcloudview` Qt/OpenGL | 部分实现 |
| `30_pointcloudstitch` | 点云拼接 | `apps/pointcloudstitch` 流程与接缝 | 部分实现 |
| `40_pointcloudregistration` | 点云配准 | `src/registration` ICP/结构验收 | 已实现，边界收敛中 |
| `50_coordinateconversion` | 坐标转换 | `src/registration` 手眼和位姿变换 | 已实现，迁移中 |
| `60_planefitting` | 平面拟合 | `apps/pointcloudview/pointcloudprocessor.*` | 已实现，抽取中 |
| `70_roi_template` | ROI/模板 | `pcv_interface` 与 pointcloudview 平面映射 | 部分实现 |
| `80_planeoutput` | 工具平面输出 | `src/output`、`pcv_output` | 已实现，迁移中 |
| `90_interferenceplane` | 干涉平面检查 | 暂无独立实现 | 未实现 |
| `100_qualityreport` | 质量报告 | 暂无独立实现 | 未实现 |

模块目标目录为 `modules/<编号>_<名称>/`，每个已实现模块包含 `include/`、`src/`、`tests/`、`CMakeLists.txt` 和 `README.md`。迁移期间保留现有目录与兼容 target。

## 3. 处理流程

标准流程为：输入契约校验 → `10` 读取/缓存 → `20` 渲染与点选 → `50` 坐标转换 → `60` 平面拟合 → `70` ROI/模板 → `80` 成套输出。多帧场景由 `30` 编排并调用 `40` 配准。`90`、`100` 未实现时不得生成伪成功结果。

## 4. 跨模块契约

- 点坐标单位为 mm；公共位姿数组固定为 `[X,Y,Z,A,B,C]`，其中 `A=Rx`、`B=Ry`、`C=Rz`。
- 旋转矩阵约定为 `Rz(C)*Ry(B)*Rx(A)`；坐标转换必须声明源坐标系和目标坐标系。
- 正式平面输出为 Grayscale8 PNG（前景 255、背景 0）、binary little-endian XYZ PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。
- 输出文件先写入 staging 目录，PNG/PLY/JSON 全部成功后一次性提交；任一失败必须回滚。
- 共享模块不得依赖应用；共享算法不得依赖 Qt Widgets；运行数据不得写入源码树。

## 5. 代码与变更治理

每次需求登记一个主模块编号。修改公共头文件、CMake 或跨模块接口时，必须列出影响模块和回归测试。模块文档使用追加式日志，记录日期、需求 ID、修改文件、行为变化、验证命令、结果和未验证风险。

## 6. 开发路线

1. M0：建立 v0.3、十个模块文档、`AGENTS.md` 规则和需求追踪。
2. M1：建立 `modules/` 目录、统一 target 命名和现有实现映射。
3. M2：迁移 `10`、`50`、`60`、`80` 的公共代码与测试，保留兼容入口。
4. M3：拆分 `30` 拼接编排和 `40` 配准算法，切换应用入口。
5. M4：抽取 `20` 渲染与 `70` ROI/模板，删除重复处理器实现。
6. M5：删除兼容 target/forwarding header，完成全量构建和审计。
7. M6：根据实际业务需求启动 `90` 干涉平面检查和 `100` 质量报告。

## 7. 验收门槛

文档变更执行 `git diff --check` 和 `git status --short`。代码迁移执行 `scripts/build_windows.ps1 -Config Debug -BuildTests`、`scripts/run_tests.ps1 -BuildDir C:\qt-build-pointcloudsuite` 和全量 CTest；涉及公共头、CMake、处理器或输出契约时必须完整重编译。GUI 点击、真实生产数据和 `90`/`100` 功能在未验证前明确标记为未验证或未实现。

## 8. 变更记录

### 2026-08-31

- 建立 v0.3 活动基线，固定十个模块编号、代码迁移策略、标准流程和验收门槛。
- 建立模块文档与 `modules/` CMake 兼容入口；未移动现有用户修改。
