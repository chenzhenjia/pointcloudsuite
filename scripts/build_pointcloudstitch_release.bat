@echo off
set VSLANG=1033
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
powershell -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" -Config Release
