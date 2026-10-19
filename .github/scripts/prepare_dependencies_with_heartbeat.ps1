param(
    [Parameter(Mandatory = $true)]
    [string] $PreparePath,
    [Parameter(Mandatory = $true)]
    [string] $LogPath,
    [int] $HeartbeatSeconds = 60
)

$ErrorActionPreference = 'Stop'
$PreparePath = [System.IO.Path]::GetFullPath($PreparePath)
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
$wrapperPath = Join-Path $env:RUNNER_TEMP 'run_prepare_dependencies.cmd'
$wrapper = "@echo off`r`ncall `"$PreparePath`" silent > `"$LogPath`" 2>&1`r`nexit /b %errorlevel%`r`n"
[System.IO.File]::WriteAllText($wrapperPath, $wrapper, [System.Text.Encoding]::ASCII)
Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue

$started = Get-Date
$process = Start-Process -FilePath $env:ComSpec `
    -ArgumentList @('/d', '/c', "`"$wrapperPath`"") `
    -WorkingDirectory $env:GITHUB_WORKSPACE `
    -PassThru `
    -WindowStyle Hidden

try {
    while (-not $process.WaitForExit($HeartbeatSeconds * 1000)) {
        $elapsed = (Get-Date) - $started
        $lines = if (Test-Path -LiteralPath $LogPath) {
            Get-Content -LiteralPath $LogPath -ErrorAction SilentlyContinue
        } else {
            @()
        }
        $stage = $lines | Where-Object { $_ -match '\[[0-9]+/[0-9]+\]\(' } | Select-Object -Last 1
        if (-not $stage) {
            $stage = 'waiting for the first dependency stage'
        }
        Write-Host ('Prepare dependencies heartbeat: {0}; latest stage: {1}' -f $elapsed.ToString('hh\:mm\:ss'), $stage)
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
} finally {
    Remove-Item -LiteralPath $wrapperPath -Force -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath $LogPath) {
    Get-Content -LiteralPath $LogPath | ForEach-Object { Write-Host $_ }
}
if ($exitCode -ne 0) {
    throw "Dependency preparation failed with exit code $exitCode. Full log: $LogPath"
}
