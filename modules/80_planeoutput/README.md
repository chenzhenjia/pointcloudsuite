# 80_planeoutput

## 当前状态

已实现。规范代码位于 `modules/80_planeoutput/include` 和 `modules/80_planeoutput/src`，target 为 `pcv_m80_planeoutput`。兼容 target 为 `pcv_output`。

## 职责

输出 Grayscale8 PNG、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。三件文件先写入 staging 目录，成功后成套提交，失败时回滚。

## 接口与测试

主要接口为 `writePlaneOutput`。主要测试为 `plane_output_tests`。输出路径、错误码和 JSON 字段以 v0.3 契约为准。

最后核对日期：2026-08-31。
