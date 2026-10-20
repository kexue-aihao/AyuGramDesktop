param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x64', 'x86')]
    [string] $Architecture,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v[0-9]{8}$')]
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
    $stream = [System.IO.File]::OpenRead($file)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid DOS header: $file" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt $stream.Length - 6) {
            throw "Invalid PE offset: $file"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE header: $file" }
        $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x014C }
        if ($reader.ReadUInt16() -ne $expectedMachine) {
            throw "The binary does not match Windows ${Architecture}: $file"
        }
    } finally {
        $reader.Dispose()
    }
}

$dist = Join-Path (Split-Path $repository -Parent) 'dist'
$folderName = "Ayugram-Desktop-v$($ReleaseTag.Substring(1))-Windows-$Architecture"
$staging = Join-Path $dist $folderName
$archive = Join-Path $dist "$folderName.zip"
if ((Test-Path -LiteralPath $staging) -or (Test-Path -LiteralPath $archive)) {
    throw "Package output already exists: $staging"
}
New-Item -ItemType Directory -Path (Join-Path $staging 'TelegramForcePortable') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "modules/$Architecture/d3d") -Force | Out-Null
Set-Content -LiteralPath (Join-Path $staging 'TelegramForcePortable/keep') -Value '' -Encoding utf8NoBOM
Copy-Item -LiteralPath $binary -Destination $staging
Copy-Item -LiteralPath $compiler -Destination (Join-Path $staging "modules/$Architecture/d3d")
foreach ($name in @('LICENSE', 'LEGAL')) {
    Copy-Item -LiteralPath (Join-Path $repository $name) -Destination $staging
}
$commit = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
$details = @(
    "Release: Ayugram Desktop v$($ReleaseTag.Substring(1))",
    "Architecture: Windows $Architecture",
    "Source commit: $commit",
    'Run AyuGram.exe. Account data is stored in TelegramForcePortable beside the executable.',
    'This self-build is unsigned. Automatic updates are disabled.'
)
Set-Content -LiteralPath (Join-Path $staging 'BUILD.txt') -Value $details -Encoding utf8NoBOM
Push-Location $dist
try {
    & 7z a -tzip -mx=9 $archive $folderName
    if ($LASTEXITCODE -ne 0) { throw "ZIP creation failed: $LASTEXITCODE" }
    & 7z t $archive
    if ($LASTEXITCODE -ne 0) { throw "ZIP verification failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    "$archive.sha256",
    "$checksum  $folderName.zip`n",
    [System.Text.UTF8Encoding]::new($false)
)
