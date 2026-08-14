<#
.SYNOPSIS
Builds and runs the host-only libFuzzer decoder harness.

.DESCRIPTION
The script keeps all mutable corpus, saved-input, log, and metadata files below the
Git-ignored testdata/fuzz directory.  A normal run resumes from the minimized
corpus left by an earlier run.  It never starts a long campaign unless the
caller explicitly supplies -DurationMinutes.

Copyright (c) 2026 Steven Zhu
SPDX-License-Identifier: MIT
#>
[CmdletBinding()]
param(
    [switch]$Smoke,

    [int]$DurationMinutes = 0,

    [ValidateRange(1, 16)]
    [int]$Jobs = 1,

    [ValidateRange(1, 120)]
    [int]$InputTimeoutSeconds = 10,

    [ValidateRange(1024, 1048576)]
    [int]$MaxInputBytes = 65536,

    [string]$SeedSource = "",

    [string]$WorkRoot = "",

    [string]$ReplayArtifact = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($WorkRoot)) {
    $WorkRoot = Join-Path $repositoryRoot "testdata\fuzz"
} elseif (-not [System.IO.Path]::IsPathRooted($WorkRoot)) {
    $WorkRoot = Join-Path $repositoryRoot $WorkRoot
}
if ([string]::IsNullOrWhiteSpace($SeedSource)) {
    $SeedSource = Join-Path $repositoryRoot `
        "testdata\compatibility_corpus\codec-corpus\gif-conformance"
} elseif (-not [System.IO.Path]::IsPathRooted($SeedSource)) {
    $SeedSource = Join-Path $repositoryRoot $SeedSource
}

if (-not $Smoke -and [string]::IsNullOrWhiteSpace($ReplayArtifact) -and
    $DurationMinutes -eq 0) {
    throw "Refusing to start an unbounded fuzz run. Use -Smoke, -ReplayArtifact, or -DurationMinutes <minutes>."
}
if ($Smoke -and $DurationMinutes -ne 0) {
    throw "Use either -Smoke or -DurationMinutes, not both."
}
if ($DurationMinutes -lt 0 -or $DurationMinutes -gt 100000) {
    throw "DurationMinutes must be between 1 and 100000 for a timed campaign."
}

if (-not (Get-Command Load-MSVC -ErrorAction SilentlyContinue)) {
    throw "Load-MSVC is unavailable. Start the script from the project's MSVC developer PowerShell environment."
}
Load-MSVC
$clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($null -eq $clangCommand -or [string]::IsNullOrWhiteSpace($clangCommand.Source)) {
    throw "clang.exe is unavailable after loading the MSVC environment."
}
$clangCompiler = $clangCommand.Source

$buildRoot = Join-Path $repositoryRoot "build\host-fuzz-clang"
$seedDirectory = Join-Path $WorkRoot "seeds"
$artifactDirectory = Join-Path $WorkRoot "artifacts"
$logDirectory = Join-Path $WorkRoot "logs"
foreach ($directory in @($seedDirectory, $artifactDirectory, $logDirectory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

if ([string]::IsNullOrWhiteSpace($ReplayArtifact)) {
    $existingSeeds = @(Get-ChildItem -LiteralPath $seedDirectory -File -ErrorAction Stop)
    if ($existingSeeds.Count -eq 0) {
        if (-not (Test-Path -LiteralPath $SeedSource -PathType Container)) {
            throw "The initial seed source does not exist: $SeedSource. Supply -SeedSource or acquire the documented local compatibility corpus."
        }
        $seedFiles = @(Get-ChildItem -LiteralPath $SeedSource -Recurse -File -Filter *.gif)
        if ($seedFiles.Count -eq 0) {
            throw "No GIF files were found below initial seed source: $SeedSource"
        }
        foreach ($seedFile in $seedFiles) {
            $relativePath = $seedFile.FullName.Substring($SeedSource.Length).TrimStart('\', '/')
            $destination = Join-Path $seedDirectory ($relativePath -replace '[\\/]', '__')
            Copy-Item -LiteralPath $seedFile.FullName -Destination $destination -Force
        }
        Write-Host "Initialized $($seedFiles.Count) seed files in $seedDirectory"
    }
}

cmake -S $repositoryRoot -B $buildRoot -G Ninja `
    "-DCMAKE_C_COMPILER=$clangCompiler" `
    -DGIFLIB_BUILD_TESTS=OFF `
    -DGIFLIB_BUILD_EXAMPLES=OFF `
    -DGIFLIB_BUILD_FUZZER=ON `
    -DGIFLIB_ENABLE_HOST_SANITIZERS=ON
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}
cmake --build $buildRoot --target gif_decoder_fuzzer
if ($LASTEXITCODE -ne 0) {
    throw "Fuzzer build failed."
}

