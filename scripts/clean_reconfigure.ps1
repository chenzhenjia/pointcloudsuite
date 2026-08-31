param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path (Split-Path $sourceDir -Parent) 'build-PointCloudSuite-Release'
}
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
