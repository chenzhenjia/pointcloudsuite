# 代码构成与架构图

本文档基于对仓库源码、CMake 目标与依赖关系的实际扫描生成，描述
PointCloudSuite 的代码构成，并给出清晰的架构图。

## 架构图（Mermaid）

```mermaid
flowchart TB
    subgraph Apps["应用层 apps/"]
        VIEW["pointcloudview<br/>Qt Widgets + OpenGL 查看/分析程序<br/>main.cpp / mainwindow / pointcloudprocessor"]
        STITCH["pointcloudstitch<br/>多帧线激光点云拼接程序<br/>main.cpp / stitchingwindow<br/>handeye_transform / seamfusion / regressionrunner"]
    end

    subgraph Libs["共享算法层 src/ → include/pcv/"]
        CORE["pcv_core（INTERFACE）<br/>pointcloud::Point3D 基础类型"]
        INFRA["pcv_infrastructure（STATIC）<br/>runtime_paths 运行目录管理"]
        IO["pcv_io（STATIC）<br/>ply_reader + cloud_cache<br/>ASCII/二进制 PLY 读取、缓存"]
        FILTER["pcv_filtering（STATIC）<br/>downsample + statistical_filter<br/>比例/体素/统计滤波"]
        OUTPUT["pcv_output（STATIC）<br/>plane_output 平面输出契约<br/>PNG / JSON / PLY 导出"]
        REG["pcv_registration（STATIC）<br/>手眼标定、位姿插值、robot_base 转换"]
        INTERFACE["pcv_interface（STATIC）<br/>临时扫描输入、平面/ROI、四件输出"]
    end

    subgraph Support["支撑层"]
        TESTS["tests/ 单元测试<br/>io / filtering / output / interface<br/>+ 直接编译 app 源码的兼容测试"]
        TOOLS["tools/registration_diagnostic<br/>诊断 CLI（编译 app 源码）"]
        DOCS["docs/ 架构、需求、契约、用户文档"]
        SCRIPTS["scripts/ 构建与测试脚本<br/>build_windows / run_tests / clean_reconfigure"]
    end

    subgraph Platform["平台层"]
        QT["Qt 6.5+<br/>Core / Gui / Widgets /<br/>Concurrent / OpenGL / OpenGLWidgets"]
        OS["Windows · MSVC · C++17<br/>ASCII-only 源码/构建路径"]
    end

    VIEW --> IO
    VIEW --> FILTER
    VIEW --> OUTPUT
    VIEW --> INTERFACE
    VIEW --> INFRA
    VIEW --> CORE
    STITCH --> IO
    STITCH --> FILTER
    STITCH --> OUTPUT
    STITCH --> INFRA
    STITCH --> CORE
    IO --> CORE
    IO --> INFRA
    FILTER --> CORE
    OUTPUT --> INFRA
    OUTPUT --> CORE
    REG --> CORE
    INTERFACE --> REG
    INTERFACE --> IO
    VIEW --> QT
    STITCH --> QT
    TESTS --> IO
    TESTS --> FILTER
    TESTS --> OUTPUT
    TESTS --> INTERFACE
    TESTS --> REG
    TOOLS --> IO
    TOOLS --> FILTER
    TOOLS --> INFRA
    QT --> OS
```

## 分层依赖关系

