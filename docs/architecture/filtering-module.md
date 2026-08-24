# Filtering 模块

`pcv_filtering` 负责可复用的点数缩减算法。

## 体素策略

- `FirstInputPoint` 保留真实测量点并返回源索引。
- `Centroid` 生成算术平均点，用于配准和诊断采样。

产品调用点必须显式选择策略。查看和最终测量流程使用真实输入点；ICP 金字塔构建可以使用质心。

## 统计滤波

`statistical_filter` 负责自适应网格尺寸估计、空间哈希、扩展邻域壳层、K 近邻平均距离和
全局均值加标准差阈值，并返回实际网格尺寸、阈值和测量点数供诊断。

查看器现有的 `removeNoise` 函数保留为兼容和流程适配器；滤波算法本身不再依赖应用。
外部消费者使用基于 `std::vector` 点云的 `pcv::filtering`，Qt 应用适配器暂时仍是
`pcv::detail::filtering` 客户端。
