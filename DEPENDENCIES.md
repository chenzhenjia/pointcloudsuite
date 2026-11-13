# 依赖

C++17、MSVC x64、Qt6.5+，CMake3.19+（preset需3.25+），Threads和Windows Psapi。Qt Core/Gui支持领域数据，Concurrent支持任务，Widgets/OpenGL/OpenGLWidgets仅GUI/渲染。

STEP开启时依赖OCCT7.9.3精确版本；相机真机与RAW转换依赖Hikvision Mv3dLp SDK和匹配运行库。无SDK可保留Mock/纯函数；请求RAW converter而缺SDK则配置失败。

[构建与开关](docs/development/build-windows.md) · [架构](docs/architecture/architecture.md)
