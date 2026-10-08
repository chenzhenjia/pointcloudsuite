@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VSLANG=1033"
set "PATH=C:\Qt\Tools\Ninja;%PATH%"
set "BUILDDIR=%~1"
set "CONFIG=%~2"
set "TARGETS="
shift
shift
:loop
if "%~1"=="" goto run
set "TARGETS=%TARGETS% %1"
shift
goto loop
:run
if "%TARGETS%"=="" goto all
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%BUILDDIR%" --config %CONFIG% --parallel --target%TARGETS%
exit /b %ERRORLEVEL%
:all
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%BUILDDIR%" --config %CONFIG% --parallel
exit /b %ERRORLEVEL%
