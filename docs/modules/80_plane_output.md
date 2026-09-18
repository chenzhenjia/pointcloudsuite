# 80_plane_output：工具平面输出

状态：兼容入口。PNG、PLY、JSON writer 与事务提交实现已迁入 `modules/10_pointcloud_io` / `pcv_m10_pointcloud_io`；`modules/80_plane_output`、`pcv_m80_plane_output`、`pcv_m80_planeoutput` 与 `pcv_output` 仅转发，不得新增重复实现。

契约：Grayscale8 PNG（0/255）、binary little-endian XYZ PLY、`sr2026-temp-workpiece-info-mvp-2` JSON；通过 `pcv::io::ArtifactTransaction` staging 后成套提交并在失败时回滚。

接口/测试：`writePlaneOutput`；`plane_output_tests`。

最后核对日期：2026-09-17。本文件历史正文中的旧名称与旧实现位置为当时状态，仅用于追溯。

## 变更记录

### 2026-09-06（模块重命名）

- 目录重命名：`modules/80_plane_output`，target 改为 `pcv_m80_plane_output`；旧 `pcv_m80_planeoutput` 保留为兼容 INTERFACE，转发到新 target。职责（Grayscale8 PNG、binary little-endian PLY、JSON 成套输出与回滚）不变。

### 2026-08-31

- 建立模块边界和兼容入口；输出实现暂留 `src/output`。
- M2：PNG/PLY/JSON 输出实现已迁移到 `modules/80_planeoutput`，旧 `pcv_output` 保留兼容聚合。
