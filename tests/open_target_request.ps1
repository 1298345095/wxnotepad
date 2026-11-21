function Read-ExplorerRequest([string]$Path) {
    # A closed staging file can become visible before its rename DELETE handle
    # closes. The published request is immutable; preserve all UTF-8 lines.
    $taskSharing = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
    $taskStream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $taskSharing)
    try {
        $taskReader = [System.IO.StreamReader]::new($taskStream, [System.Text.Encoding]::UTF8)
        try {
            while ($null -ne ($taskLine = $taskReader.ReadLine())) { $taskLine }
        } finally { $taskReader.Dispose() }
    } finally { $taskStream.Dispose() }
}
