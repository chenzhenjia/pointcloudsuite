# 80_planeoutput：工具平面输出

状态：已实现。规范实现位于 `modules/80_planeoutput/{include,src}`，模块 target 为 `pcv_m80_planeoutput`，兼容 target 为 `pcv_output`。

契约：Grayscale8 PNG（0/255）、binary little-endian XYZ PLY、`sr2026-temp-workpiece-info-mvp-2` JSON；三件套 staging 后原子提交并失败回滚。

接口/测试：`writePlaneOutput`；`plane_output_tests`。

最后核对日期：2026-08-31。

## 变更记录

### 2026-08-31

- 建立模块边界和兼容入口；输出实现暂留 `src/output`。
- M2：PNG/PLY/JSON 输出实现已迁移到 `modules/80_planeoutput`，旧 `pcv_output` 保留兼容聚合。
