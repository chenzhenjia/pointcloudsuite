param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$BuildDir = "C:\qt-build-pointcloudsuite",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$BuildTests
)

$ErrorActionPreference = "Stop"

function Assert-Ascii([string]$Value, [string]$Name) {
    if ($Value -match '[^\x00-\x7F]') {
        throw "$Name contains non-ASCII characters: $Value"
    }
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Assert-Ascii $SourceDir 'Source path'
Assert-Ascii $BuildDir 'Build path'
Assert-Ascii $QtDir 'Qt path'

$cmakeCandidates = @(
    (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$CMakePath = $cmakeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $CMakePath) { throw 'cmake.exe was not found.' }

$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
& $CMakePath -S $SourceDir -B $BuildDir -G 'NMake Makefiles' `
    -DCMAKE_PREFIX_PATH=$QtDir `
    -DCMAKE_BUILD_TYPE=$Config `
    -DPCV_BUILD_TESTS=$testOption
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $CMakePath --build $BuildDir --parallel
exit $LASTEXITCODE
