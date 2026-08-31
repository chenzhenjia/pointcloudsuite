# 点云读写模块

规范实现位于 `modules/10_pointcloudread`，target 为 `pcv_m10_pointcloudread`；`pcv_io` 仅为迁移期兼容 target。

## 支持格式

- ASCII PLY；
- binary little-endian PLY；
- binary big-endian PLY；
- 任意标量属性顺序；
- 可选 `nx`、`ny`、`nz` 法向属性。

读取器按属性名读取 `x`、`y`、`z`，拒绝缺坐标、不支持的类型、vertex list、坏 header、坏数字、非有限值和截断 payload。

## 缓存与取消

`pcv::detail::io::readPly` 和 `readPlyCached` 保持点序、bounds、source index、取消和错误语义。缓存校验格式版本、源文件状态、点数和 payload 长度，并使用 `QSaveFile` 写入。

共享读取器不访问 QWidget/OpenGL。具体字段、计时和并行解析约束见 v0.3 需求文档与公共头文件。

最后核对日期：2026-08-31。
