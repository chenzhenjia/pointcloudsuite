# Project headers

本目录保存根 `src/` 中共享模块的项目头文件。引用时使用带模块前缀的形式，
例如：

```cpp
#include <pcv/io/ply_reader.h>
#include <pcv/filtering/downsample.h>
```

平面输出契约位于 `pcv/output/plane_output.h`，负责作业上下文校验以及 PNG、
binary little-endian PLY、JSON 三件套输出。这些头文件用于本仓库内的应用、
测试和工具，不代表独立 SDK 的兼容性承诺。
