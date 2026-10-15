# 40_pointcloud_matching：工件位置确认规划占位

版本 v0.5，核对日期 2026-10-15。pcv_m40_pointcloud_matching 为 INTERFACE，聚合 30/60；旧 pcv_m40_pointcloudregistration 转发它。[架构](../architecture/software_architecture_v0.5.md) · [路线图](../development/roadmap_v0.5.md)。

## 正式目标

对经过点云处理和平面/坐标转换后的数据，生成含 robot_base 坐标信息的平面点云数据和坐标元数据，用于确定机器人坐标系中的工件信息；后续可通过基准模板或参考扫描比较，判断工件是否被挪动。

## 当前状态

业务为“规划目标，未实现”。没有本模块的基准持久化/比较、移动阈值/6DoF 判定、机器人执行/修正下发或新的正式输出闭环。临时工件 baseline 文件和现有几何/拼接输出不能作为移动判定已实现的证据。

旧 registration/matching 头只转发：多帧/ICP 归 30，ROI/临时工件归 60。历史在本模块记录的高倾角、2.5D/快速模式及配准测试已集中到[归档](../archive/history_pre_v0.5.md)与[日志](../changelog/v0.5.md)，不能误作本模块功能。

本轮仅静态检查兼容 target/header；没有移动判定测试或现场验收。后续独立冻结基准、阈值和完成条件，不新增空实现。
