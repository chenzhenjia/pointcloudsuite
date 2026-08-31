# 80_planeoutput：工具平面输出

状态：已实现，迁移中。当前实现位于 `src/output`、`include/pcv/output`，模块 target 为 `pcv_m80_planeoutput`，兼容 target 为 `pcv_output`。

契约：Grayscale8 PNG（0/255）、binary little-endian XYZ PLY、`sr2026-temp-workpiece-info-mvp-2` JSON；三件套 staging 后原子提交并失败回滚。

接口/测试：`writePlaneOutput`；`plane_output_tests`。

## 变更记录

### 2026-08-31

- 建立模块边界和兼容入口；输出实现暂留 `src/output`。
