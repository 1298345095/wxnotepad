param([string]$Root, [string]$Evidence, [string]$Directory, [string]$Item, [int]$Request, [uint32]$CallerPid = 0,
      [object]$Shell, [hashtable]$Existing)
$ErrorActionPreference = 'Stop'
[System.IO.Directory]::CreateDirectory($Evidence) > $null
$taskSnapshot = [ordered]@{ request = $Request; expected_directory = $Directory; expected_item = $Item;
    observer_user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name;
    observer_session = [System.Diagnostics.Process]::GetCurrentProcess().SessionId; windows = @() }
if ($Item -and [System.IO.Path]::GetFullPath($Item).StartsWith([System.IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) {
    try {
        $taskSource = Get-Item -LiteralPath $Item
        $taskSnapshot.expected_item_attributes = [string]$taskSource.Attributes
        $taskSnapshot.expected_item_acl = (Get-Acl -LiteralPath $Item).Sddl
        $taskSnapshot.expected_directory_acl = (Get-Acl -LiteralPath $Directory).Sddl
    } catch { $taskSnapshot.expected_item_error = $_.Exception.Message }
}
try {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OpenTargetExplorerNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rectangle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("dwmapi.dll")] public static extern int DwmFlush();
    [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] private static extern IntPtr LocalFree(IntPtr memory);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool GetTokenInformation(IntPtr token, int information, IntPtr buffer, uint length, out uint needed);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool ConvertSidToStringSidW(IntPtr sid, out IntPtr text);
    private enum TokenInformation { User = 1, ElevationType = 18, Elevation = 20, IntegrityLevel = 25 }
    private static IntPtr Information(IntPtr token, TokenInformation kind) {
        uint length;
        GetTokenInformation(token, (int)kind, IntPtr.Zero, 0, out length);
        if (length == 0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        IntPtr buffer = Marshal.AllocHGlobal((int)length);
        if (!GetTokenInformation(token, (int)kind, buffer, length, out length)) {
            int error = Marshal.GetLastWin32Error();
            Marshal.FreeHGlobal(buffer);
            throw new System.ComponentModel.Win32Exception(error);
        }
        return buffer;
    }
    private static string Sid(IntPtr token, TokenInformation kind) {
        IntPtr buffer = Information(token, kind);
        try {
            IntPtr text;
            if (!ConvertSidToStringSidW(Marshal.ReadIntPtr(buffer), out text))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            try { return Marshal.PtrToStringUni(text); }
            finally { LocalFree(text); }
        } finally { Marshal.FreeHGlobal(buffer); }
    }
    private static int Integer(IntPtr token, TokenInformation kind) {
        IntPtr buffer = Information(token, kind);
        try { return Marshal.ReadInt32(buffer); }
        finally { Marshal.FreeHGlobal(buffer); }
    }
    public static string[] TokenContext(uint pid) {
        const uint ProcessQueryLimitedInformation = 0x1000;
        const uint TokenQuery = 0x0008;
        IntPtr process = OpenProcess(ProcessQueryLimitedInformation, false, pid);
        if (process == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try {
            IntPtr token;
            if (!OpenProcessToken(process, TokenQuery, out token))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            try {
                return new string[] { Sid(token, TokenInformation.User), Sid(token, TokenInformation.IntegrityLevel),
                    Integer(token, TokenInformation.ElevationType).ToString(), Integer(token, TokenInformation.Elevation).ToString() };
            } finally { CloseHandle(token); }
        } finally { CloseHandle(process); }
    }
}
'@
    function Get-OpenTargetTokenContext([uint32]$TaskProcessId) {
        $taskToken = [OpenTargetExplorerNative]::TokenContext($TaskProcessId)
        return [ordered]@{ user_sid = $taskToken[0]; integrity_sid = $taskToken[1];
            elevation_type = [int]$taskToken[2]; elevated = $taskToken[3] -ne '0' }
    }
    try { $taskSnapshot.observer_token = Get-OpenTargetTokenContext ([System.Diagnostics.Process]::GetCurrentProcess().Id) }
    catch { $taskSnapshot.observer_token_error = $_.Exception.Message }
    if ($CallerPid -ne 0) {
        try {
            $taskCallerProcess = Get-Process -Id $CallerPid
            if ([System.IO.Path]::GetDirectoryName($taskCallerProcess.Path) -ine [System.IO.Path]::GetFullPath($Root)) {
                throw 'Caller token collection must remain with the executable in the owned fixture root'
            }
            $taskSnapshot.caller_pid = $CallerPid
            $taskSnapshot.caller_session = $taskCallerProcess.SessionId
            $taskSnapshot.caller_token = Get-OpenTargetTokenContext $CallerPid
        } catch { $taskSnapshot.caller_token_error = $_.Exception.Message }
    }
    $taskSnapshot.foreground = [string][OpenTargetExplorerNative]::GetForegroundWindow()
    foreach ($taskWindow in $Shell.Windows()) {
        try {
            $taskHandle = [string]$taskWindow.HWND
            if ($Existing.ContainsKey($taskHandle)) { continue }
            $taskPath = [string]$taskWindow.Document.Folder.Self.Path
            if (-not $taskPath.StartsWith([System.IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) { continue }
            if ([System.IO.Path]::GetFileName([string]$taskWindow.FullName) -ine 'explorer.exe') { continue }
            $taskRecord = [ordered]@{ hwnd = $taskHandle; directory = $taskPath; busy = $taskWindow.Busy;
                ready_state = [int]$taskWindow.ReadyState; location_url = [string]$taskWindow.LocationURL;
                selected = @($taskWindow.Document.SelectedItems() | ForEach-Object { $_.Path });
                items = @($taskWindow.Document.Folder.Items() | ForEach-Object {
                    [ordered]@{ name = $_.Name; path = $_.Path; folder = $_.IsFolder }
                }) }
            try { $taskRecord.focused_item = [string]$taskWindow.Document.FocusedItem.Path } catch { $taskRecord.focus_error = $_.Exception.Message }
            $taskOwner = [uint32]0
            [OpenTargetExplorerNative]::GetWindowThreadProcessId([IntPtr]$taskWindow.HWND, [ref]$taskOwner) > $null
            $taskRecord.pid = $taskOwner
            $taskRecord.session = (Get-Process -Id $taskOwner).SessionId
            try { $taskRecord.token = Get-OpenTargetTokenContext $taskOwner }
            catch { $taskRecord.token_error = $_.Exception.Message }
            $taskRecord.explorer_version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo([string]$taskWindow.FullName).FileVersion
            $taskSnapshot.windows += $taskRecord
            [System.IO.File]::WriteAllText((Join-Path $Evidence "explorer-$Request.json"), ($taskSnapshot | ConvertTo-Json -Depth 8))
            try {
                $taskHwnd = [IntPtr]$taskWindow.HWND
                if (-not [OpenTargetExplorerNative]::IsWindowVisible($taskHwnd) -or [OpenTargetExplorerNative]::IsIconic($taskHwnd)) { throw 'Owned Explorer window is not visibly presented' }
                $taskPreviousDpi = [OpenTargetExplorerNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
                $taskTopmost = ([OpenTargetExplorerNative]::GetWindowLongPtr($taskHwnd, -20).ToInt64() -band 8) -ne 0
                try {
                    if (-not [OpenTargetExplorerNative]::SetWindowPos($taskHwnd, [IntPtr](-1), 0, 0, 0, 0, 0x13)) { throw 'Could not place verified owned Explorer window on top for capture' }
                    [OpenTargetExplorerNative]::DwmFlush() > $null
                    $taskBounds = [OpenTargetExplorerNative+Rect]::new()
                    if (-not [OpenTargetExplorerNative]::GetWindowRect($taskHwnd, [ref]$taskBounds)) { throw 'Could not read owned Explorer bounds' }
                    $taskBitmap = [System.Drawing.Bitmap]::new($taskBounds.Right - $taskBounds.Left, $taskBounds.Bottom - $taskBounds.Top)
                    try {
                        $taskGraphics = [System.Drawing.Graphics]::FromImage($taskBitmap)
                        try { $taskGraphics.CopyFromScreen($taskBounds.Left, $taskBounds.Top, 0, 0, $taskBitmap.Size) }
                        finally { $taskGraphics.Dispose() }
                        $taskImage = "explorer-$Request-$taskHandle.png"
                        $taskBitmap.Save((Join-Path $Evidence $taskImage), [System.Drawing.Imaging.ImageFormat]::Png)
                        $taskRecord.image = $taskImage
                    } finally { $taskBitmap.Dispose() }
                } finally {
                    if (-not $taskTopmost) { [OpenTargetExplorerNative]::SetWindowPos($taskHwnd, [IntPtr](-2), 0, 0, 0, 0, 0x13) > $null }
                    if ($taskPreviousDpi -ne [IntPtr]::Zero) { [OpenTargetExplorerNative]::SetThreadDpiAwarenessContext($taskPreviousDpi) > $null }
                }
            } catch { $taskRecord.capture_error = $_.Exception.Message }
        } catch { $taskSnapshot.windows += [ordered]@{ error = $_.Exception.Message } }
    }
} catch { $taskSnapshot.error = $_.Exception.Message }
[System.IO.File]::WriteAllText((Join-Path $Evidence "explorer-$Request.json"), ($taskSnapshot | ConvertTo-Json -Depth 8))
Write-Host ('Owned Explorer failure snapshot: ' + (Join-Path $Evidence "explorer-$Request.json"))
