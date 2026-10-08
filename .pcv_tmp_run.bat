@echo off
setlocal
set "PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%"
%*
echo EXITCODE=%ERRORLEVEL%
exit /b 0
