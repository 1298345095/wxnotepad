param([Parameter(Mandatory)][string]$Root)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
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
            } catch { }
        }
        if ($null -eq $taskMatched) { Start-Sleep -Milliseconds 20 }
    }
    if ($null -eq $taskMatched) {
        Write-Host ('Owned folder observations: ' + ($taskObserved | ConvertTo-Json -Compress))
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
                try { if ([string]$taskWindow.HWND -eq $taskClosingHandle) { $taskStillPresent = $true } } catch { }
            }
            if ($taskStillPresent) { Start-Sleep -Milliseconds 20 }
        } while ($taskStillPresent -and $taskCloseWatch.Elapsed.TotalSeconds -lt 5)
        if ($taskStillPresent) { throw 'Owned Explorer window must finish closing before the next selection request' }
    }
    [System.IO.File]::WriteAllText((Join-Path $Root "folder-done-$taskIndex"), "ok`n")
}

} finally {
    foreach ($taskWindow in $taskShell.Windows()) {
        try {
            if (-not $taskExisting.ContainsKey([string]$taskWindow.HWND) -and
                $taskRequested.ContainsKey([string]$taskWindow.Document.Folder.Self.Path)) {
                $taskWindow.Quit()
            }
        } catch { }
    }
}
