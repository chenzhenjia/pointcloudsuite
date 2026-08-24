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

    $installationPath = $null
    if ($vswhereCandidates) {
        $vswherePath = $vswhereCandidates | Select-Object -First 1
        $vswhereOutput = @(& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
        if ($LASTEXITCODE -eq 0) {
            $installationPath = $vswhereOutput |
                Where-Object { $_ -and $_.Trim() } |
                Select-Object -First 1
        }
    }

    $vcVarsCandidates = @()
    if ($installationPath) {
        $vcVarsCandidates += Join-Path $installationPath 'VC\Auxiliary\Build\vcvars64.bat'
    }

    $standardInstallations = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools',
        'C:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\18\Professional',
        'C:\Program Files\Microsoft Visual Studio\18\Enterprise',
        'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
    )
    $vcVarsCandidates += $standardInstallations | ForEach-Object {
        Join-Path $_ 'VC\Auxiliary\Build\vcvars64.bat'
    }

    $vcVarsPath = $vcVarsCandidates |
        Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
    if (-not $vcVarsPath) {
        $searched = if ($vswhereCandidates) { 'vswhere.exe and standard Visual Studio installation paths' } else { 'standard Visual Studio installation paths; vswhere.exe was not found' }
        throw "Visual Studio with the MSVC x64 workload was not found. Searched $searched. Expected VC\Auxiliary\Build\vcvars64.bat."
    }
    return $vcVarsPath
}

function Import-VcVarsEnvironment([string]$VcVarsPath) {
    $commandLine = '"{0}" && set' -f $VcVarsPath
    $environmentLines = @(& cmd.exe /d /s /c $commandLine 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize the MSVC environment with $VcVarsPath (cmd.exe exit code $LASTEXITCODE)."
    }

    foreach ($line in $environmentLines) {
        $text = [string]$line
        if ($text -match '^(?<Name>[^=]+)=(?<Value>.*)$') {
            Set-Item -Path ("Env:{0}" -f $matches.Name) -Value $matches.Value
        }
    }
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Assert-Ascii $SourceDir 'Source path'
Assert-Ascii $BuildDir 'Build path'
Assert-Ascii $QtDir 'Qt path'

$clPath = Get-ExecutablePath 'cl.exe'
$nmakePath = Get-ExecutablePath 'nmake.exe'
if (-not $clPath -or -not $nmakePath) {
    Write-Host 'MSVC Developer Environment is incomplete. Initializing x64 tools...'
    $vcVarsPath = Get-VcVars64Path
    Write-Host "Using MSVC initialization script: $vcVarsPath"
    Import-VcVarsEnvironment $vcVarsPath
    $clPath = Get-ExecutablePath 'cl.exe'
    $nmakePath = Get-ExecutablePath 'nmake.exe'
}
if (-not $clPath) { throw 'MSVC cl.exe was not found. Verify the Visual Studio C++ workload and vcvars64.bat.' }
if (-not $nmakePath) { throw 'NMake nmake.exe was not found. Verify the Visual Studio C++ workload and vcvars64.bat.' }

if (-not (Test-Path -LiteralPath $QtDir -PathType Container)) {
    throw "Qt directory was not found: $QtDir"
}
$qtConfig = Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'
if (-not (Test-Path -LiteralPath $qtConfig -PathType Leaf)) {
    throw "Qt6 CMake package was not found under Qt directory: $qtConfig"
}

$cmakeCandidates = @(
    (Get-ExecutablePath 'cmake.exe'),
    'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$CMakePath = $cmakeCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
if (-not $CMakePath) { throw 'CMake cmake.exe was not found. Checked the current PATH, Qt Tools, and Visual Studio CMake locations.' }

Write-Host "CMake: $CMakePath"
Write-Host "MSVC: $clPath"
Write-Host "NMake: $nmakePath"
Write-Host "Qt: $QtDir"

$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
& $CMakePath -S $SourceDir -B $BuildDir -G 'NMake Makefiles' `
    ("-DCMAKE_PREFIX_PATH={0}" -f $QtDir) `
    ("-DCMAKE_BUILD_TYPE={0}" -f $Config) `
    ("-DPCV_BUILD_TESTS={0}" -f $testOption)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $CMakePath --build $BuildDir --parallel
exit $LASTEXITCODE
