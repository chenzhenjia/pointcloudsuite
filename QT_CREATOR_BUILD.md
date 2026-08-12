# Build and Run with Qt Creator

## Requirements

- Keep the source, build, Qt, and Visual Studio paths ASCII-only.
- Use Qt 6.8.3 MSVC 2022 64-bit with the matching MSVC compiler.
- Do not reuse a build directory created for another Qt kit or source tree.

## Open the project

Open this CMake project in Qt Creator:

```text
D:/workpiece/pointcloudview/pointcloudview/CMakeLists.txt
```

Select this kit:

```text
Desktop Qt 6.8.3 MSVC2022 64bit
```

Use an ASCII-only build directory, for example:

```text
C:/qt-build-pointcloudview
```

Then run CMake, build the project, and start `pointcloudview.exe`.
The build automatically runs `windeployqt`, so the executable directory
contains the required Qt DLLs and the `platforms/qwindows*.dll` plugin.

## Command-line build

Run from Visual Studio Developer PowerShell:

```powershell
cd D:/workpiece/pointcloudview/pointcloudview
./build_windows.ps1 `
  -QtDir C:/Qt/6.8.3/msvc2022_64 `
  -BuildDir C:/qt-build-pointcloudview `
  -Config Release
```

To remove a stale build directory and configure again:

```powershell
./clean_reconfigure.ps1 -BuildDir C:/qt-build-pointcloudview
```

## OpenGL backend

The application defaults to desktop OpenGL. This is required because the
Qt 6.8.3 software OpenGL backend crashes while creating `QOpenGLWidget` on
the verified machine. To make the choice explicit, set:

```powershell
$env:QT_OPENGL = 'desktop'
./pointcloudview.exe
```

Do not set `QT_OPENGL=software` on this installation. The NVIDIA and AMD
high-performance GPU export markers remain enabled, but the actual renderer
is still selected by Windows and the display driver.

If startup fails, inspect `startup.log` next to the executable. The final
startup line identifies whether failure occurred before QApplication,
during MainWindow construction, or after the window was shown.
