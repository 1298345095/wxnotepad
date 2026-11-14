param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$taskProbe = Join-Path $taskRepository '.ci/directwrite-prototype/directwrite_diagnostic.exe'
$taskWpr = (Get-Command wpr.exe -ErrorAction Stop).Source
$taskMarkers = Join-Path $PSScriptRoot 'markers.wprp'
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $taskProbe -PathType Leaf)) { throw "Build the diagnostic probe first: $taskProbe" }
if (Test-Path -LiteralPath $taskOutput) { throw "Use a fresh output directory: $taskOutput" }
if (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($taskOutput)) -PathType Container)) {
    throw 'The output parent must already exist'
}
& $taskWpr -profiledetails "$taskMarkers!WxNotepadMarkers" | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'WPR marker profile validation failed' }
if ($ValidateOnly) {
    [ordered]@{ probe = $taskProbe; output = $taskOutput; profiles = 'CPU, GPU, VirtualAllocation, WxNotepadMarkers';
        probeTimeoutSeconds = 60; requiresAdministrator = $true } | ConvertTo-Json
    return
}
$taskAdministrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $taskAdministrator.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
    throw 'Windows kernel ETW recording requires an Administrator PowerShell process'
}
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskInstance = "wxnotepad-dwrite-$PID"
$taskStarted = $false
$taskFailure = $null
[ordered]@{ instance = $taskInstance; startedUtc = [DateTime]::UtcNow.ToString('o'); probe = $taskProbe;
    probeIntegrity = 'standard-user child of controller'; profiles = @('CPU', 'GPU', 'VirtualAllocation', 'WxNotepadMarkers') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'capture.json') -Encoding utf8
try {
    & $taskWpr -start CPU -start GPU -start VirtualAllocation -start "$taskMarkers!WxNotepadMarkers" -filemode `
        -recordtempto $taskOutput -instancename $taskInstance > (Join-Path $taskOutput 'wpr-start.log') 2>&1
    if ($LASTEXITCODE -ne 0) { throw "WPR start failed with $LASTEXITCODE; existing sessions were not cancelled" }
    $taskStarted = $true
    'ready' | Set-Content -LiteralPath (Join-Path $taskOutput 'trace-ready.txt')
    $taskDeadline = [DateTime]::UtcNow.AddSeconds(90)
    $taskDone = Join-Path $taskOutput 'probe-completion.json'
    while (-not (Test-Path -LiteralPath $taskDone)) {
        if ([DateTime]::UtcNow -gt $taskDeadline) { throw 'Controller did not finish its probe within 90 seconds' }
        Start-Sleep -Milliseconds 100
    }
    $taskResult = Get-Content -LiteralPath $taskDone -Raw | ConvertFrom-Json
    if (-not $taskResult.success) { throw "Diagnostic probe failed: $($taskResult.failure)" }
} catch {
    $taskFailure = $_.Exception.Message
} finally {
    if ($taskStarted) {
        & $taskWpr -stop (Join-Path $taskOutput 'diagnosis.etl') -instancename $taskInstance `
            > (Join-Path $taskOutput 'wpr-stop.log') 2>&1
        if ($LASTEXITCODE -ne 0) {
            $taskFailure = "WPR stop failed with $LASTEXITCODE; original raw traces recovered"
            foreach ($taskCollector in @('WPR Event Collector', 'WPR System Collector', 'wxnotepad diagnostic markers')) {
                & logman.exe stop "WPR_initiated_${taskInstance}_$taskCollector" -ets `
                    > (Join-Path $taskOutput ($taskCollector.Replace(' ', '-') + '-recovery.log')) 2>&1
            }
            $taskRecovered = Join-Path $taskOutput 'recovered-raw'
            New-Item -ItemType Directory -Path $taskRecovered | Out-Null
            Get-ChildItem -LiteralPath $taskOutput -Filter '*.etl' -File | Copy-Item -Destination $taskRecovered -ErrorAction Stop
            & $taskWpr -cancel -instancename $taskInstance > (Join-Path $taskOutput 'recovery-cancel.log') 2>&1
        }
    }
    [ordered]@{ success = ($null -eq $taskFailure); failure = $taskFailure; finishedUtc = [DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'completion.json') -Encoding utf8
}
if ($taskFailure) { throw $taskFailure }
