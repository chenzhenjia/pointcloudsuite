param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$BuildDir = "C:\qt-build-pointcloudview",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

if ($env:VCToolsInstallDir -eq $null) {
    Write-Warning 'Run this script from Visual Studio Developer PowerShell so cl.exe and nmake.exe are available.'
}

function Assert-Ascii([string]$Value, [string]$Name) {
    if ($Value -match '[^\x00-\x7F]') {
        throw "$Name contains non-ASCII characters: $Value`nQt Windows builds require an ASCII-only path."
    }
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
Assert-Ascii $SourceDir 'Source path'
Assert-Ascii $BuildDir 'Build path'
Assert-Ascii $QtDir 'Qt path'

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Qt\Tools\CMake_64\bin\cmake.exe'
    )
    $cmakePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cmakePath) { throw 'cmake.exe was not found. Install CMake or add it to PATH.' }
} else { $cmakePath = $cmake.Source }

if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt6Config.cmake not found under $QtDir. Pass -QtDir with the Qt MSVC kit path."
}

if (-not (Get-Command nmake -ErrorAction SilentlyContinue)) {
    throw 'nmake.exe was not found. Open Developer PowerShell for Visual Studio and run this script there.'
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
& $cmakePath -S $SourceDir -B $BuildDir -G 'NMake Makefiles' `
    -DCMAKE_PREFIX_PATH=$QtDir -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmakePath --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Executable = Join-Path $BuildDir 'pointcloudview.exe'
if (-not (Test-Path $Executable)) {
    throw "Build succeeded but pointcloudview.exe was not found: $Executable"
}

Write-Host "Build and Qt deployment completed: $Executable"
exit 0
