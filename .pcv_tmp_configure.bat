@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VSLANG=1033"
set "PATH=C:\Qt\Tools\Ninja;%PATH%"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S "D:\workpiece\pointcloudview\pointcloudview" -B %1 -G Ninja -DCMAKE_BUILD_TYPE=%2 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DPCV_BUILD_TESTS=ON -DPCV_BUILD_POINTCLOUDSTITCH=ON -DPCV_BUILD_POINTCLOUDVIEW=OFF -DPCV_BUILD_EXPERIMENTAL_CAMERA=OFF -DPCV_BUILD_TOOLS=OFF
exit /b %ERRORLEVEL%
