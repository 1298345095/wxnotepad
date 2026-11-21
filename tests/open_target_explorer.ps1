param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$taskComErrors = @{}
function Write-ExplorerPollingError([System.Management.Automation.ErrorRecord]$Failure, [string]$Operation,
                                    [switch]$DuringCleanup) {
    $taskException = $Failure.Exception.GetBaseException()
    $taskResult = '{0:X8}' -f $taskException.HResult
    # Navigation and window destruction can temporarily reject or disconnect
    # these read-only COM queries. Other observation failures must stay fatal.
    $taskTransient = $taskException -is [System.Runtime.InteropServices.COMException] -and
        $taskResult -in @('80010001', '80010108', '8001010A', '800401FD')
    if (-not $taskTransient -and -not $DuringCleanup) { throw $Failure }
    $taskKey = "$Operation/$taskResult/$($taskException.Message)"
    if (-not $taskComErrors.ContainsKey($taskKey)) {
        $taskComErrors[$taskKey] = 0
        Write-Warning "Explorer $Operation COM query failed (HRESULT $taskResult): $($taskException.Message)"
    }
    $taskComErrors[$taskKey]++
}
$taskShell = New-Object -ComObject Shell.Application
$taskExisting = @{}
foreach ($taskWindow in $taskShell.Windows()) { $taskExisting[[string]$taskWindow.HWND] = $true }
[System.IO.File]::WriteAllText((Join-Path $Root 'explorer-ready'), 'ready')
$taskRequested = @{}
try {
for ($taskIndex = 1; $taskIndex -le 4; $taskIndex++) {
    $taskRequest = Join-Path $Root "folder-request-$taskIndex"
    $taskWatch = [System.Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $taskRequest)) {
        if ($taskWatch.Elapsed.TotalSeconds -gt 30) { throw 'Owned folder request did not arrive' }
        Start-Sleep -Milliseconds 20
    }
    $taskLines = [System.IO.File]::ReadAllLines($taskRequest, [System.Text.Encoding]::UTF8)
    $taskDirectory = $taskLines[0]
    $taskRequested[$taskDirectory] = $true
    $taskItem = $taskLines[1]
    $taskCaller = if ($taskLines.Length -gt 2) { [uint32]$taskLines[2] } else { 0 }
    if (-not ([System.IO.Path]::GetFullPath($taskDirectory).StartsWith([System.IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Folder observation must remain inside the owned fixture root'
    }
    $taskWatch.Restart()
    $taskMatched = $null
    $taskObserved = @{}
    while ($null -eq $taskMatched -and $taskWatch.Elapsed.TotalSeconds -lt 15) {
        foreach ($taskWindow in $taskShell.Windows()) {
            try {
                if ($taskExisting.ContainsKey([string]$taskWindow.HWND)) { continue }
                $taskActualDirectory = [string]$taskWindow.Document.Folder.Self.Path
                if (-not $taskActualDirectory.StartsWith([System.IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) { continue }
                $taskSelected = @($taskWindow.Document.SelectedItems() | ForEach-Object { $_.Path })
                $taskObserved[[string]$taskWindow.HWND] = @{ directory = $taskActualDirectory; selected = $taskSelected }
                if ($taskActualDirectory -ine $taskDirectory) { continue }
                if ([string]::IsNullOrEmpty($taskItem) -or $taskSelected -contains $taskItem) { $taskMatched = $taskWindow; break }
            } catch { Write-ExplorerPollingError $_ 'selection' }
        }
        if ($null -eq $taskMatched) { Start-Sleep -Milliseconds 20 }
    }
    if ($null -eq $taskMatched) {
        Write-Host ('Owned folder observations: ' + ($taskObserved | ConvertTo-Json -Compress))
        Write-Host ('Explorer polling errors: ' + ($taskComErrors | ConvertTo-Json -Compress))
        & "$PSScriptRoot/open_target_explorer_capture.ps1" -Root $Root -Evidence $Evidence `
            -Directory $taskDirectory -Item $taskItem -Request $taskIndex -CallerPid $taskCaller -Shell $taskShell -Existing $taskExisting
        throw "Native Explorer did not select the owned item: $taskItem"
    }
    $taskRecord = [ordered]@{ hwnd = [string]$taskMatched.HWND; directory = $taskDirectory; selected = $taskItem }
    Write-Host ('PASS: native Explorer ' + ($taskRecord | ConvertTo-Json -Compress))
    # Reverify immediately before closing only the newly created owned-folder window.
    if (-not $taskExisting.ContainsKey([string]$taskMatched.HWND) -and $taskMatched.Document.Folder.Self.Path -ieq $taskDirectory) {
        $taskClosingHandle = [string]$taskMatched.HWND
        $taskMatched.Quit()
        $taskCloseWatch = [System.Diagnostics.Stopwatch]::StartNew()
        do {
            $taskStillPresent = $false
            foreach ($taskWindow in $taskShell.Windows()) {
                try { if ([string]$taskWindow.HWND -eq $taskClosingHandle) { $taskStillPresent = $true } }
                catch { Write-ExplorerPollingError $_ 'closing' }
            }
            if ($taskStillPresent) { Start-Sleep -Milliseconds 20 }
        } while ($taskStillPresent -and $taskCloseWatch.Elapsed.TotalSeconds -lt 5)
        if ($taskStillPresent) { throw 'Owned Explorer window must finish closing before the next selection request' }
    }
    # Publish only after the complete acknowledgement is closed; the native
    # reader checks existence before independently reading the exact bytes.
    $taskDone = Join-Path $Root "folder-done-$taskIndex"
    [System.IO.File]::WriteAllText("$taskDone.tmp", "ok`n")
    [System.IO.File]::Move("$taskDone.tmp", $taskDone)
}

} finally {
    foreach ($taskWindow in $taskShell.Windows()) {
        try {
            if (-not $taskExisting.ContainsKey([string]$taskWindow.HWND) -and
                $taskRequested.ContainsKey([string]$taskWindow.Document.Folder.Self.Path)) {
                $taskWindow.Quit()
            }
        } catch { Write-ExplorerPollingError $_ 'cleanup' -DuringCleanup }
    }
}
