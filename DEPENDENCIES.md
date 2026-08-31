# Dependencies

## Build requirements

- Windows 10 or Windows 11
- CMake 3.19 or newer
- MSVC x64 with C++17 support
- Qt 6.5 or newer, using a kit that provides `Core`, `Gui`, `Widgets`, `Concurrent`, `OpenGL`, and `OpenGLWidgets`

Open the repository-root `CMakeLists.txt` in Qt Creator and select a compatible Desktop Qt/MSVC kit. Qt Creator supplies the Qt location through its kit; no Qt installation path is stored in the repository.

For command-line builds, place `cmake.exe` and either `qtpaths6.exe`, `qtpaths.exe`, or `qmake.exe` on `PATH`, set `CMAKE_PREFIX_PATH`/`Qt6_DIR`, or pass `-QtDir <Qt prefix>` to `scripts/build_windows.ps1`. The script uses `vswhere.exe` to locate the MSVC environment.

Windows deployment uses Qt `windeployqt` and the MSVC runtime. The project does not require Python, PCL, Open3D, VTK, CUDA, a database, or a cloud service.
