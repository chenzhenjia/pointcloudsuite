# 依赖与构建环境

v0.5，核对日期2026-10-15。C++17、Windows/MSVC x64、Qt6.5+；根CMake最低3.19，preset version6需3.25+。Qt Core/Gui/Concurrent支持算法与CLI，Widgets/OpenGL/OpenGLWidgets支持画布和GUI；Threads供60并行，Windows Psapi供进程内存查询。

不依赖Python、Open3D、PCL、VTK或CUDA算法运行时。实验相机SDK可选，探测Mv3dLpApi.h/Mv3dLp.lib后编译真机源；SDK DLL和VC/Qt运行库部署另验，无SDK仍有Mock/纯函数。

[Windows构建](docs/development/build-windows.md)提供脚本、生成器、依赖跟踪、CLI和部署条件；[架构](docs/architecture/software_architecture_v0.5.md)提供target依赖。历史Qt6.8.3/MSVC19.51不等于所有版本已验证；本轮不构建或部署。
