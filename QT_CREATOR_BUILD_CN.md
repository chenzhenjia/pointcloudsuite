# 在 Qt Creator 中打开和编译

## 1. 打开正确的工程文件

不要打开上一级目录作为普通项目，也不要直接打开 `mainwindow.cpp`。
在 Qt Creator 中选择：

```text
文件 -> 打开文件或项目 -> pointcloudciew/CMakeLists.txt
```

例如：

```text
D:/workpiece/pointcloudview/pointcloudciew/CMakeLists.txt
```

## 2. 选择 Desktop Qt 套件

配置项目时选择 Qt 6 的 MSVC 套件，例如：

```text
Desktop Qt 6.8.3 MSVC2022 64bit
```

不要选择 MinGW 套件去编译 MSVC 版本的 Qt，也不要混用旧的构建目录。

## 3. 设置纯英文构建目录

在 Qt Creator 的“项目 -> 构建设置”中，把构建目录改为：

```text
C:/qt-build-pointcloudview
```

不要使用包含中文、空格、括号或特殊字符的路径。修改后点击：

```text
构建 -> 清理项目
构建 -> 运行 CMake
构建 -> 构建项目
```

## 4. 如果仍然显示旧错误

关闭 Qt Creator，删除旧构建目录中的 `CMakeCache.txt` 和 `CMakeFiles`，然后重新打开 `pointcloudciew/CMakeLists.txt`。不要复用之前失败的构建目录。

如果出现 `LNK2005 MainWindow::~MainWindow`，这表示旧的 `main.cpp.obj` 与新的 `mainwindow.cpp.obj` 混用了。请在 Qt Creator 中点击“构建 -> 清理项目”，然后在“项目 -> 构建设置”中删除并重新添加构建目录；也可以在 Developer PowerShell 中执行：

```powershell
./clean_reconfigure.ps1 -BuildDir C:/qt-build-pointcloudview
```

## 5. 命令行替代方案

在 Visual Studio Developer PowerShell 中执行：

```powershell
cd D:/workpiece/pointcloudview/pointcloudciew
./build_windows.ps1 -QtDir C:/Qt/6.8.3/msvc2022_64 -BuildDir C:/qt-build-pointcloudview -Config Release
```

如果工程实际放在中文目录，请先复制到 `C:/work/pointcloudview` 等纯英文目录。CMake 已经配置了路径检查，会在配置阶段给出明确提示。
