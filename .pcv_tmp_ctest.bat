@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VSLANG=1033"
set "PATH=C:\Qt\Tools\Ninja;%PATH%"
set "BUILDDIR=%~1"
set "CONFIG=%~2"
set "MODE=%~3"
if /I "%MODE%"=="targeted" goto targeted
"C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%BUILDDIR%" -C %CONFIG% --output-on-failure
exit /b %ERRORLEVEL%
:targeted
"C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%BUILDDIR%" -C %CONFIG% -R "multiframe_registration_tests|stitching_interface_tests" --output-on-failure
exit /b %ERRORLEVEL%
