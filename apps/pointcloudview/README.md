# pointcloudview

v0.5当前说明，2026-10-15。主入口负责会话、后台生命周期、渲染DTO和用户提示；领域算法归10/30/50/60，画布归20。[使用指南](../../docs/user-guide/POINTCLOUDVIEW_REQUIREMENTS.md) · [架构](../../docs/architecture/software_architecture_v0.5.md) · [版本日志](../../docs/changelog/v0.5.md)。

## 业务入口

PLY/扫描JSON临时工件保留两组三点、WObj1、边缘/ROI和四件套；扫描页支持Global/LocalRoi。严格原始sourceIndex亮度图与原始坐标纹理实验分开，实验不手眼、不生成四件套。

工具菜单/旧多帧按钮复用pcv_stitching_ui的非模态窗口，关闭隐藏；任务失败/取消保留旧画布，成功正式发布后清旧临时工件、平面/ROI/WObj1、纹理和轨迹。主窗退出取消并join。

所选平面对话框独立：默认单击v2，三点备用，预览确认/自动下一帧；参数清候选、位姿清对应行、标定清全部。共享UI与选面launcher都属于应用编排，不把算法复制回processor。

## 工件模板 v1

工具菜单“创建工件模板”：在空闲、非相机实时预览且当前点云已确认为 robot_base 时，填写工件名称/源码树外保存根目录，核对点数、包围盒和来源，勾选基准状态确认后保存。手眼转换、整帧拼接/仅转换或合法模板读取可建模板；去噪继承来源，恢复原始点云恢复原始来源。普通 PLY 和原始纹理/相机点云不能靠文件名或 WObj1 声明 robot_base。

每次生成独立版本文件夹，包含 workpiece_template_robot_base.ply 和 workpiece_template_info.json；默认根为运行数据目录/workpiece_templates，旧版本不覆盖。保留全部 XYZ/已有法向及点序，不保存 WObj1/ROI/纹理，不裁剪/补面或比较位置。文件夹可整体复制，通过“打开工件模板”选择 JSON 打开，原扫描文件缺失不影响读取。

读写后台执行、提供阶段进度和取消；读入校验成功才替换画布并清旧派生会话，失败/取消保留原点云。保存成功展示版本目录，画布保持不变；commit 已完成不因晚到取消误报失败。关闭取消并等待任务。模板文件有效不证明标定、拼接质量或基准状态正确；本次真实 GUI 样例待用户指定。

## 内存与验收

Windows每1000ms采样当前进程WorkingSetSize/PrivateUsage，MiB/GiB；失败显示不可用，关闭停采。包含本进程窗口/任务/缓存，不含独立进程或GPU专用显存，无峰值/历史/告警。真实读数/DPI、GUI点选/纹理/性能和现场数据尚待人工验收。

MainWindow/依赖/生命周期由主线维护，同事仅在登记features/UI资源工作区。历史自动化结果只在版本日志按日期记录，本轮仅文档静态审计。
