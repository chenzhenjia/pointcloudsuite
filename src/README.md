# 共享源码

本目录保存两个应用共同使用的实现代码：

- `core/`：点云基础数据类型。
- `io/`：PLY 读取和点云缓存。
- `filtering/`：比例、体素和统计滤波。
- `infrastructure/`：缓存、日志和导出目录等运行环境能力。
- `registration/`：手眼标定读取、刚体校验、机器人位姿插值和线扫点云转换。
- `interface/`：临时扫描信息读取、临时工件平面/ROI 生成和成套文件提交。
- `output/`：平面或边缘 Mask 的 PNG、robot_base PLY、JSON 输出契约。

共享实现不得依赖 Qt Widgets，也不得依赖 `apps/` 中的文件。新增模块时在
本目录的 `CMakeLists.txt` 注册，并将对应项目头文件放在
`include/pcv/<module>/`。
