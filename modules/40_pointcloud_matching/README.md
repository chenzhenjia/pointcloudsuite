# 40_pointcloud_matching

## 当前状态

规划占位。40 的正式声明是工件位置确认能力，但本版不实现移动判定或新输出闭环。多帧 ICP 已迁入 30，ROI/临时工件几何已迁入 60；`pcv_m40_pointcloud_matching` 与 `pcv_m40_pointcloudregistration` 仅为兼容聚合入口。

## 职责

平面/ROI 点匹配与 `robot_base` 完整回传：正式接口 `pcv::matching::{RoiMatchInput, RoiMatchResult, matchRoiToRobotBase(...)}`（`include/pcv/matching/roi_matching.h`），校验 ROI 索引、平面来源、点数量与有限性，经 50 坐标转换生成 `robot_base` 点并保留原始点索引；fail-closed，任一 ROI 点无法合法回传则不发布不完整结果；返回完整性统计、丢失点数、覆盖率、取消状态与失败原因。保留 `registerRobotBaseFrames()`/ICP/结构校验等底层能力（兼容接口，内部逐步转向 matching API）；不再负责 seam fusion 编排与最终拼接输出（seam fusion 实现与公共类型见 `pcv_m30_pointcloud_stitch`）。

## 依赖与测试

不得依赖应用或 Qt Widgets。主要测试为 `roi_matching_tests` 和 `multiframe_registration_tests`；模块 30 的 `seam_fusion_tests` 覆盖与本模块配准结果的集成。

最后核对日期：2026-09-07（模块重命名、职责重划与 ROI 匹配接口接入临时工件流程）。
