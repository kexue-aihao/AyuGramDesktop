param(
    [Parameter(Mandatory = $true)]
    [string] $BuildPath,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 16)]
    [int] $Parallel,
    [Parameter(Mandatory = $true)]
    [string] $LogPath,
    [ValidateRange(1, 60)]
    [int] $HeartbeatSeconds = 60,
    [ValidateRange(128, 16384)]
    [int] $CommitFloorMB = 1024
)

$ErrorActionPreference = 'Stop'
$build = (Get-Item -LiteralPath $BuildPath).FullName
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
$started = Get-Date
$cmake = Get-Command cmake -CommandType Application | Select-Object -First 1
if (-not $cmake) { throw 'cmake was not found on PATH.' }

function Invoke-CMakeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [int] $BuildParallelism,
        [Parameter(Mandatory = $true)]
        [string] $StandardOutputPath,
        [Parameter(Mandatory = $true)]
        [string] $StandardErrorPath
    )

    $process = Start-Process -FilePath $cmake.Source `
        -ArgumentList @('--build', "`"$build`"", '--config', 'Release', '--target', 'Telegram', '--parallel', $BuildParallelism) `
        -RedirectStandardOutput $StandardOutputPath `
        -RedirectStandardError $StandardErrorPath `
        -PassThru `
        -WindowStyle Hidden
    $readers = @($StandardOutputPath, $StandardErrorPath) | ForEach-Object {
        $stream = [System.IO.File]::Open($_, 'Open', 'Read', 'ReadWrite')
        [System.IO.StreamReader]::new($stream)
    }
    $processEnded = $false
    $commitExhausted = $false
    $lowCommitCount = 0
    $commitLimitMB = 0

    try {
        do {
            $finished = $process.WaitForExit($HeartbeatSeconds * 1000)
            foreach ($reader in $readers) {
                $output = $reader.ReadToEnd()
                if ($output) { Write-Host $output -NoNewline }
            }
            if (-not $finished) {
                $elapsed = (Get-Date) - $started
                $compilers = @(Get-Process -Name cl -ErrorAction SilentlyContinue)
                $private = ($compilers | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
                $privateMax = ($compilers | Measure-Object -Property PrivateMemorySize64 -Maximum).Maximum
                if (-not $private) { $private = 0 }
                if (-not $privateMax) { $privateMax = 0 }
                $cpu = ($compilers | Measure-Object -Property CPU -Sum).Sum
                if (-not $cpu) { $cpu = 0 }
                $memory = Get-CimInstance Win32_OperatingSystem -Property TotalVirtualMemorySize, FreeVirtualMemory
                $commitLimitMB = [math]::Round($memory.TotalVirtualMemorySize / 1KB)
                $commitFreeMB = [math]::Round($memory.FreeVirtualMemory / 1KB)
                Write-Host ("Build heartbeat: {0}; compiler processes: {1}; compiler private memory: {2} MB (max {3} MB); compiler CPU: {4} s; commit free: {5} MB of {6} MB" -f `
                    $elapsed.ToString('hh\:mm\:ss'), $compilers.Count, [math]::Round($private / 1MB), `
                    [math]::Round($privateMax / 1MB), [math]::Round($cpu), $commitFreeMB, $commitLimitMB)
                if ($commitFreeMB -lt $CommitFloorMB) {
                    $lowCommitCount++
                } else {
                    $lowCommitCount = 0
                }
                if ($lowCommitCount -ge 3) {
                    # A full build that reaches the commit limit used to hang
                    # for hours (the job was cancelled by its own timeout).
                    # Stop it instead, so the failure is reported quickly.
                    Write-Host ("Commit memory stayed below {0} MB for {1} heartbeats; stopping the build." -f $CommitFloorMB, $lowCommitCount)
                    $commitExhausted = $true
                    $process.Kill($true)
                    $process.WaitForExit()
                }
            }
        } while (-not $finished -and -not $commitExhausted)
        if (-not $commitExhausted) {
            $process.WaitForExit()
            $exitCode = $process.ExitCode
        }
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

    if ($commitExhausted) {
        throw "The build exhausted the runner commit memory (limit $commitLimitMB MB). The page file is too small for this compiler workload."
    }
    return $exitCode
}

$errorPath = "$LogPath.stderr"
$exitCode = Invoke-CMakeBuild `
    -BuildParallelism $Parallel `
    -StandardOutputPath $LogPath `
    -StandardErrorPath $errorPath

if ($exitCode -ne 0) {
    # A Release build of Telegram regularly sits at the commit limit of a
    # 16 GB windows-2022 runner while the stdafx.h PCH is loaded by two heavy
    # translation units at once, and cl.exe dies with C1060/C1076/C3859
    # ("paging file is too small"). Objects from the failed attempt stay
    # valid, so resume the build with one compiler instead of throwing away
    # the whole compile.
    $diagnostics = ''
    foreach ($path in @($LogPath, $errorPath)) {
        if (Test-Path -LiteralPath $path) {
            $diagnostics += Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
        }
    }
    if ($diagnostics -match 'error C(1060|1076|3859)|paging file is too small|heap limit reached') {
        $elapsed = (Get-Date) - $started
        $retryLogPath = "$LogPath.retry"
        $retryErrorPath = "$retryLogPath.stderr"
        Write-Host ("Compilers ran out of memory after {0}; resuming the build with one compiler at a time." -f $elapsed.ToString('hh\:mm\:ss'))
        $env:_CL_ = '/MP1 /FS'
        $exitCode = Invoke-CMakeBuild `
            -BuildParallelism 1 `
            -StandardOutputPath $retryLogPath `
            -StandardErrorPath $retryErrorPath
        if ($exitCode -ne 0) {
            throw "Build failed with exit code $exitCode after the serial retry. Full logs: $LogPath, $errorPath, $retryLogPath, $retryErrorPath"
        }
    } else {
        throw "Build failed with exit code $exitCode. Full logs: $LogPath, $errorPath"
    }
}
