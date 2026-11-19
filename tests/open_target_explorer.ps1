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
    while ($null -eq $taskMatched -and $taskWatch.Elapsed.TotalSeconds -lt 15) {
        foreach ($taskWindow in $taskShell.Windows()) {
            try {
                if ($taskExisting.ContainsKey([string]$taskWindow.HWND)) { continue }
                if ($taskWindow.Document.Folder.Self.Path -ine $taskDirectory) { continue }
                $taskSelected = @($taskWindow.Document.SelectedItems() | ForEach-Object { $_.Path })
                if ([string]::IsNullOrEmpty($taskItem) -or $taskSelected -contains $taskItem) { $taskMatched = $taskWindow; break }
            } catch { }
        }
        if ($null -eq $taskMatched) { Start-Sleep -Milliseconds 20 }
    }
    if ($null -eq $taskMatched) { throw "Native Explorer did not select the owned item: $taskItem" }
    $taskRecord = [ordered]@{ hwnd = [string]$taskMatched.HWND; directory = $taskDirectory; selected = $taskItem }
    Write-Host ('PASS: native Explorer ' + ($taskRecord | ConvertTo-Json -Compress))
    # Reverify immediately before closing only the newly created owned-folder window.
    if (-not $taskExisting.ContainsKey([string]$taskMatched.HWND) -and $taskMatched.Document.Folder.Self.Path -ieq $taskDirectory) {
        $taskMatched.Quit()
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
