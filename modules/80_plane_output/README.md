# 80_plane_output

## 当前状态

已实现。规范代码位于 `modules/80_plane_output/include` 和 `modules/80_plane_output/src`，target 为 `pcv_m80_plane_output`。旧 target `pcv_m80_planeoutput` 与 `pcv_output` 保留为迁移期兼容入口（仅转发）。

## 职责

PNG、PLY、JSON 成套输出。输出 Grayscale8 PNG、binary little-endian robot_base PLY 和 `sr2026-temp-workpiece-info-mvp-2` JSON。三件文件先写入 staging 目录，成功后成套提交，失败时回滚。

## 接口与测试

主要接口为 `writePlaneOutput`。主要测试为 `plane_output_tests`。输出路径、错误码和 JSON 字段以 v0.3 契约为准。

最后核对日期：2026-09-07（模块重命名后复核）。