| 目标 | 类型 | 内容 | 依赖 |
| --- | --- | --- | --- |
| `pcv_core` | INTERFACE 库 | `point_types.h`：`pointcloud::Point3D` | Qt::Core |
| `pcv_infrastructure` | 静态库 | `runtime_paths`：应用数据/缓存/日志/导出目录 | Qt::Core |
| `pcv_io` | 静态库 | `ply_reader`（ASCII/二进制 LE/BE，进度、取消、并行解析）、`cloud_cache`（校验缓存） | pcv_core、pcv_infrastructure、Qt::Core |
| `pcv_filtering` | 静态库 | `downsample`（比例/体素，首点与质心策略）、`statistical_filter` | pcv_core、Qt::Core |
| `pcv_output` | 静态库 | `plane_output`：作业上下文校验、平面 PNG/JSON/PLY 契约导出、错误码 | pcv_infrastructure、pcv_core、Qt::Core、Qt::Gui |
| `pcv_registration` | 静态库 | XML 手眼标定读取、刚体矩阵校验、机器人位姿插值、线扫点云转换 | pcv_core、Qt::Core、Qt::Gui |
| `pcv_interface` | 静态库 | `temp_workpiece_interface`：扫描信息解析、平面/ROI 生成、四件输出事务提交 | pcv_core、pcv_io、pcv_registration、pcv_infrastructure、Qt::Core、Qt::Gui |
| `pointcloudview` | 可执行程序 | PLY 加载/缓存、降噪、三点平面提取、工件坐标系、平面边缘分割、平面图像导出 | 全部 pcv 库 + Qt Widgets/Concurrent/OpenGL/OpenGLWidgets |
| `pointcloudstitch` | 可执行程序 | 手眼标定读取、逐帧机器人坐标转换、相邻帧 ICP 配准（平面预对齐/结构验收/信赖域）、接缝融合、回归模式 | 全部 pcv 库 + Qt Core/Gui/Widgets/Concurrent |
| `registration_diagnostic` | CLI（默认关闭） | 直接编译 `apps/pointcloudview/pointcloudprocessor.cpp` | pcv_core、pcv_infrastructure、pcv_io、pcv_filtering |
| 单元测试（7 个共享模块测试 + 1 个 app 源码兼容测试） | CTest | `tests/unit/{io,filtering,output,interface,pointcloudview,registration}` | 对应 pcv 库；`pointcloudprocessor_obstacle_tests` 保留历史目标名，当前验证边缘 Mask；`handeye_transform_tests` 链接 pcv_registration |

## 代码规模（按目录）

| 目录 | 说明 | 约行数 |
| --- | --- | --- |
| `apps/pointcloudview` | 查看/分析程序 | ~8,150 |
| `apps/pointcloudstitch` | 拼接程序（配准/手眼/接缝/回归） | ~3,670 |
| `src/io` + `include/pcv/io` | PLY 读取与缓存 | ~790 |
| `src/filtering` + `include/pcv/filtering` | 降采样与统计滤波 | ~320 |
| `src/output` + `include/pcv/output` | 平面输出契约 | ~380 |
| `src/registration` + `include/pcv/registration` | 手眼标定和点云坐标转换 | ~470 |
| `src/interface` + `include/pcv/interface` | 临时扫描输入和临时工件输出 | ~1,040 |
| `src/infrastructure` + `include/pcv/infrastructure` | 运行目录 | ~45 |
| `src/core` + `include/pcv/core` | 基础类型 | ~15 |

## 依赖规则（与 docs/architecture/overview.md 一致）

1. 共享算法代码不得依赖 Qt Widgets。
2. 应用可依赖共享模块；共享模块不得反向依赖应用。
3. 运行时生成文件不得写入源码树。
4. 测试只在根 `tests/` 目录注册。

## 当前维护提示

- 两个应用各维护一份同名但内容不同的 `pointcloudprocessor.{h,cpp}`
  （`pointcloudview` 版约 7,600 行，`pointcloudstitch` 版约 1,700 行），
  文档说明这是“算法逐步抽取期间保持的应用边界”；`tests/` 与
  `tools/registration_diagnostic` 均以“直接编译 app 源文件”方式复用，
  而非链接共享库。
- `pointcloudstitch` 的手眼转换实现已迁移到 `pcv_registration`；应用目录中的旧
  `handeye_transform.{h,cpp}` 不再由当前 CMake 目标编译，不应继续作为真相源。
- `mid_gap/parallel_ascii_tests/` 与 `test_pointcloud_a/` 属于实验/回归产物目录，
  不属于源码发布集；`.gitignore` 对两者均提供目录级排除规则。
