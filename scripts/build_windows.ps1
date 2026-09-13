param(
    [string]$QtDir = "",
    [string]$BuildDir = "",
    [string]$CMakePath = "",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$BuildTests,
    [switch]$CliOnly,
    [switch]$Deploy,
    [string]$DeployDir = "",
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

function Resolve-SafeDeployDirectory([string]$RequestedDirectory, [string]$SourceDirectory) {
    if ([string]::IsNullOrWhiteSpace($RequestedDirectory)) {
        throw '-DeployDir must not be empty.'
    }
    $full = [System.IO.Path]::GetFullPath($RequestedDirectory)
    $root = [System.IO.Path]::GetPathRoot($full)
    if ([string]::Equals($full.TrimEnd('\'), $root.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
        throw ('-DeployDir must not be a drive root: {0}' -f $full)
    }
    $source = [System.IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\')
    $candidate = $full.TrimEnd('\')
    $candidateInsideSource = [string]::Equals($candidate, $source, [System.StringComparison]::OrdinalIgnoreCase) -or
        $candidate.StartsWith($source + '\', [System.StringComparison]::OrdinalIgnoreCase)
    if ($candidateInsideSource) {
        throw ('-DeployDir must be outside the source tree: {0}' -f $full)
    }
    if (Test-Path -LiteralPath $full) {
        $resolved = (Resolve-Path -LiteralPath $full).Path.TrimEnd('\')
        $resolvedInsideSource = [string]::Equals($resolved, $source, [System.StringComparison]::OrdinalIgnoreCase) -or
            $resolved.StartsWith($source + '\', [System.StringComparison]::OrdinalIgnoreCase)
        if ($resolvedInsideSource) {
            throw ('-DeployDir resolves inside the source tree: {0}' -f $resolved)
        }
    }
    return $full
}

function Get-CliExecutablePath([string]$BuildDirectory, [string]$BuildConfiguration) {
    $candidates = @(
        (Join-Path $BuildDirectory 'pointcloudcli.exe'),
        (Join-Path (Join-Path $BuildDirectory $BuildConfiguration) 'pointcloudcli.exe'))
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw ('pointcloudcli.exe was not found under {0}. Build target pointcloudcli first.' -f $BuildDirectory)
}

function Get-DumpbinPath {
    $path = Get-ExecutablePath 'dumpbin.exe'
    if ($path) { return $path }
    $candidates = @()
    if ($env:VCToolsInstallDir) {
        $candidates += (Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe')
        $candidates += (Join-Path $env:VCToolsInstallDir 'bin\HostX64\x64\dumpbin.exe')
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'dumpbin.exe was not found in the MSVC environment; deployment dependency validation cannot continue.'
}

function Get-ImportedDllNames([string]$DumpbinPath, [string]$BinaryPath) {
    $lines = @(& $DumpbinPath /DEPENDENTS $BinaryPath 2>&1)
    if ($LASTEXITCODE -ne 0) { throw ('dumpbin /DEPENDENTS failed for {0}' -f $BinaryPath) }
    $dependencies = @()
    $inDependencies = $false
    foreach ($line in $lines) {
        $text = [string]$line
        if ($text -match 'dependencies:') { $inDependencies = $true; continue }
        if ($inDependencies -and $text -match '^\s*Summary\s*$') { break }
        if ($inDependencies -and $text -match '^\s{2,}([A-Za-z0-9_.-]+\.dll)\s*$') {
            $dependencies += $matches[1].ToLowerInvariant()
        }
    }
    return $dependencies | Select-Object -Unique
}

function Test-WindowsSystemDll([string]$Name) {
    if ($Name -match '^(api-ms-win-|ext-ms-win-)') { return $true }
    $systemRoots = @(
        (Join-Path $env:SystemRoot 'System32'),
        (Join-Path $env:SystemRoot 'SysWOW64')) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }
    foreach ($root in $systemRoots) {
        if (Test-Path -LiteralPath (Join-Path $root $Name) -PathType Leaf) { return $true }
    }
    return $false
}

function Assert-PortableDependencies([string]$DeployDirectory) {
    $dumpbin = Get-DumpbinPath
    $binaries = @(Get-ChildItem -LiteralPath $DeployDirectory -Recurse -File -Include '*.exe', '*.dll')
    foreach ($binary in $binaries) {
        foreach ($dependency in Get-ImportedDllNames $dumpbin $binary.FullName) {
            $localMatches = @(Get-ChildItem -LiteralPath $DeployDirectory -Recurse -File -Filter $dependency -ErrorAction SilentlyContinue)
            if (($localMatches.Count -gt 0) -or (Test-WindowsSystemDll $dependency)) { continue }
            throw ('Portable dependency is missing: {0} imported by {1}' -f $dependency, $binary.FullName)
        }
    }
}

function Invoke-PortableCliTests([string]$CMakeExecutable, [string]$DeployDirectory, [string]$ProjectSourceDirectory, [string]$ProjectVersion) {
    $testRoot = Join-Path $env:TEMP ('pointcloudcli-portable-tests-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $invocationScript = Join-Path $ProjectSourceDirectory 'apps\pointcloudcli\tests\verify_cli_invocation.cmake'
    $jobScript = Join-Path $ProjectSourceDirectory 'apps\pointcloudcli\tests\verify_cli_job.cmake'
    $plyScript = Join-Path $ProjectSourceDirectory 'apps\pointcloudcli\tests\verify_cli_ply.cmake'
    $cliPath = Join-Path $DeployDirectory 'pointcloudcli.exe'
    $modes = @(
        @{ Script = $invocationScript; Mode = 'help'; Version = $true },
        @{ Script = $invocationScript; Mode = 'version'; Version = $true },
        @{ Script = $invocationScript; Mode = 'empty'; Version = $true },
        @{ Script = $invocationScript; Mode = 'unknown'; Version = $true },
        @{ Script = $jobScript; Mode = 'rectangle'; Version = $false },
        @{ Script = $jobScript; Mode = 'indices'; Version = $false },
        @{ Script = $jobScript; Mode = 'overwrite'; Version = $false },
        @{ Script = $jobScript; Mode = 'traversal'; Version = $false },
        @{ Script = $plyScript; Mode = 'inspect_ascii'; Version = $false },
        @{ Script = $plyScript; Mode = 'inspect_binary_le'; Version = $false },
        @{ Script = $plyScript; Mode = 'inspect_binary_be'; Version = $false },
        @{ Script = $plyScript; Mode = 'inspect_missing'; Version = $false },
        @{ Script = $plyScript; Mode = 'read_summary'; Version = $false },
        @{ Script = $plyScript; Mode = 'read_invalid'; Version = $false },
        @{ Script = $plyScript; Mode = 'read_bounds'; Version = $false })
    $oldPath = $env:PATH
    Push-Location $testRoot
    try {
        # Keep the host PATH empty. Portable CMake scripts inject only the
        # package directory into their child process environment because
        # Windows CMake execute_process requires a searchable PATH.
        $env:PATH = ''
        foreach ($item in $modes) {
            $arguments = @("-DPCV_CLI=$cliPath", "-DPCV_MODE=$($item.Mode)")
            if ($item.Version) { $arguments += "-DPCV_PROJECT_VERSION=$ProjectVersion" }
            $arguments += @('-DPCV_PORTABLE=ON', '-P', $item.Script)
            & $CMakeExecutable @arguments
            if ($LASTEXITCODE -ne 0) {
                throw ('Portable CLI test failed: {0}' -f $item.Mode)
            }
        }
    } finally {
        $env:PATH = $oldPath
        Pop-Location
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Deploy-PointCloudCli([string]$BuildDirectory, [string]$BuildConfiguration, [string]$QtPrefix, [string]$CMakeExecutable, [string]$SourceDirectory, [string]$TargetDirectory, [string]$ProjectVersion) {
    $deploy = Resolve-SafeDeployDirectory $TargetDirectory $SourceDirectory
    if (Test-Path -LiteralPath $deploy -PathType Leaf) { throw ('-DeployDir is an existing file: {0}' -f $deploy) }
    if (Test-Path -LiteralPath $deploy) { Remove-Item -LiteralPath $deploy -Recurse -Force }
    New-Item -ItemType Directory -Path $deploy -Force | Out-Null
    $exe = Get-CliExecutablePath $BuildDirectory $BuildConfiguration
    Copy-Item -LiteralPath $exe -Destination (Join-Path $deploy 'pointcloudcli.exe') -Force
    $windeployqt = Join-Path $QtPrefix 'bin\windeployqt.exe'
    if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
        throw ('windeployqt.exe was not found under {0}' -f $QtPrefix)
    }
    $deployArguments = @('--release', '--compiler-runtime', '--no-translations',
        '--no-system-d3d-compiler', '--no-opengl-sw', '--dir', $deploy,
        (Join-Path $deploy 'pointcloudcli.exe'))
    & $windeployqt @deployArguments
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt deployment failed.' }
    if ($env:VCToolsRedistDir) {
        $crtFiles = @(Get-ChildItem -LiteralPath $env:VCToolsRedistDir -Recurse -File -Include 'vcruntime*.dll', 'msvcp*.dll', 'concrt*.dll' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' })
        foreach ($crt in $crtFiles) {
            $destination = Join-Path $deploy $crt.Name
            if (-not (Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $crt.FullName -Destination $destination -Force }
        }
    }
    Get-ChildItem -LiteralPath $deploy -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(concrt|msvcp|vcruntime|ucrtbase).*d(?:_|\.dll)$' } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path (Join-Path $deploy 'data') -Force | Out-Null
    Assert-PortableDependencies $deploy
    $readme = @"
PointCloudSuite pointcloudcli portable package
Version: $ProjectVersion

Usage:
  pointcloudcli.exe --version
  pointcloudcli.exe inspect ply input.ply --json
  pointcloudcli.exe read ply input.ply --json
  pointcloudcli.exe temp-workpiece run job.json --data-dir .\data

This package is the CLI only. Input Job JSON, PLY and calibration files are not included.
"@
    Set-Content -LiteralPath (Join-Path $deploy 'README.txt') -Value $readme -Encoding UTF8
    Get-ChildItem -LiteralPath $deploy -Recurse -File | Unblock-File -ErrorAction SilentlyContinue
    Invoke-PortableCliTests $CMakeExecutable $deploy $SourceDirectory $ProjectVersion
    $files = @(Get-ChildItem -LiteralPath $deploy -Recurse -File)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    Write-Host ('Portable package: {0} files, {1:N0} bytes, {2}' -f $files.Count, $bytes, $deploy)
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($Deploy -and -not $CliOnly) { throw '-Deploy requires -CliOnly.' }
if ($CliOnly -and $ExperimentalCamera) { throw '-CliOnly cannot be combined with -ExperimentalCamera.' }
if ($Deploy -and $Config -ne 'Release') { throw '-Deploy is supported only with -Config Release.' }
if ($Deploy -and $PSBoundParameters.ContainsKey('DeployDir') -and [string]::IsNullOrWhiteSpace($DeployDir)) {
    throw '-DeployDir must not be empty when specified.'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path (Split-Path $SourceDir -Parent) ("build-PointCloudSuite-{0}" -f $Config)
}
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
if ($CliOnly -and $QtDir -match '(?i)(^|[\\/])mingw') {
    throw ('-CliOnly requires an MSVC Qt kit; MinGW Qt was selected: {0}' -f $QtDir)
}
$CMakePath = Get-CMakePath $CMakePath

$testOption = if ($BuildTests) { 'ON' } else { 'OFF' }
$cameraOption = if ($ExperimentalCamera) { 'ON' } else { 'OFF' }
Write-Host "CMake: $CMakePath"
Write-Host "Qt: $QtDir"
Write-Host "Generator: $Generator"
Write-Host "Build: $BuildDir"
Write-Host "CliOnly: $CliOnly"
Write-Host "Deploy: $Deploy"
Write-Host "BuildTests: $BuildTests"
Write-Host "ExperimentalCamera: $ExperimentalCamera"

$configureArguments = @(
    '-S', $SourceDir, '-B', $BuildDir, '-G', $Generator,
    ("-DCMAKE_PREFIX_PATH={0}" -f $QtDir),
    ("-DCMAKE_BUILD_TYPE={0}" -f $Config),
    ("-DPCV_BUILD_TESTS={0}" -f $testOption),
    ("-DPCV_BUILD_EXPERIMENTAL_CAMERA={0}" -f $cameraOption)
)
if ($CliOnly) {
    $configureArguments += @('-DPCV_BUILD_POINTCLOUDVIEW=OFF',
        '-DPCV_BUILD_POINTCLOUDSTITCH=OFF', '-DPCV_BUILD_CLI=ON')
}
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

$cacheVersion = Join-Path $BuildDir 'CMakeCache.txt'
$projectVersion = 'unknown'
if (Test-Path -LiteralPath $cacheVersion -PathType Leaf) {
    $versionLine = Select-String -LiteralPath $cacheVersion -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.*)$' | Select-Object -First 1
    if ($versionLine) { $projectVersion = $versionLine.Matches[0].Groups[1].Value.Trim() }
}

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
if ($Deploy) {
    if ([string]::IsNullOrWhiteSpace($DeployDir)) {
        $DeployDir = Join-Path (Split-Path $BuildDir -Parent) 'pointcloudcli-portable-Release'
    }
    if ($projectVersion -eq 'unknown') { throw 'CMAKE_PROJECT_VERSION was not found in CMakeCache.txt after configuration.' }
    Deploy-PointCloudCli $BuildDir $Config $QtDir $CMakePath $SourceDir $DeployDir $projectVersion
}
exit 0
