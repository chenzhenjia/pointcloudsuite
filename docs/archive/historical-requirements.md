# 历史需求

历史证据，原文日期、命令与结果只代表当时状态，不能替代当前工作区验证。源码、公共头和 CMake 为实现事实。

<a id="source-docs-requirements-pointcloudview-ui-design-v1-md"></a>

## 来源：`docs/requirements/pointcloudview_ui_design_v1.md`

### pointcloudview UI design v1

日期：2026-10-23。主责：`apps/pointcloudview` 与模块 20 渲染层。

#### 视觉与布局

- 主窗口采用极简工程风格：蓝色顶栏、白色控件、黑色 3D 点云画布。
- 顶部只保留 `打开点云`、`打开扫描 JSON`、`导入工序 JSON`、工作页选择和 `工具`。
- 工具通过右侧覆盖抽屉按“数据 / 处理 / 输出 / 高级”分组；抽屉关闭不销毁页面、参数或后台任务。
- 预览工具栏提供适配、重置、色彩、叠加、截图和全屏。
- 1280×720 验收时无抽屉预览占客户区约 90%；抽屉覆盖预览而不改变画布尺寸。

#### 点云默认展示

- 默认点大小为 `2`，高度颜色自动使用当前 Z 范围。
- 默认高度伪彩为蓝、青、绿、黄、红的 `RAINBOW` 梯度。
- 黑色画布固定显示无数值彩条、左下 XYZ 坐标轴和右上方向控件。
- 默认关闭地板网格、包围盒和统计文字；详细叠加项通过工具栏/渲染设置开启。
- 默认视角为自动适配的轻微倾斜顶视；方向控件支持顶、底、左、右快捷视角。
- 亮度纹理仍可切换查看，但不会改变首次加载的高度伪彩默认值。

#### 交互与生命周期

- 工作页切换不会取消相机、拼接、六分屏或模板任务。
- 取点和边缘选择期间工具抽屉自动隐藏；取消、完成或失败后恢复。
- `Esc`、`Backspace`、取消、关闭和后台 join 继续使用现有语义。
- 失败或取消不发布部分结果；revision 门禁、临时工件会话和数据契约保持不变。

#### 六分屏选面页紧凑布局

- 六格预览为页面主体；手眼/输出/选面参数、帧列表与加载/提交操作收进默认收起的“帧配置 / 文件列表”。
- 运行日志收进默认收起的“运行日志”；展开后仍保留原完整诊断文本。
- 收起只改变控件可见性，不清除参数、帧、选择、后台任务或日志内容。
- 单元回归已检查配置/日志面板默认收起且可展开/收起；原生 GUI 截图尚未重拍，因此六分屏视觉验收为 blocked。

#### 验收记录

- 原生 NVIDIA OpenGL 3.3 UI 回归：预览占比、抽屉覆盖、工作页复用、Z 梯度、色条/坐标轴/方向控件和取点恢复通过。
- `POINTCLOUDVIEW_SELFTEST_CLOSE=1` Debug 启动冒烟返回 `0`。
- 2026-10-23 六分屏收纳后：Debug 与 Release 全量 CTest 各 41/41 通过；Release 完整构建完成。Debug 完整构建在收纳改动前完成，收纳改动后定向 Debug/Release 目标均重编通过。
- `POINTCLOUDVIEW_SELFTEST_CLOSE=1` 原生窗口在当前会话超时；offscreen 平台提前退出但未产生可用退出码，不能记为通过。原生六画布 GUI 与截图对照 blocked。
- 未完成真实 PLY、相机、机器人和现场验收。


<a id="source-docs-requirements-pointcloudview-v0-1-md"></a>

## 来源：`docs/requirements/pointcloudview_v0.1.md`

### PointCloudSuite v0.1 历史文档迁移指引

原v0.1正文的独有决策、契约沿革及验证已合并到[历史归档](historical-development.md#source-docs-archive-history-pre-v0-5-md)与[阶段日志](changelog.md#source-docs-changelog-v0-5-md)，原路径保留供外部引用。版本号不表示对应内容仍为当前规范。

当前请读[需求 v0.5](historical-development.md#source-docs-requirements-pointcloudview-v0-5-md)、[整体架构](historical-development.md#source-docs-architecture-software-architecture-v0-5-md)、[数据契约](historical-development.md#source-docs-contracts-data-contracts-v0-5-md)；文件/schema本身没有因为文档升级而改名。[本文件迁移证据](historical-development.md#source-docs-archive-document-migration-v0-5-md)。更新2026-10-15。


<a id="source-docs-requirements-pointcloudview-v0-2-md"></a>

## 来源：`docs/requirements/pointcloudview_v0.2.md`

### PointCloudSuite v0.2 历史文档迁移指引

原v0.2正文的独有决策、契约沿革及验证已合并到[历史归档](historical-development.md#source-docs-archive-history-pre-v0-5-md)与[阶段日志](changelog.md#source-docs-changelog-v0-5-md)，原路径保留供外部引用。版本号不表示对应内容仍为当前规范。

当前请读[需求 v0.5](historical-development.md#source-docs-requirements-pointcloudview-v0-5-md)、[整体架构](historical-development.md#source-docs-architecture-software-architecture-v0-5-md)、[数据契约](historical-development.md#source-docs-contracts-data-contracts-v0-5-md)；文件/schema本身没有因为文档升级而改名。[本文件迁移证据](historical-development.md#source-docs-archive-document-migration-v0-5-md)。更新2026-10-15。


<a id="source-docs-requirements-pointcloudview-v0-3-md"></a>

## 来源：`docs/requirements/pointcloudview_v0.3.md`

### PointCloudSuite v0.3 历史文档迁移指引

原v0.3正文的独有决策、契约沿革及验证已合并到[历史归档](historical-development.md#source-docs-archive-history-pre-v0-5-md)与[阶段日志](changelog.md#source-docs-changelog-v0-5-md)，原路径保留供外部引用。版本号不表示对应内容仍为当前规范。

当前请读[需求 v0.5](historical-development.md#source-docs-requirements-pointcloudview-v0-5-md)、[整体架构](historical-development.md#source-docs-architecture-software-architecture-v0-5-md)、[数据契约](historical-development.md#source-docs-contracts-data-contracts-v0-5-md)；文件/schema本身没有因为文档升级而改名。[本文件迁移证据](historical-development.md#source-docs-archive-document-migration-v0-5-md)。更新2026-10-15。


<a id="source-docs-requirements-pointcloudview-v0-4-md"></a>

## 来源：`docs/requirements/pointcloudview_v0.4.md`

### PointCloudSuite v0.4 历史文档迁移指引

原v0.4正文的独有决策、契约沿革及验证已合并到[历史归档](historical-development.md#source-docs-archive-history-pre-v0-5-md)与[阶段日志](changelog.md#source-docs-changelog-v0-5-md)，原路径保留供外部引用。版本号不表示对应内容仍为当前规范。

当前请读[需求 v0.5](historical-development.md#source-docs-requirements-pointcloudview-v0-5-md)、[整体架构](historical-development.md#source-docs-architecture-software-architecture-v0-5-md)、[数据契约](historical-development.md#source-docs-contracts-data-contracts-v0-5-md)；文件/schema本身没有因为文档升级而改名。[本文件迁移证据](historical-development.md#source-docs-archive-document-migration-v0-5-md)。更新2026-10-15。
