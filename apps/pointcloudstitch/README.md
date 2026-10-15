# pointcloudstitch

v0.5当前说明，2026-10-15。独立桌面入口保留，链接应用层pcv_stitching_ui；原stitchingwindow.h为forwarding，原cpp/.ui不再是维护实现。[共享窗口](../stitching_ui/README.md) · [模块30](../../docs/modules/30_pointcloud_stitch.md)。

输入为有序PLY、XML和逐帧Start/End；提供完整配准/仅手眼转换，默认输出首输入目录stitch_output，四件正式产物由30/10事务提交。整帧PoseOverlapFine/2.5D已回滚，不在当前模式列表。

独立窗口退出取消并等待，--selftest-close仅用于启动冒烟；regressionrunner命令保留。窗口不是第二套算法，参数/并行/进度/取消均透传统一入口。构建开关PCV_BUILD_POINTCLOUDSTITCH仍有效，组合风险见[构建说明](../../docs/development/build-windows.md)。

真实数据、质量、性能及便携部署待验收；本轮不运行动态检查，历史见[版本日志](../../docs/changelog/v0.5.md)。
