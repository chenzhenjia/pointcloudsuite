param(
    [string]$QtDir = "",
    [string]$BuildDir = "",
    [string]$CMakePath = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$BuildTests,
    # Force a full rebuild (cmake --build --clean-first). Required after any
    # change to a public header when the build tree does not track header
    # dependencies; see docs/development/build-windows.md.
    [switch]$Clean,
    [switch]$ExperimentalCamera,
    # "NMake Makefiles" (default) or "Ninja". Ninja relies on the MSVC
    # /showIncludes prefix for header dependencies and silently loses them on
    # a localized MSVC; the Makefile generators use CMake's own scanner, which
    # this project forces on in CMakeLists.txt.
    [ValidateSet("", "Ninja", "NMake Makefiles", "NMake Makefiles JOM")]
    [string]$Generator = ""
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

function Get-NinjaPath {
    $candidates = @(
        (Get-ExecutablePath 'ninja.exe'),
        (Join-Path $env:SystemDrive 'Qt\Tools\Ninja\ninja.exe'),
        'C:\Qt\Tools\Ninja\ninja.exe',
        (Join-Path $env:SystemDrive 'Qt\Tools\QtCreator\bin\ninja.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    return $candidates
}

function Get-JomPath {
    return @(
        (Get-ExecutablePath 'jom.exe'),
        (Join-Path $env:SystemDrive 'Qt\Tools\QtCreator\bin\jom\jom.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
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

if (-not (Get-ExecutablePath 'cl.exe')) {
    Import-VcVarsEnvironment (Get-VcVars64Path)
}
$env:VSLANG = '1033'
if (-not (Get-ExecutablePath 'cl.exe')) {
    throw 'MSVC x64 tools were not initialized.'
}

$NinjaPath = Get-NinjaPath
if ([string]::IsNullOrWhiteSpace($Generator)) { $Generator = 'NMake Makefiles' }
if ($Generator -eq 'NMake Makefiles JOM') {
    throw 'NMake Makefiles JOM does not emit usable MSVC header dependency files in this environment. Use -Generator "NMake Makefiles".'
}
if ($Generator -eq 'Ninja') {
    if (-not $NinjaPath) { throw 'ninja.exe was not found. Install Ninja or pass -Generator "NMake Makefiles".' }
    Write-Warning 'Ninja derives header dependencies from the MSVC /showIncludes prefix. On a localized MSVC that prefix does not match and header changes stop triggering rebuilds; the post-build check below will fail if that happens.'
} else {
    if (-not (Get-ExecutablePath 'nmake.exe')) { throw 'nmake.exe was not found in the MSVC environment.' }
}

$QtDir = Get-QtDirectory $QtDir
Assert-Ascii $QtDir 'Qt path'
if ([string]::IsNullOrWhiteSpace($CMakePath)) { $CMakePath = Get-ExecutablePath 'cmake.exe' }
if (-not $CMakePath) { throw 'cmake.exe was not found on PATH. Install CMake or start the script from the Qt Creator/Developer environment.' }

$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
$cameraOption = if ($ExperimentalCamera) { 'ON' } else { 'OFF' }
Write-Host "CMake: $CMakePath"
Write-Host "Qt: $QtDir"
Write-Host "Generator: $Generator"
Write-Host "Build: $BuildDir"

$configureArguments = @(
    '-S', $SourceDir, '-B', $BuildDir, '-G', $Generator,
    ("-DCMAKE_PREFIX_PATH={0}" -f $QtDir),
    ("-DCMAKE_BUILD_TYPE={0}" -f $Config),
    ("-DPCV_BUILD_TESTS={0}" -f $testOption)
    ("-DPCV_BUILD_EXPERIMENTAL_CAMERA={0}" -f $cameraOption)
)
if ($Generator -eq 'Ninja') {
    $configureArguments += ("-DCMAKE_MAKE_PROGRAM={0}" -f $NinjaPath)
}
if ($Generator -eq 'NMake Makefiles JOM') {
    $jomPath = Get-JomPath
    if (-not $jomPath) { throw 'jom.exe was not found. Install Qt Creator or pass -Generator "NMake Makefiles".' }
    $configureArguments += ("-DCMAKE_MAKE_PROGRAM={0}" -f $jomPath)
}
& $CMakePath @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$buildArguments = @('--build', $BuildDir, '--parallel')
if ($Clean) { $buildArguments += '--clean-first' }
& $CMakePath @buildArguments
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { exit $buildExitCode }

# Guard against the failure mode that produced a stale-object crash: when the
# build tree records no header dependencies, a later header-only change is
# silently ignored and objects built from different versions of the same
# struct get linked together. A healthy dependency database names headers.
$dependencyProblem = $null
if ($Generator -eq 'Ninja') {
    $ninjaDeps = Join-Path $BuildDir '.ninja_deps'
    if (-not (Test-Path -LiteralPath $ninjaDeps -PathType Leaf)) {
        $dependencyProblem = 'Ninja did not write .ninja_deps'
    } elseif (-not ([System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($ninjaDeps)) -match '\.h(pp)?\b')) {
        $dependencyProblem = '.ninja_deps records no header files (localized /showIncludes prefix)'
    }
} else {
    $dependencyFiles = @(Get-ChildItem -Path $BuildDir -Recurse -File -Include 'compiler_depend.make', 'depend.make', '*.obj.d' -ErrorAction SilentlyContinue)
    $withHeaders = $dependencyFiles | Where-Object { $_.Length -gt 0 -and (Select-String -LiteralPath $_.FullName -Pattern '\.h(pp)?$|\.h(pp)? ' -Quiet) }
    if (-not $withHeaders) {
        $dependencyProblem = 'no dependency file records a header'
    }
}
if ($dependencyProblem) {
    Write-Error ("Header dependency tracking is not working in {0} ({1}). A public header change will not rebuild its users, which links objects with mismatched struct layouts. Rebuild with -Clean and keep the Makefile generator so CMake's own scanner is used." -f $BuildDir, $dependencyProblem)
    exit 1
}
exit 0
