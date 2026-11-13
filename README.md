# PointCloudSuite

C++17 / Qt6 点云处理项目，保留pointcloudview工作台、scan_geometry_demo_v1扫描几何、step_template_builder_v1数模模板三个GUI，以及可选depth_raw_to_ply_v1。软件版本1.0.0，数据契约版本独立。

- [文档索引](docs/README.md)、[需求](docs/requirements/requirements.md)、[架构](docs/architecture/architecture.md)
- [Windows构建](docs/development/build-windows.md)、[测试](docs/development/testing.md)、[数据契约](docs/contracts/data-contracts.md)
- [当前目标](target.md)、[变更记录](docs/archive/changelog.md)、[迁移表](docs/archive/document-migration.md)

领域模块为10/20/30/40/50/60，共享应用任务位于apps/stitching_ui，基础类型与配置位于include/src。相机SDK与OCCT可选；不依赖PCL/VTK/Open3D/CUDA算法运行时。缓存、日志、构建和输出位于源码树外。

模板定位后端保留，机器人移动判定/IK/碰撞/控制未实现。真实设备、生产样本、性能与干净机部署需单独验收，历史记录不代表当前通过。
