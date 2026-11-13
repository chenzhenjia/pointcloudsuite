# Windows 构建

C++17、MSVC x64、Qt6.5+，根CMake3.19+，preset需3.25+。本机Qt6.8.3、OCCT7.9.3。优先NMake，拒绝JOM；VSLANG=1033和头依赖护栏保留。构建、日志、缓存、导出均须在源码树外，公共头变化后全量重建。

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests -ExperimentalCamera -QtDir C:/Qt/6.8.3/msvc2022_64 -OcctDir D:/Dev/vcpkg/installed/x64-windows/share/opencascade -BuildDir D:/pcv-build/structure-debug
C:/Qt/Tools/CMake_64/bin/ctest.exe --test-dir D:/pcv-build/structure-debug --output-on-failure
```

Release使用新目录与`-Config Release`。`-Gui`可多选三个GUI名称；`-Gui @()`关闭全部GUI。`-BuildRawConverter`开启RAW转换，要求`-ExperimentalCamera`和SDK；`-StepImport:$false`关闭STEP导入；`-ExperimentalTests`增加SDK特定实验测试。CLI/配准诊断已退出，不再支持旧CLI/Tools开关和部署分支。

共享preset包含全部GUI、三个单GUI及RAW-only的Debug/Release；Qt/OCCT路径由环境变量注入。RAW-only使用`raw-converter-debug`/`raw-converter-release`及对应`build-raw-converter-*`，关闭GUI、STEP和测试，开启相机SDK与RAW转换。本机忽略的CMakeUserPresets.json继续有效。测试开关为PCV_BUILD_TESTS，实验测试为PCV_BUILD_EXPERIMENTAL_TESTS；RAW为PCV_BUILD_RAW_CONVERTER。

SDK找不到时不声明真机可用；RAW工具明确拒绝缺少SDK的配置。STEP开启时严格要求OCCT7.9.3。干净机运行库部署与真机验收另行记录。
