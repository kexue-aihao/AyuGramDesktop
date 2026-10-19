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
$processEnded = $false

try {
    do {
        $finished = $process.WaitForExit($HeartbeatSeconds * 1000)
        foreach ($reader in $readers) {
            $output = $reader.ReadToEnd()
            if ($output) { Write-Host $output -NoNewline }
        }
        $elapsed = (Get-Date) - $started
        $compilers = @(Get-Process -Name cl -ErrorAction SilentlyContinue)
        Write-Host ("Build heartbeat: {0}; compiler processes: {1}" -f `
            $elapsed.ToString('hh\:mm\:ss'), $compilers.Count)
    } while (-not $finished)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $processEnded = $true
    foreach ($reader in $readers) {
        $output = $reader.ReadToEnd()
        if ($output) { Write-Host $output -NoNewline }
    }
} finally {
    if (-not $processEnded -and -not $process.HasExited) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    foreach ($reader in $readers) { $reader.Dispose() }
    $process.Dispose()
}

if ($exitCode -ne 0) {
    throw "Build failed with exit code $exitCode. Full logs: $LogPath, $errorPath"
}
