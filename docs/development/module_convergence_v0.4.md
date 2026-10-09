# v0.4 文档治理与模块收敛开发方案

版本：v0.4。日期：2026-10-09。状态：文档整理已按本方案执行；代码/API 收敛批次尚未实施。本轮只做静态检查，不构建、不运行 CTest 或 GUI。当前事实见 [正式 v0.4 基线](../requirements/pointcloudview_v0.4.md)。

## 1. 本轮交付与边界

全仓 Markdown 逐个登记当前/历史/兼容/实验/未实现状态，修正 namespace、模块归属、失效链接和索引。新增规范 10/60 模块文档，旧 05/10/60/70/80 文档保留正文并加归档入口；40 保留单一一级标题和统一未实现声明。历史测试结论按日期保留，不以历史通过率替代本次验证。

本次仅修改 Markdown。以下批次是后续代码开发顺序，不在本次整理中修改源码、CMake、JSON schema、输出文件或默认值。代码更新必须登记受影响模块文档、协同映射表及 [target.md](../../target.md)，包含日期、owner、文件/API、行为、验证、风险与回滚点。

## 2. 批次一：公共边界与兼容映射

| domain | current_entry | planned_entry |
|---|---|---|
| IO/预处理/平面输出 | `pcv::detail::io/filtering`、`pcv::output` | `pcv::io` 公共门面 |
| 拼接 | `pcv::interface::StitchingRequest/stitch` 与 `pointcloud::` 算法类型 | `pcv::stitching` 门面 |
| 坐标 | `pointcloud::`、`pcv/registration/handeye_transform.h` | `pcv::coordinates` 门面 |
| 平面/几何 | `pcv::planefitting/roi/matching` | `pcv::geometry` 门面 |
| 工件流程 | `pcv::interface`、`pcv/workpiece/*` | `pcv::workpiece` 门面 |
| 扫描/纹理/相机 | `pcv::scan/texture/camera` | 保持独立领域接口 |

采用增量 alias/forwarding 或薄门面，底层类型与算法 namespace 暂保留，不做全部实现改名。新门面不复制算法、不修改结构体布局/默认值/错误码；旧头、target 和 namespace 保留一个兼容周期。可标记的旧类型/函数使用 `[[deprecated]]`，namespace/转发头以说明和迁移记录标注；不能假装 namespace 本身可统一加弃用属性。

30 的分组请求成为主实现，旧 `StitchingOptions/stitchRawLineProfiles` 反向适配。兼容映射逐字段记录，显式保留旧 `allowOverwrite=true` 与新请求 `false` 的不同默认值，并完整透传注册 profile、2.5D、seam、preview、诊断、进度与取消。保留 Legacy 和 PoseOverlapFine 的质量/回退政策差异。

IO writer 与事务仅保留 10 实现；30/60 领域结果与提交编排的拆分先保留现有便利入口行为，再逐步提供纯结果/manifest 边界。所有正式产物走同一事务，提交失败不得返回正式成功状态。

## 3. 批次二：生产调用迁移与 UI 接入

先迁 `pointcloudview registrationdialog` 与 `registration_diagnostic` 的旧 stitching 调用，再迁 view processor/MainWindow、CLI 和必要工具的 10/50/60 公共入口。工具 target 改用规范 10/30/50，清理仅为旧 registration 路由而链接的 40 聚合依赖；保留原输入/输出与命令契约。修正 10 CMake 中仍推荐旧 target 的注释。

主线持有 MainWindow、模块依赖和任务生命周期；同事负责 feature 组件、登记的 UI/资源、交互说明与 UI 测试。临时 adapter 限定在 `apps/pointcloudview/adapters/` 并记录替代 API/删除条件。页面控件形态和外部高点/刀路 schema 在同事交接包内冻结，冻结前只保留版本化占位，不实现规划算法。pointcloudstitch 视觉不属于本轮统一。

每个迁移批次记录旧/新 header、target、namespace 和实际调用方。当前代码存在的兼容接口应保留用于回归；不得用全仓无记录字符串替换改变历史记录或公开 JSON 字段。

## 4. 批次三：审计、验证与回滚

静态门禁覆盖：Markdown 本地链接/锚点/围栏/一级标题；规范目录/target/API 与源码一致；共享模块无 apps 依赖；生产旧调用有明确迁移清单；取消/失败发布和事务覆盖政策保持；`git diff --check`。本轮执行这些静态核对，不把“静态通过”写成“编译通过”。

后续动态验证单独安排：源码树外可写 Debug/Release 构建、全量 CTest 与相关定向回归、view 主窗/feature GUI smoke、CLI-only 配置、高倾角基线。场景包含输入/取消/覆盖/回滚、旧新请求选项等价、来源映射、Qt 共享副本下串行与 1/2/4 worker 结果等价、Legacy 与快速模式回退、扫描单线容差与 P.json、严格纹理点序与会话失效。不得因本轮未运行而标记通过。

每批回滚点是本批开始前的文件/API 映射及独立 diff；保留旧接口使调用方能回退。保护已有工作区改动，不使用破坏性 Git 回退。删除兼容层必须在弃用周期结束、引用清零及构建/测试/GUI/现场条件满足后独立评审与授权，不与迁移混合。

## 5. 明确范围与待冻结项

不实现 40 移动判定、90 干涉检查、100 质量报告、外部刀路算法、机器人执行或便携部署；不提交/push/上传。本方案无新增数据 schema。

后续业务接入需要同事冻结页面形态、DTO/schema 版本与外部项目调用方式；真实数据回放需要完整 Start/End 与标定来源。性能任务按自身基准和 GUI 端到端口径验收，不能由文档整理替代。实现人员先按本方案完成公共与调用批次，业务待冻结项不阻塞当前文档交付。
