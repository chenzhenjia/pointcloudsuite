# 测试结构与运行

| 目录 | 内容 |
|---|---|
| tests/unit/core | 配置和基础设施 |
| tests/unit/io、camera、filtering | PLY/缓存/事务/RAW/帧转换/组帧/过滤 |
| tests/unit/rendering、stitching、coordinates、geometry、matching | 渲染契约、拼接、坐标、选面/ROI/模板、定位 |
| tests/unit/applications | 会话、输出契约、结果区域、处理与日志工具 |
| tests/gui | 三GUI关键交互、取消/关闭/结果发布与显示回归 |
| tests/experimental/camera | SDK声明/假符号启动回归，需PCV_BUILD_EXPERIMENTAL_TESTS |
| tests/support/fixtures | 共享合成样例 |

单击选面v1/v2按两组命名空间合并到同一可执行文件，保留原有边界和串行/1/2/4 worker等价断言；删除手动synthetic benchmark入口。文件名和CTest target去掉迭代后缀，协议版本和类名不改。旧模板页测试随页面退出，CLI专属测试随CLI退出。

`ctest --test-dir <build> --output-on-failure`运行所有已注册测试。标签为unit/application/gui/experimental/smoke，可用`-L`过滤。三个实际EXE启动/自动退出smoke仅在对应应用启用时注册。SDK/STEP测试受能力开关控制，缺失能力不算该能力验证通过。

GUI测试保留已覆盖的拾取、取消、资源、revision和输出断言；不因断言涉及控件就删除。原生OpenGL、DPI、生产样本与设备仍需人工验收，历史数字不作本轮结果。

2026-11-13结构统一验收：源码树外Debug/Release全量构建和CTest各56/56通过；五种独立配置/构建通过；另行运行两种配置的原生六画布拾取、关闭STEP/相机后的3项定向测试、可选SDK假符号启动测试。命令、日志和剩余限制见[本轮记录](../archive/changelog.md#structure-optimization)。
