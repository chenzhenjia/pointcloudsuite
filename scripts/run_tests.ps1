param([string]$BuildDir = "")

$ErrorActionPreference = "Stop"
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path (Split-Path $sourceDir -Parent) 'build-PointCloudSuite-Debug'
}
$ctest = Get-Command ctest.exe -CommandType Application -ErrorAction SilentlyContinue
if (-not $ctest -or -not $ctest.Source) { throw 'ctest.exe was not found on PATH.' }
if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    throw "Build directory was not found: $BuildDir"
}
& $ctest.Source --test-dir $BuildDir --output-on-failure
exit $LASTEXITCODE
