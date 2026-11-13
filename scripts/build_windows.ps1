param(
    [string]$QtDir = "",
    [string]$BuildDir = "",
    [string]$CMakePath = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$BuildTests,
    [switch]$BuildRawConverter,
    [switch]$ExperimentalTests,
    [ValidateSet('pointcloudview', 'scan_geometry_demo_v1', 'step_template_builder_v1')]
    [AllowEmptyCollection()]
    [string[]]$Gui = @('pointcloudview', 'scan_geometry_demo_v1', 'step_template_builder_v1'),
    [switch]$StepImport = $true,
    [string]$OcctDir = "",
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

function Get-ExecutablePath([string]$Name) {
    $command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($command -and $command.Source -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-VisualStudioInstallationPaths {
    $vswhereCandidates = @(
        (Get-ExecutablePath 'vswhere.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique
    foreach ($vswherePath in $vswhereCandidates) {
        @(& $vswherePath -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null) |
            Where-Object { $_ -and $_.Trim() } |
            ForEach-Object { (Resolve-Path -LiteralPath $_.Trim()).Path }
    }
}

function Get-CMakePath([string]$RequestedCMakePath) {
    $candidates = @()
    if ($RequestedCMakePath) { $candidates += $RequestedCMakePath }
    $pathCMake = Get-ExecutablePath 'cmake.exe'
    if ($pathCMake) { $candidates += $pathCMake }
    $candidates += (Join-Path $env:SystemDrive 'Qt\Tools\CMake_64\bin\cmake.exe')
    foreach ($installationPath in Get-VisualStudioInstallationPaths) {
        $candidates += (Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
        $candidates += (Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\cmake.exe')
    }
    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw ('cmake.exe was not found. Probed: {0}. Install CMake or pass -CMakePath <cmake.exe>.' -f (($candidates | Select-Object -Unique) -join '; '))
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
    $qtRoot = Join-Path $env:SystemDrive 'Qt'
    if (Test-Path -LiteralPath $qtRoot -PathType Container) {
        $qtVersions = Get-ChildItem -LiteralPath $qtRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
            Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending
        foreach ($version in $qtVersions) {
            Get-ChildItem -LiteralPath $version.FullName -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^msvc.*_64$' } |
                ForEach-Object { $candidates += $_.FullName }
        }
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
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
if ($BuildDir -eq $SourceDir -or $BuildDir.StartsWith($SourceDir + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDir must be outside the source tree.'
}
if ([string]::IsNullOrWhiteSpace($Generator)) { $Generator = 'NMake Makefiles' }
$existingCache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $existingCache -PathType Leaf) {
    $cacheValues = @{}
    foreach ($cacheLine in Get-Content -LiteralPath $existingCache) {
        if ($cacheLine -match '^(CMAKE_HOME_DIRECTORY|CMAKE_GENERATOR|CMAKE_BUILD_TYPE):[^=]+=(.*)$') {
            $cacheValues[$matches[1]] = $matches[2]
        }
    }
    $cachedSource = $cacheValues['CMAKE_HOME_DIRECTORY']
    if (-not $cachedSource -or [System.IO.Path]::GetFullPath($cachedSource).TrimEnd('\', '/') -ne $SourceDir -or
        $cacheValues['CMAKE_GENERATOR'] -ne $Generator -or $cacheValues['CMAKE_BUILD_TYPE'] -ne $Config) {
        throw 'Existing CMake cache has a different source, generator or configuration. Choose a new -BuildDir; the old directory was preserved.'
    }
}
if ($StepImport) {
    if (-not $OcctDir) { $OcctDir = $env:PCV_OCCT_DIR }
    if (-not $OcctDir) { $OcctDir = $env:OpenCASCADE_DIR }
    if (-not $OcctDir) {
        foreach ($prefixPath in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if ($prefixPath -and (Test-Path -LiteralPath (Join-Path $prefixPath 'share/opencascade/OpenCASCADEConfig.cmake'))) {
                $OcctDir = Join-Path $prefixPath 'share/opencascade'; break
            }
        }
    }
    if (-not $OcctDir -or -not (Test-Path -LiteralPath (Join-Path $OcctDir 'OpenCASCADEConfig.cmake') -PathType Leaf)) {
        throw 'STEP is enabled. Pass -OcctDir <OCCT-7.9.3-config-directory>, set PCV_OCCT_DIR, or explicitly use -StepImport:$false.'
    }
    $OcctDir = (Resolve-Path -LiteralPath $OcctDir).Path
}
if (-not (Get-ExecutablePath 'cl.exe')) { Import-VcVarsEnvironment (Get-VcVars64Path) }
$env:VSLANG = '1033'
if (-not (Get-ExecutablePath 'cl.exe')) { throw 'MSVC x64 tools were not initialized.' }
$NinjaPath = Get-NinjaPath
if ($Generator -eq 'NMake Makefiles JOM') { throw 'NMake Makefiles JOM is unsupported. Use -Generator "NMake Makefiles".' }
if ($Generator -eq 'Ninja' -and -not $NinjaPath) { throw 'ninja.exe was not found.' }
if ($Generator -ne 'Ninja' -and -not (Get-ExecutablePath 'nmake.exe')) { throw 'nmake.exe was not found in the MSVC environment.' }
$QtDir = Get-QtDirectory $QtDir
$CMakePath = Get-CMakePath $CMakePath
$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
$cameraOption = if ($ExperimentalCamera) { 'ON' } else { 'OFF' }
$configureArguments = @(
    '-S', $SourceDir, '-B', $BuildDir, '-G', $Generator,
    ("-DCMAKE_PREFIX_PATH={0}" -f $(if ($OcctDir) { "$QtDir;$(Split-Path (Split-Path $OcctDir -Parent) -Parent)" } else { $QtDir })),
    ("-DCMAKE_BUILD_TYPE={0}" -f $Config),
    ("-DPCV_BUILD_TESTS={0}" -f $testOption),
    ("-DPCV_BUILD_STEP_IMPORT={0}" -f $(if ($StepImport) { 'ON' } else { 'OFF' })),
    ("-DPCV_BUILD_EXPERIMENTAL_CAMERA={0}" -f $cameraOption),
    ("-DPCV_BUILD_RAW_CONVERTER={0}" -f $(if ($BuildRawConverter) { 'ON' } else { 'OFF' })),
    ("-DPCV_BUILD_EXPERIMENTAL_TESTS={0}" -f $(if ($ExperimentalTests) { 'ON' } else { 'OFF' }))
)
if ($OcctDir) { $configureArguments += ("-DOpenCASCADE_DIR={0}" -f $OcctDir) }
foreach ($guiEntry in @('pointcloudview', 'scan_geometry_demo_v1', 'step_template_builder_v1')) {
    $guiEnabled = if ($guiEntry -in $Gui) { 'ON' } else { 'OFF' }
    $optionName = $guiEntry.ToUpperInvariant()
    $configureArguments += ("-DPCV_BUILD_{0}={1}" -f $optionName, $guiEnabled)
}
if ($Generator -eq 'Ninja') { $configureArguments += ("-DCMAKE_MAKE_PROGRAM={0}" -f $NinjaPath) }
& $CMakePath @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$buildArguments = @('--build', $BuildDir)
if ($Generator -eq 'Ninja') { $buildArguments += '--parallel' }
if ($Clean) { $buildArguments += '--clean-first' }
& $CMakePath @buildArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$dependencyProblem = $null
if ($Generator -eq 'Ninja') {
    $ninjaDeps = Join-Path $BuildDir '.ninja_deps'
    if (-not (Test-Path -LiteralPath $ninjaDeps -PathType Leaf)) { $dependencyProblem = 'Ninja did not write .ninja_deps' }
    elseif (-not ([System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($ninjaDeps)) -match '\.h(pp)?\b')) {
        $dependencyProblem = '.ninja_deps records no header files'
    }
} else {
    $dependencyFiles = @(Get-ChildItem -Path $BuildDir -Recurse -File -Include 'compiler_depend.make', 'depend.make', '*.obj.d' -ErrorAction SilentlyContinue)
    $withHeaders = $dependencyFiles | Where-Object { $_.Length -gt 0 -and (Select-String -LiteralPath $_.FullName -Pattern '\.h(pp)?$|\.h(pp)? ' -Quiet) }
    if (-not $withHeaders) { $dependencyProblem = 'no dependency file records a header' }
}
if ($dependencyProblem) {
    Write-Error ("Header dependency tracking is not working in {0} ({1}). Rebuild with -Clean." -f $BuildDir, $dependencyProblem)
    exit 1
}
exit 0
