# pointcloudview features：同事工作区

v0.5，2026-10-15。于海军负责独立feature组件、登记的UI资源、交互说明和UI测试；MainWindow壳、共享拼接UI生命周期、CMake/模块依赖由主线陈振佳维护。[协同白名单](../../../docs/architecture/module_ui_integration_v0.5.md)。

页面的QWidget/QDialog形态及外部DTO/schema/调用方式在交接包中冻结；通过公共API和应用adapter接入，不include模块私有src，不复制算法。发布结果核对revision，取消/失败和关闭join按主线会话语义。

交接包包含owner/日期/文件白名单/API依赖/截图或交互说明/构建命令/测试及未验证/adapter替代与删除条件/回滚点。不得把尚未实现的外部刀路或40业务写成完成。详细更新只进[日志](../../../docs/changelog/v0.5.md)，并在target登记摘要。

2026-10-16：主线/Codex按用户授权将相机采集面板升级为CameraCaptureWizard四步向导，camera_capture_wizard.h为当前应用接口，camera_capture_panel_v1.h保留alias；原cpp继续作为唯一实现。设备/预览由CameraDialog管理，取流/保存/坐标/拼接均复用既有模块。见[操作与状态](../../../docs/development/camera_capture_ui_simplification_v1.md)。
