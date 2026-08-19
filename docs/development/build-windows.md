# Windows build

Requirements:

- Qt 6.8.3 MSVC 2022 64-bit
- Visual Studio Developer PowerShell
- ASCII-only source and build paths

Build both desktop applications and tests:

```powershell
.\scripts\build_windows.ps1 -Config Debug -BuildTests
.\scripts\run_tests.ps1
```

Open the repository root `CMakeLists.txt` in Qt Creator. The workspace exposes
`pointcloudview` and `pointcloudstitch` as separate executable targets.
