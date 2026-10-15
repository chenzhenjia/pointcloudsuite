# pointcloudview

v0.5当前说明，2026-10-15。主入口负责会话、后台生命周期、渲染DTO和用户提示；领域算法归10/30/50/60，画布归20。[使用指南](../../docs/user-guide/POINTCLOUDVIEW_REQUIREMENTS.md) · [架构](../../docs/architecture/software_architecture_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。

## 业务入口

PLY/扫描JSON临时工件保留两组三点、WObj1、边缘/ROI和四件套；扫描页支持Global/LocalRoi。严格原始sourceIndex亮度图与原始坐标纹理实验分开，实验不手眼、不生成四件套。

工具菜单/旧多帧按钮复用pcv_stitching_ui的非模态窗口，关闭隐藏；任务失败/取消保留旧画布，成功正式发布后清旧临时工件、平面/ROI/WObj1、纹理和轨迹。主窗退出取消并join。

所选平面对话框独立：默认单击v2，三点备用，预览确认/自动下一帧；参数清候选、位姿清对应行、标定清全部。共享UI与选面launcher都属于应用编排，不把算法复制回processor。

## 内存与验收

Windows每1000ms采样当前进程WorkingSetSize/PrivateUsage，MiB/GiB；失败显示不可用，关闭停采。包含本进程窗口/任务/缓存，不含独立进程或GPU专用显存，无峰值/历史/告警。真实读数/DPI、GUI点选/纹理/性能和现场数据尚待人工验收。

MainWindow/依赖/生命周期由主线维护，同事仅在登记features/UI资源工作区。历史自动化结果只在版本日志按日期记录，本轮仅文档静态审计。
