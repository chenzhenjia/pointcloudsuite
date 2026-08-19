param([string]$BuildDir = "C:\qt-build-pointcloudsuite")

$ErrorActionPreference = "Stop"
$ctest = Join-Path (Split-Path (Get-Command cmake).Source) 'ctest.exe'
if (-not (Test-Path $ctest)) { $ctest = 'C:\Qt\Tools\CMake_64\bin\ctest.exe' }
& $ctest --test-dir $BuildDir --output-on-failure
exit $LASTEXITCODE
