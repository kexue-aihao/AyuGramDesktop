param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x64', 'x86')]
    [string] $Architecture,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^base-[0-9]{8}$')]
    [string] $ReleaseTag
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $repository 'out/Release'
$binary = Join-Path $output 'AyuGram.exe'
$compiler = Join-Path $output "modules/$Architecture/d3d/d3dcompiler_47.dll"
foreach ($file in @($binary, $compiler)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Missing release file: $file"
    }
}

$dist = Join-Path (Split-Path $repository -Parent) 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$releaseVersion = $ReleaseTag.Substring(5)
$versionLine = Get-Content (Join-Path $repository 'Telegram/build/version') |
    Where-Object { $_ -match '^AppVersionStr\s+' } |
    Select-Object -First 1
if ($versionLine -notmatch '^AppVersionStr\s+(\S+)') {
    throw 'AppVersionStr was not found in Telegram/build/version.'
}
$version = $Matches[1]
$name = "Ayugram-Desktop-Base-$releaseVersion-Windows-$Architecture-setup"
$installer = Join-Path $dist "$name.exe"
$iss = Join-Path $repository 'Telegram/build/setup_ayugram.iss'

$iscc = $null
$candidates = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6/ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 5/ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 5/ISCC.exe')
)
foreach ($candidate in $candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        $iscc = $candidate
        break
    }
}
if (-not $iscc) {
    throw 'Inno Setup compiler (ISCC.exe) was not found.'
}

$buildTarget = if ($Architecture -eq 'x64') { 'win64' } else { 'win' }
$arguments = @(
    '/Qp',
    "/dMyAppVersion=$version",
    "/dOutputDir=$dist",
    "/dReleasePath=$output",
    "/dMyBuildTarget=$buildTarget",
    "/dMyOutputBaseFilename=$name",
    $iss
)
& $iscc @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed: $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer was not created: $installer"
}

$checksum = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    "$installer.sha256",
    "$checksum  $name.exe`n",
    [System.Text.UTF8Encoding]::new($false)
)
