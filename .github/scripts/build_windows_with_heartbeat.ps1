param(
    [Parameter(Mandatory = $true)]
    [string] $BuildPath,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 16)]
    [int] $Parallel,
    [Parameter(Mandatory = $true)]
    [string] $LogPath,
    [ValidateRange(1, 60)]
    [int] $HeartbeatSeconds = 60
)

$ErrorActionPreference = 'Stop'
$build = (Get-Item -LiteralPath $BuildPath).FullName
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
$errorPath = "$LogPath.stderr"
$started = Get-Date
$cmake = Get-Command cmake -CommandType Application | Select-Object -First 1
if (-not $cmake) { throw 'cmake was not found on PATH.' }
$process = Start-Process -FilePath $cmake.Source `
    -ArgumentList @('--build', "`"$build`"", '--config', 'Release', '--target', 'Telegram', '--parallel', $Parallel) `
    -RedirectStandardOutput $LogPath `
    -RedirectStandardError $errorPath `
    -PassThru `
    -WindowStyle Hidden
$readers = @($LogPath, $errorPath) | ForEach-Object {
    $stream = [System.IO.File]::Open($_, 'Open', 'Read', 'ReadWrite')
    [System.IO.StreamReader]::new($stream)
}

try {
    do {
        $finished = $process.WaitForExit($HeartbeatSeconds * 1000)
        foreach ($reader in $readers) {
            $output = $reader.ReadToEnd()
            if ($output) { Write-Host $output -NoNewline }
        }
        $elapsed = (Get-Date) - $started
        $compilers = @(Get-Process -Name cl -ErrorAction SilentlyContinue)
        $memory = Get-CimInstance Win32_OperatingSystem
        $freeGiB = [Math]::Round($memory.FreePhysicalMemory / 1MB, 1)
        $objects = @(Get-ChildItem -LiteralPath $build -Filter '*.obj' -File -Recurse -ErrorAction SilentlyContinue).Count
        Write-Host ("Build heartbeat: {0}; objects: {1}; compiler processes: {2}; free RAM: {3} GiB" -f `
            $elapsed.ToString('hh\:mm\:ss'), $objects, $compilers.Count, $freeGiB)
    } while (-not $finished)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    foreach ($reader in $readers) {
        $output = $reader.ReadToEnd()
        if ($output) { Write-Host $output -NoNewline }
    }
} finally {
    foreach ($reader in $readers) { $reader.Dispose() }
    $process.Dispose()
}

if ($exitCode -ne 0) {
    throw "Build failed with exit code $exitCode. Full logs: $LogPath, $errorPath"
}
