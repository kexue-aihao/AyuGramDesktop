<#
.SYNOPSIS
Builds the AyuGram Desktop Windows x64 release locally.

.DESCRIPTION
Uses the same configuration as .github/workflows/windows-release.yml:
MSVC 14.44 with the 10.0.26100.0 SDK, Release only, no LTO, no automatic
updates and no crash reports.

Dependencies must already be prepared in the parent directory of the
repository, exactly as Telegram/build/prepare/win.bat lays them out:

    <parent>\Libraries\win64
    <parent>\ThirdParty

.PARAMETER ReleaseTag
Optional vYYYYMMDD tag. When set, the portable ZIP and, if Inno Setup is
installed, the official setup.exe are built through the existing
Telegram/build/package_windows*.ps1 scripts.

.PARAMETER Parallel
Number of MSBuild project builds. Defaults to half of the logical processors;
every compiler process uses /MP2, so the default keeps roughly one compile
process per hardware thread.

.PARAMETER ApiId
Telegram API ID. Defaults to the official key from docs/api_credentials.md.

.PARAMETER ApiHash
Telegram API hash. Defaults to the official key from docs/api_credentials.md.

.PARAMETER SkipConfigure
Reuse the existing out directory without running configure.bat.

.PARAMETER SkipPackage
Build only, even when -ReleaseTag is set.

.PARAMETER ValidateOnly
Initialize the toolchain, resolve Python and check the dependencies, then
exit without configuring or building.

.EXAMPLE
.\tools\build_windows_release.ps1

Builds out\Release\AyuGram.exe without packaging.

.EXAMPLE
.\tools\build_windows_release.ps1 -ReleaseTag v20261020

Builds the release and packages it into the dist directory next to the
repository.
#>
[CmdletBinding()]
param(
    [ValidatePattern('^v[0-9]{8}$')]
    [string] $ReleaseTag,

    [ValidateRange(0, 64)]
    [int] $Parallel = 0,

    [ValidatePattern('^[0-9]+$')]
    [string] $ApiId = '2040',

    [ValidatePattern('^[a-fA-F0-9]{32}$')]
    [string] $ApiHash = 'b18441a1ff607e10a989891a5462e627',

    [switch] $SkipConfigure,
    [switch] $SkipPackage,
    [switch] $ValidateOnly
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = Split-Path $repo -Parent
$libraries = Join-Path $buildPath 'Libraries\win64'
$thirdParty = Join-Path $buildPath 'ThirdParty'
$out = Join-Path $repo 'out'
$binary = Join-Path $out 'Release\AyuGram.exe'

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio 2022 Build Tools with the C++ workload.'
    }
    $install = @(
        & $vswhere -latest -products '*' -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationPath
    ) | Where-Object { $_ } | Select-Object -First 1
    if (-not $install) {
        throw 'No Visual Studio installation with the C++ build tools was found.'
    }
    return $install
}

function Initialize-VsEnvironment {
    param([Parameter(Mandatory = $true)] [string] $InstallPath)

    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and
        (Get-Command cmake.exe -ErrorAction SilentlyContinue) -and
        $env:VCToolsInstallDir) {
        Write-Host "Using the active Visual Studio environment: $env:VCToolsInstallDir"
        return
    }

    $devShell = Join-Path $InstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShell)) {
        throw "Microsoft.VisualStudio.DevShell.dll was not found at $devShell."
    }
    $installerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    if (Test-Path -LiteralPath $installerDir) {
        $env:PATH = $installerDir + ';' + $env:PATH
    }
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $InstallPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.26100.0'

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'cl.exe is unavailable after initializing the Visual Studio environment.'
    }
    if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
        throw 'cmake.exe is unavailable after initializing the Visual Studio environment.'
    }
}

