param([string]$BuildDir = "C:\qt-build-pointcloudsuite")

$ErrorActionPreference = "Stop"
$cmake = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue
$ctestCandidates = @()
if ($cmake -and $cmake.Source) {
    $ctestCandidates += Join-Path (Split-Path $cmake.Source -Parent) 'ctest.exe'
}
$ctestCandidates += 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$ctest = $ctestCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
if (-not $ctest) {
    throw 'ctest.exe was not found. Checked the cmake.exe directory and C:\Qt\Tools\CMake_64\bin\ctest.exe.'
}
if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    throw "Build directory was not found: $BuildDir"
}

Write-Host "CTest: $ctest"
& $ctest --test-dir $BuildDir --output-on-failure
exit $LASTEXITCODE
