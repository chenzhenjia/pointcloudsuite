# Shared source

本目录保存两个应用共同使用的实现代码：

- `core/`：点云基础数据类型。
- `io/`：PLY 读取和点云缓存。
- `filtering/`：比例、体素和统计滤波。
- `infrastructure/`：缓存、日志和导出目录等运行环境能力。
- `output/`：平面 PNG、robot_base PLY、JSON 三件套输出契约。

共享实现不得依赖 Qt Widgets，也不得依赖 `apps/` 中的文件。新增模块时在
本目录的 `CMakeLists.txt` 注册，并将对应项目头文件放在
`include/pcv/<module>/`。
