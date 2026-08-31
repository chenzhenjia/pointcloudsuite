# 点云过滤模块

`pcv_filtering` 提供可复用的点数缩减和统计滤波算法。

## 算法边界

- 比例降采样按输入顺序保留真实点。
- 体素降采样明确区分 `FirstInputPoint` 和 `Centroid`。
- 统计滤波保留有效点和 source index；邻域不足或结果为空时按接口约定回退并返回诊断。
- 非法参数不得悄悄生成虚拟点。
- 共享过滤模块不得依赖应用或 Qt Widgets。

详细参数、默认值和测试入口以 `src/filtering`、`include/pcv/filtering` 和 `tests/` 为准。

最后核对日期：2026-08-31。
