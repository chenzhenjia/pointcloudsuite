param(
    [string]$BuildDir = "C:\qt-build-pointcloudview"
)

$ErrorActionPreference = "Stop"
if ($BuildDir -match '[^\x00-\x7F]') {
    throw "Build path contains non-ASCII characters: $BuildDir"
}

if (Test-Path $BuildDir) {
    Write-Host "Removing old CMake build directory: $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$script = Join-Path $PSScriptRoot 'build_windows.ps1'
& $script -BuildDir $BuildDir
exit $LASTEXITCODE
