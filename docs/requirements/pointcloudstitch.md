# pointcloudstitch 需求与验收说明

版本归属：PointCloudSuite v0.3
最后核对日期：2026-08-31
状态：当前需求档案

## 1. 功能范围

`pointcloudstitch` 处理多帧线扫 PLY：

1. 读取和缓存 PLY；
2. 读取手眼标定 XML；
3. 根据起止位姿转换到 `robot_base`；
4. 按输入顺序执行相邻帧配准；
5. 执行 seam fusion；
6. 输出正式 `stitched_robot_base.ply` 和诊断信息。

## 2. 模块边界

- `pcv_m30_pointcloudstitch`：流程编排、输入校验、输出事务。
- `pcv_m40_pointcloudregistration`：ICP、结构点覆盖、配准诊断和 seam fusion。
- `pcv_m50_coordinateconversion`：手眼标定、位姿插值和坐标转换。
- `pcv_m10_pointcloudread`：PLY 读取和缓存。
- 应用层：窗口、文件选择、进度、取消和结果展示。

统一接口为 `pcv::interface::stitchRawLineProfiles`。应用 wrapper 只能转换参数和结果，不得复制算法。

## 3. 关键契约

- 位姿顺序为 `[X,Y,Z,A,B,C])，旋转为 `Rz(C)*Ry(B)*Rx(A)`。
- seam fusion 默认 `halfWidth=8.0f`、`mutualDistance=0.6f`、`decisionCellSize=0.5f`。
- 无真实投影重叠时保留完整点云并返回诊断。
- 真实重叠但融合失败、输入顺序错误、矩阵不可逆或发生取消时，整体失败且不得生成或复用旧正式输出。
- 保留点、来源索引和 `scanRatios` 的对应关系。

## 4. 验收

自动化测试覆盖 XML、刚体矩阵链、位姿转换、取消、ICP、seam fusion、错误输入和正式输出保护。

真实生产夹具需要另外比较 GUI 与接口路径的输出点数、来源索引、`scanRatios`、ICP diagnostics 和 seam diagnostics。该验收未完成前，不宣称发布通过。

## 5. 当前实现依据

源码和 CMake 是事实来源；历史参考程序、研究报告和旧版本需求仅用于追溯，不作为运行时依赖或当前算法规范。
