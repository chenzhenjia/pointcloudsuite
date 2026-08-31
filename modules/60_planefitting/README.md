# 60_planefitting

## 当前状态

已实现。规范代码位于 `modules/60_planefitting/include` 和 `modules/60_planefitting/src`，target 为 `pcv_m60_planefitting`。

## 职责

三点/n 点平面拟合、PCA 初始模型、RANSAC、PCA 细化、连通域筛选、一致性校验和诊断。应用入口只负责参数转换和结果映射。

## 依赖与测试

纯算法模块不得依赖 `apps/` 或 Qt Widgets。主要测试为 `plane_fitting_tests`；ROI/Mask 测试归入相应应用或接口测试。

最后核对日期：2026-08-31。
