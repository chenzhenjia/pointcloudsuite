# 60 几何与工件

Target：`pcv_m60_geometry_workpiece`。公共头位于对应模块`include/pcv/`，算法位于`src/`。

入口：fit、preparePlaneSelectionIndex/selectPlaneAtPoint、reference match、高侧候选、plane mask、ROI/WObj1、临时工件、planScanTrajectory、mapPlaneBrightness、CAD/实测模板。

单击选面使用完整不可变索引、多尺度RANSAC/PCA、全帧垂距、投影连通与稳健精化；不补点/加厚/压平。纹理严格row-major来源映射；Mask为Grayscale8 0/255。STEP仅支持有效闭合单实体，不做装配或自动修复。扫描轨迹不含IK/碰撞/机器人控制。

当前测试见[测试说明](../development/testing.md)，历史实现与验收见[历史开发](../archive/historical-development.md)。