$fuzzer = Join-Path $buildRoot "gif_decoder_fuzzer.exe"
if (-not (Test-Path -LiteralPath $fuzzer -PathType Leaf)) {
    throw "The fuzzer executable was not produced: $fuzzer"
}
$activeFuzzers = @(Get-Process -Name gif_decoder_fuzzer -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $fuzzer })
if ($activeFuzzers.Count -ne 0) {
    throw "An existing local run is still using this executable. End it before starting another run with the same corpus."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logFile = Join-Path $logDirectory "fuzz-$timestamp.log"
$metadataFile = Join-Path $logDirectory "fuzz-$timestamp.json"
$artifactPrefix = [System.IO.Path]::GetFullPath($artifactDirectory) + [System.IO.Path]::DirectorySeparatorChar
$runMode = if ($Smoke) { "smoke" } elseif ($ReplayArtifact) { "replay" } else { "timed" }
$gitRevision = (git -C $repositoryRoot rev-parse HEAD 2>$null).Trim()

$fuzzerArguments = @(
    "-artifact_prefix=$artifactPrefix",
    "-timeout=$InputTimeoutSeconds",
    "-max_len=$MaxInputBytes",
    "-rss_limit_mb=1024",
    "-malloc_limit_mb=512",
    "-print_final_stats=1",
    "-verbosity=1"
)
if ($Smoke) {
    $fuzzerArguments += "-runs=2000"
} elseif ($ReplayArtifact) {
    $ReplayArtifact = [System.IO.Path]::GetFullPath($ReplayArtifact)
    if (-not (Test-Path -LiteralPath $ReplayArtifact -PathType Leaf)) {
        throw "Replay artifact does not exist: $ReplayArtifact"
    }
    $fuzzerArguments += "-runs=1"
} else {
    $fuzzerArguments += "-max_total_time=$($DurationMinutes * 60)"
    if ($Jobs -gt 1) {
        $fuzzerArguments += "-jobs=$Jobs"
        $fuzzerArguments += "-workers=$Jobs"
    }
}

$inputPath = if ($ReplayArtifact) { $ReplayArtifact } else { $seedDirectory }
$workerLogDirectory = Join-Path $logDirectory "workers-$timestamp"
$controllerOutputFile = Join-Path $logDirectory "fuzz-$timestamp-controller.log"
New-Item -ItemType Directory -Force -Path $workerLogDirectory | Out-Null
$metadata = [ordered]@{
    started_utc = (Get-Date).ToUniversalTime().ToString("o")
    mode = $runMode
    git_revision = $gitRevision
    fuzzer = $fuzzer
    input_path = $inputPath
    seed_source = $SeedSource
    arguments = $fuzzerArguments
    jobs = $Jobs
    worker_log_directory = $workerLogDirectory
    controller_log = $controllerOutputFile
}
$metadata | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $metadataFile -Encoding utf8

Write-Host "Starting $runMode fuzz run. Live log: $logFile"
Write-Host "Live worker records: $workerLogDirectory"
Write-Host "Saved inputs: $artifactDirectory"
Write-Host "Stop safely with Ctrl+C; the corpus already written to $seedDirectory remains reusable."
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$exitCode = -1

function Get-WorkerProgressSummary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    $workerLogs = @(Get-ChildItem -LiteralPath $Directory -File -Filter "fuzz-*.log" `
        -ErrorAction SilentlyContinue | Sort-Object Name)
    if ($workerLogs.Count -eq 0) {
        return "starting worker processes"
    }

    $workerStates = foreach ($workerLog in $workerLogs) {
        try {
            $lastLine = Get-Content -LiteralPath $workerLog.FullName -Tail 1 -ErrorAction Stop
            if (-not [string]::IsNullOrWhiteSpace($lastLine)) {
                $workerName = $workerLog.BaseName -replace '^fuzz-', 'f'
                $iteration = [regex]::Match($lastLine, '#(?<value>\d+)')
                $coverage = [regex]::Match($lastLine, 'cov:\s*(?<value>\d+)')
                $rate = [regex]::Match($lastLine, 'exec/s:\s*(?<value>[\d.]+)')
                if ($iteration.Success -and $coverage.Success -and $rate.Success) {
                    "$workerName #$($iteration.Groups['value'].Value) " +
                    "cov $($coverage.Groups['value'].Value) " +
                    "$($rate.Groups['value'].Value)/s"
                } else {
                    "$workerName active"
                }
            } else {
                "$($workerLog.BaseName -replace '^fuzz-', 'f') starting"
            }
        } catch {
            "$($workerLog.BaseName -replace '^fuzz-', 'f') updating"
        }
    }

    return ($workerStates -join " | ")
}

$asanOptions = "abort_on_error=1:halt_on_error=1"
if (-not $IsWindows) {
    $asanOptions += ":detect_leaks=1"
}
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $fuzzer
$startInfo.WorkingDirectory = $workerLogDirectory
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.Environment["ASAN_OPTIONS"] = $asanOptions
foreach ($argument in @($fuzzerArguments) + @($inputPath)) {
    [void]$startInfo.ArgumentList.Add($argument)
}
$fuzzerProcess = [System.Diagnostics.Process]::new()
$fuzzerProcess.StartInfo = $startInfo
$controllerOutput = ""
$controllerError = ""
try {
    if (-not $fuzzerProcess.Start()) {
        throw "Unable to start the fuzzer executable."
    }
    $standardOutputTask = $fuzzerProcess.StandardOutput.ReadToEndAsync()
    $standardErrorTask = $fuzzerProcess.StandardError.ReadToEndAsync()

    while (-not $fuzzerProcess.HasExited) {
        if (-not $Smoke -and -not $ReplayArtifact -and $DurationMinutes -gt 0) {
            $percent = [Math]::Min(99, [int](100 * $stopwatch.Elapsed.TotalSeconds /
                                              ($DurationMinutes * 60)))
            Write-Progress -Activity "libFuzzer campaign" `
                -Status ("elapsed " + $stopwatch.Elapsed.ToString() + "; " +
                         (Get-WorkerProgressSummary -Directory $workerLogDirectory)) `
                -PercentComplete $percent
        }
        Start-Sleep -Seconds 1
    }
    $fuzzerProcess.WaitForExit()
    $controllerOutput = $standardOutputTask.GetAwaiter().GetResult()
    $controllerError = $standardErrorTask.GetAwaiter().GetResult()
    $exitCode = $fuzzerProcess.ExitCode
} finally {
    if (-not $fuzzerProcess.HasExited) {
        Write-Host "Stopping local worker processes..."
        $fuzzerProcess.Kill($true)
        $fuzzerProcess.WaitForExit()
    }
    if ($null -ne $standardOutputTask) {
        $controllerOutput = $standardOutputTask.GetAwaiter().GetResult()
    }
    if ($null -ne $standardErrorTask) {
        $controllerError = $standardErrorTask.GetAwaiter().GetResult()
    }
    @(
        "=== controller standard output ===",
        $controllerOutput,
        "=== controller standard error ===",
        $controllerError
    ) | Set-Content -LiteralPath $controllerOutputFile -Encoding utf8
    @(
        "Controller record: $controllerOutputFile",
        "Worker records: $workerLogDirectory"
    ) | Set-Content -LiteralPath $logFile -Encoding utf8
    $stopwatch.Stop()
    Write-Progress -Activity "libFuzzer campaign" -Completed
}

$metadata.completed_utc = (Get-Date).ToUniversalTime().ToString("o")
$metadata.elapsed_seconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
$metadata.exit_code = $exitCode
$metadata | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $metadataFile -Encoding utf8

if ($exitCode -ne 0) {
    throw "libFuzzer ended with exit code $exitCode. Preserve $logFile, $metadataFile, and every file under $artifactDirectory for review."
}
Write-Host "libFuzzer completed successfully in $($stopwatch.Elapsed). Log: $logFile"
