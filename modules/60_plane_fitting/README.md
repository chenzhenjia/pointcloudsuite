# 60_plane_fitting

## 当前状态

已实现。规范代码位于 `modules/60_plane_fitting/include` 和 `modules/60_plane_fitting/src`，target 为 `pcv_m60_plane_fitting`。旧 target `pcv_m60_planefitting` 保留为迁移期兼容入口（仅转发）。

## 职责

平面拟合与几何诊断。三点/n 点平面拟合、PCA 初始模型、RANSAC、PCA 细化、连通域筛选、一致性校验和诊断。应用入口只负责参数转换和结果映射。

## 依赖与测试

纯算法模块不得依赖 `apps/` 或 Qt Widgets。主要测试为 `plane_fitting_tests`；ROI/Mask 测试归入相应应用或接口测试。

最后核对日期：2026-09-07（模块重命名后复核）。