function Test-Python {
    param([string] $Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    try {
        $output = & $Path --version 2>&1
        return ($LASTEXITCODE -eq 0) -and ("$output" -match '^Python 3\.')
    } catch {
        return $false
    }
}

function Resolve-Python {
    $candidates = @()
    if ($env:AYUGRAM_PYTHON) {
        $candidates += $env:AYUGRAM_PYTHON
    }
    $candidates += (Join-Path $thirdParty 'python\Scripts\python.exe')
    foreach ($name in @('python.exe', 'python3.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) {
            $candidates += $command.Source
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Python $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    throw 'A working Python 3 was not found. Install Python 3.10 or set AYUGRAM_PYTHON to python.exe.'
}

function Test-Dependencies {
    foreach ($path in @($libraries, $thirdParty)) {
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Missing dependency directory: $path. Run Telegram\build\prepare\win.bat silent once from an x64 Native Tools prompt."
        }
    }
    $qt = @(Get-ChildItem -LiteralPath $libraries -Directory -Filter 'Qt-*' -ErrorAction SilentlyContinue)
    if ($qt.Count -eq 0) {
        throw "No Qt installation was found in $libraries. Prepare the dependencies first."
    }
}

function Invoke-Configure {
    $configure = Join-Path $repo 'Telegram\configure.bat'
    $arguments = @(
        'x64',
        "-DTDESKTOP_API_ID=$ApiId",
        "-DTDESKTOP_API_HASH=$ApiHash",
        '-DCMAKE_CONFIGURATION_TYPES=Release',
        '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded',
        '-DCMAKE_C_FLAGS=/utf-8',
        '-DCMAKE_CXX_FLAGS=/utf-8',
        '-DDESKTOP_APP_SPECIAL_TARGET=win64',
        '-DDESKTOP_APP_ENABLE_LTO=OFF',
        '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON',
        '-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON'
    )
    Write-Host 'Configuring Release...'
    Push-Location (Join-Path $repo 'Telegram')
    try {
        & $configure @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "configure.bat failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}

function Invoke-Build {
    param([Parameter(Mandatory = $true)] [int] $ParallelCount)

    $previousCl = $env:_CL_
    $started = Get-Date
    try {
        $env:_CL_ = '/MP2 /FS'
        Write-Host ("Building Release with MSBuild /m:{0} and cl /MP2..." -f $ParallelCount)
        & cmake --build $out --config Release --target Telegram --parallel $ParallelCount | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE."
        }
    } finally {
        if ($null -eq $previousCl) {
            Remove-Item Env:_CL_ -ErrorAction SilentlyContinue
        } else {
            $env:_CL_ = $previousCl
        }
    }
    return (Get-Date) - $started
}

function Invoke-Package {
    param([Parameter(Mandatory = $true)] [string] $Tag)

    & (Join-Path $repo 'Telegram\build\package_windows.ps1') `
        -Architecture 'x64' -ReleaseTag $Tag

    $iscc = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 5\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 5\ISCC.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1

    if ($iscc) {
        & (Join-Path $repo 'Telegram\build\package_windows_installer.ps1') `
            -Architecture 'x64' -ReleaseTag $Tag
    } else {
        Write-Warning 'Inno Setup (ISCC.exe) was not found; setup.exe was not built.'
    }
}

$vsInstall = Find-VisualStudio
Initialize-VsEnvironment -InstallPath $vsInstall
$targetArch = if ($env:VSCMD_ARG_TGT_ARCH) { $env:VSCMD_ARG_TGT_ARCH } else { $env:Platform }
if ($targetArch -ne 'x64') {
    throw "Unexpected target platform: $targetArch. Run from the x64 Native Tools environment."
}
if ($env:VCToolsInstallDir -notmatch '\\14\.44\.') {
    throw "MSVC 14.44 is required, but $env:VCToolsInstallDir was selected."
}
$atl = Join-Path $env:VCToolsInstallDir 'atlmfc\include\atlbase.h'
if (-not (Test-Path -LiteralPath $atl)) {
    throw "The selected MSVC 14.44 toolset has no ATL header at $atl."
}

$python = Resolve-Python
$env:PATH = (Split-Path $python -Parent) + ';' + $env:PATH
Write-Host "Python: $python"
Test-Dependencies

Write-Host "Repository: $repo"
Write-Host "Dependencies: $libraries"
Write-Host "Build directory: $out"

if ($ValidateOnly) {
    Write-Host 'Validation finished; nothing was configured or built.'
    exit 0
}

if (-not $SkipConfigure) {
    Invoke-Configure
}

$parallelCount = if ($Parallel -gt 0) {
    $Parallel
} else {
    [Math]::Max(1, [int][Math]::Floor([Environment]::ProcessorCount / 2))
}
Write-Host ("MSBuild parallel projects: {0}" -f $parallelCount)
$elapsed = Invoke-Build -ParallelCount $parallelCount

if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "The build finished without producing $binary."
}
Write-Host ''
Write-Host ("Release binary: {0}" -f $binary)
Write-Host ("Build time: {0:hh\:mm\:ss}" -f $elapsed)

if ($ReleaseTag -and -not $SkipPackage) {
    Invoke-Package -Tag $ReleaseTag
    $dist = Join-Path $buildPath 'dist'
    Write-Host ("Packages: {0}" -f $dist)
    Get-ChildItem -LiteralPath $dist -File |
        Where-Object { $_.Name -like "*$($ReleaseTag.Substring(1))*Windows-x64*" } |
        ForEach-Object { Write-Host ("  {0} ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB)) }
}
