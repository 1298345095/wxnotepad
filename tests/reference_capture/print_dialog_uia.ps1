param(
    [Parameter(Mandatory = $true)][int]$ProxyProcess,
    [Parameter(Mandatory = $true)][ValidateSet('Inspect', 'Cancel', 'Print', 'PrintAll', 'InspectPages', 'PrintRange', 'PrintWhole')][string]$Action,
    [string]$Excluded = '0'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if ($Action -eq 'PrintRange') {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PrintInput {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
}

$root = [System.Windows.Automation.AutomationElement]::RootElement
$automation = [System.Windows.Automation.AutomationElement]
$condition = [System.Windows.Automation.PropertyCondition]::new($automation::ProcessIdProperty, $ProxyProcess)
$excludedHandles = @($Excluded.Split(',') | ForEach-Object { [int]$_ })
$wait = [System.Diagnostics.Stopwatch]::StartNew()
do {
    $windowCandidates = @($root.FindAll([System.Windows.Automation.TreeScope]::Children, $condition) | Where-Object {
        $_.Current.NativeWindowHandle -notin $excludedHandles -and
        $_.Current.Name -match '^(?:(?:Notepad4|wxnotepad|printing|Print from a Win32 app|从 Win32 应用程序打印) - )?(Print|打印|列印|打印对话框|列印對話方塊)$'
    })
    if (!$windowCandidates) { Start-Sleep -Milliseconds 100 }
} until ($windowCandidates -or $wait.Elapsed.TotalSeconds -ge 4)
if ($windowCandidates.Count -ne 1) {
    $matchedProcessWindows = @($root.FindAll([System.Windows.Automation.TreeScope]::Children, $condition) |
        ForEach-Object { "$($_.Current.NativeWindowHandle) $($_.Current.ClassName) $($_.Current.Name)" })
    throw "Expected one new print window associated with proxy $ProxyProcess; found $($matchedProcessWindows -join '; ')"
}

$window = $windowCandidates[0]
$all = @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition))

function Record($element) {
    $current = $element.Current
    $pattern = $null
    $state = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) {
        $state = $pattern.Current.IsSelected
    } elseif ($element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) {
        $state = "$($pattern.Current.ToggleState)"
    }
    $value = $null
    if ($current.AutomationId -eq 'com.microsoft.JobCustomPageRange_ValueText' -and
        $element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
        $value = $pattern.Current.Value
    }
    [pscustomobject]@{ Value = $value; Focused = $current.HasKeyboardFocus; Name = $current.Name; Id = $current.AutomationId; Class = $current.ClassName;
        Type = $current.ControlType.ProgrammaticName; Enabled = $current.IsEnabled; Selected = $state;
        Offscreen = $current.IsOffscreen; Handle = $current.NativeWindowHandle }
}

if ($Action -in @('InspectPages', 'PrintRange')) {
    $pagesCombo = @($all | Where-Object { $_.Current.AutomationId -eq 'com.microsoft.JobCustomPageRange_ItemList' })
    if ($pagesCombo.Count -ne 1) { throw 'Expected one Pages combo box' }
    $pageExpand = $pagesCombo[0].GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $pageExpand.Expand()
    Start-Sleep -Milliseconds 500
    $all = @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition))
}
if ($Action -eq 'PrintRange') {
    $popup = @($all | Where-Object { $_.Current.AutomationId -eq 'Popup' -and -not $_.Current.IsOffscreen })
    if ($popup.Count -ne 1) { throw 'Expected one scoped Pages popup' }
    $popupChildren = $popup[0].FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
    $rangeChoice = @($popupChildren | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and
        -not $_.Current.IsOffscreen -and $_.Current.Name.Trim() -match '^(Custom( (range|page range))?|自定义范围|自訂範圍)$'
    })
    if ($rangeChoice.Count -ne 1) {
        @($all | ForEach-Object { Record $_ }) | ConvertTo-Json -Depth 5 -Compress
        throw "Expected one custom page range choice, found $($rangeChoice.Count)"
    }
    $rangeChoice[0].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 250
    $all = @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition))
    $rangeFields = @($all | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Edit -and
        $_.Current.IsEnabled -and -not $_.Current.IsOffscreen
    })
    if ($rangeFields.Count -ne 1) { throw 'Expected one custom page range field' }
    $field = $rangeFields[0]
    if ($field.Current.AutomationId -ne 'com.microsoft.JobCustomPageRange_ValueText') {
        throw 'Only the proven native range textbox may receive input'
    }
    $field.SetFocus()
    Start-Sleep -Milliseconds 150
    if (-not $field.Current.HasKeyboardFocus -or [PrintInput]::GetForegroundWindow().ToInt64() -ne
            $window.Current.NativeWindowHandle) {
        throw 'Owned Print range textbox does not have foreground keyboard focus'
    }
    [System.Windows.Forms.SendKeys]::SendWait('^a2{TAB}')
    Start-Sleep -Milliseconds 400
    $value = $field.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
    if ($value -ne '2' -or $field.Current.HasKeyboardFocus) {
        throw "Native page range was not entered and committed: '$value'"
    }
}
$snapshot = [pscustomobject]@{ Window = Record $window; Children = @($all | ForEach-Object { Record $_ }) }
$snapshot | ConvertTo-Json -Depth 5 -Compress

function OneVisible($elements, $description) {
    $found = @($elements | Where-Object { $_.Current.IsEnabled -and -not $_.Current.IsOffscreen })
    if ($found.Count -ne 1) { throw "Expected one visible $description; found $($found.Count)" }
    return $found[0]
}

function InvokeButton($labels) {
    $button = OneVisible @($all | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.Name -in $labels
    }) ($labels -join '/')
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}

if ($Action -in @('Cancel', 'InspectPages')) { InvokeButton @('Cancel', '取消'); exit 0 }
if ($Action -eq 'Inspect') { exit 0 }

$pdf = @($all | Where-Object { $_.Current.Name -eq 'Microsoft Print to PDF' -and -not $_.Current.IsOffscreen })
if ($pdf.Count -ne 1 -or -not (Record $pdf[0]).Selected) { throw 'Visible Microsoft Print to PDF selection not found; print was not invoked' }

if ($Action -eq 'PrintAll') {
    $pages = OneVisible @($all | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and
        $_.Current.Name -match '^(All|All pages|全部|所有页|所有页面|全部頁面)$'
    }) 'All pages choice'
    if (-not (Record $pages).Selected) { throw 'All pages is not selected' }
}
InvokeButton @('Print', '打印', '列印')
