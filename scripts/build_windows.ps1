param(
    [string]$QtDir = "",
    [string]$BuildDir = "",
    [string]$CMakePath = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$BuildTests
)

$ErrorActionPreference = "Stop"

function Assert-Ascii([string]$Value, [string]$Name) {
    if ($Value -match '[^\x00-\x7F]') { throw "$Name contains non-ASCII characters: $Value" }
}

function Get-ExecutablePath([string]$Name) {
    $command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($command -and $command.Source -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-VcVars64Path {
    $vswhereCandidates = @(
        (Get-ExecutablePath 'vswhere.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique
    foreach ($vswherePath in $vswhereCandidates) {
        $installationPath = @(& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null) |
            Where-Object { $_ -and $_.Trim() } | Select-Object -First 1
        $candidate = if ($installationPath) { Join-Path $installationPath 'VC\Auxiliary\Build\vcvars64.bat' }
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $candidate }
    }
    throw 'Visual Studio with the MSVC x64 workload was not found. Install it or run this script from a Developer PowerShell.'
}

function Import-VcVarsEnvironment([string]$VcVarsPath) {
    $environmentLines = @(& cmd.exe /d /s /c ('"{0}" && set' -f $VcVarsPath) 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Failed to initialize MSVC with $VcVarsPath." }
    foreach ($line in $environmentLines) {
        $text = [string]$line
        if ($text -match '^(?<Name>[^=]+)=(?<Value>.*)$') {
            Set-Item -Path ("Env:{0}" -f $matches.Name) -Value $matches.Value
        }
    }
}

function Get-QtDirectory([string]$RequestedQtDir) {
    $candidates = @($RequestedQtDir)
    if ($env:Qt6_DIR) { $candidates += (Split-Path (Split-Path (Split-Path $env:Qt6_DIR -Parent) -Parent) -Parent) }
    if ($env:CMAKE_PREFIX_PATH) { $candidates += $env:CMAKE_PREFIX_PATH -split ';' }
    foreach ($tool in @('qtpaths6.exe', 'qtpaths.exe', 'qmake.exe')) {
        $path = Get-ExecutablePath $tool
        if (-not $path) { continue }
        $query = if ($tool -eq 'qmake.exe') { @('-query', 'QT_INSTALL_PREFIX') } else { @('--query', 'QT_INSTALL_PREFIX') }
        $prefix = @(& $path @query 2>$null) | Select-Object -First 1
        if ($prefix) { $candidates += $prefix.Trim() }
    }
    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        $config = Join-Path $candidate 'lib\cmake\Qt6\Qt6Config.cmake'
        if (Test-Path -LiteralPath $config -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'Qt 6 was not found. Select a Qt Creator Kit, add qtpaths6/qmake to PATH, set CMAKE_PREFIX_PATH/Qt6_DIR, or pass -QtDir <Qt prefix>.'
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path (Split-Path $SourceDir -Parent) ("build-PointCloudSuite-{0}" -f $Config)
}
Assert-Ascii $SourceDir 'Source path'
Assert-Ascii $BuildDir 'Build path'

if (-not (Get-ExecutablePath 'cl.exe') -or -not (Get-ExecutablePath 'nmake.exe')) {
    Import-VcVarsEnvironment (Get-VcVars64Path)
}
if (-not (Get-ExecutablePath 'cl.exe') -or -not (Get-ExecutablePath 'nmake.exe')) {
    throw 'MSVC x64 tools were not initialized.'
}

$QtDir = Get-QtDirectory $QtDir
Assert-Ascii $QtDir 'Qt path'
if ([string]::IsNullOrWhiteSpace($CMakePath)) { $CMakePath = Get-ExecutablePath 'cmake.exe' }
if (-not $CMakePath) { throw 'cmake.exe was not found on PATH. Install CMake or start the script from the Qt Creator/Developer environment.' }

$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
Write-Host "CMake: $CMakePath"
Write-Host "Qt: $QtDir"
Write-Host "Build: $BuildDir"
& $CMakePath -S $SourceDir -B $BuildDir -G 'NMake Makefiles' `
    ("-DCMAKE_PREFIX_PATH={0}" -f $QtDir) `
    ("-DCMAKE_BUILD_TYPE={0}" -f $Config) `
    ("-DPCV_BUILD_TESTS={0}" -f $testOption)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $CMakePath --build $BuildDir --parallel
exit $LASTEXITCODE
