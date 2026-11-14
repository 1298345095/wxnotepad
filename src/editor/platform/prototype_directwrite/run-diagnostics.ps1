param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$taskBuild = Join-Path $taskRepository '.ci/directwrite-prototype'
$taskProbe = Join-Path $taskBuild 'directwrite_diagnostic.exe'
$taskNativeControl = Join-Path $taskBuild 'directwrite_native_memory.exe'
if (-not (Test-Path -LiteralPath $taskNativeControl -PathType Leaf)) { throw 'Build directwrite_native_memory first' }
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
$taskCollector = Join-Path $PSScriptRoot 'collect-wpt.ps1'
& $taskCollector -OutputDirectory $taskOutput -ValidateOnly
$taskAdministrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($taskAdministrator.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
    throw 'Run this controller without elevation; only its WPR collector elevates'
}
Start-Process -FilePath (Get-Command pwsh).Source -Verb RunAs -WindowStyle Hidden `
    -ArgumentList @('-NoProfile', '-File', ('"' + $taskCollector + '"'), '-OutputDirectory', ('"' + $taskOutput + '"'))
$taskDeadline = [DateTime]::UtcNow.AddSeconds(90)
while (-not (Test-Path -LiteralPath (Join-Path $taskOutput 'trace-ready.txt'))) {
    if ((Test-Path -LiteralPath (Join-Path $taskOutput 'completion.json')) -or [DateTime]::UtcNow -gt $taskDeadline) {
        throw 'WPR collector did not become ready; inspect completion.json and wpr-start.log'
    }
    Start-Sleep -Milliseconds 100
}
$taskFailure = $null
$taskProbeProcess = $null
$taskProbeDeadline = [DateTime]::UtcNow.AddSeconds(60)
try {
    $taskBinaryRecord = Join-Path $taskOutput 'binary-record'
    New-Item -ItemType Directory -Path $taskBinaryRecord | Out-Null
    Copy-Item -LiteralPath $taskProbe, (Join-Path $taskBuild 'directwrite_diagnostic.pdb'),
        $taskNativeControl, (Join-Path $taskBuild 'directwrite_native_memory.pdb'),
        (Join-Path $taskBuild 'diagnostic_probe.cpp'), (Join-Path $taskBuild 'editor_core_diagnostic.cpp'),
        (Join-Path $taskBuild 'directwrite_surface_diagnostic.cpp'), (Join-Path $taskBuild 'original_directwrite_diagnostic.inc'),
        (Join-Path $taskBuild 'compile_commands.json'), (Join-Path $PSScriptRoot 'diagnostic_metrics.h') -Destination $taskBinaryRecord
    Get-FileHash -Algorithm SHA256 -LiteralPath $taskProbe | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $taskOutput 'probe-sha256.json') -Encoding utf8
    foreach ($taskCase in @(
        @{name = 'native-control'; executable = $taskNativeControl; arguments = @()},
        @{name = 'probe'; executable = $taskProbe; arguments = @('0', 'long')}
    )) {
        if ([DateTime]::UtcNow -gt $taskProbeDeadline) { throw 'Shared 60-second diagnostic budget exhausted' }
        $taskLaunch = @{ FilePath = $taskCase.executable; PassThru = $true; WindowStyle = 'Normal';
            RedirectStandardOutput = (Join-Path $taskOutput ($taskCase.name + '.jsonl'));
            RedirectStandardError = (Join-Path $taskOutput ($taskCase.name + '.stderr.log')) }
        if ($taskCase.arguments.Count) { $taskLaunch.ArgumentList = $taskCase.arguments }
        $taskProbeProcess = Start-Process @taskLaunch
        $taskProbeProcess.Id | Set-Content -LiteralPath (Join-Path $taskOutput ($taskCase.name + '.pid'))
        while (-not $taskProbeProcess.HasExited) {
            if ([DateTime]::UtcNow -gt $taskProbeDeadline) { throw 'Shared 60-second diagnostic budget exhausted' }
            if (Test-Path -LiteralPath (Join-Path $taskOutput 'completion.json')) {
                throw 'Collector ended before the owned diagnostic process completed'
            }
            Start-Sleep -Milliseconds 100
            $taskProbeProcess.Refresh()
        }
        $taskProbeProcess.Refresh()
        if ($taskProbeProcess.ExitCode -ne 0) { throw "$($taskCase.name) exit: $($taskProbeProcess.ExitCode)" }
    }
} catch {
    $taskFailure = $_.Exception.Message
} finally {
    if ($null -ne $taskProbeProcess -and -not $taskProbeProcess.HasExited) {
        $taskProbeProcess.Kill()
        $taskProbeProcess.WaitForExit()
    }
    $taskPending = Join-Path $taskOutput 'probe-completion.pending'
    [ordered]@{ success = ($null -eq $taskFailure); failure = $taskFailure } | ConvertTo-Json |
        Set-Content -LiteralPath $taskPending -Encoding utf8
    Move-Item -LiteralPath $taskPending -Destination (Join-Path $taskOutput 'probe-completion.json')
}
$taskDeadline = [DateTime]::UtcNow.AddSeconds(120)
while (-not (Test-Path -LiteralPath (Join-Path $taskOutput 'completion.json'))) {
    if ([DateTime]::UtcNow -gt $taskDeadline) { throw 'WPR collector has not finished; inspect its owned session and logs' }
    Start-Sleep -Milliseconds 100
}
$taskResult = Get-Content -LiteralPath (Join-Path $taskOutput 'completion.json') -Raw | ConvertFrom-Json
if (-not $taskResult.success) { throw $taskResult.failure }
