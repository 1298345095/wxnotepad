param([string]$Run = 'installed-01')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($env:COMPUTERNAME -ne 'VIRTUAL') { throw 'This entry point is only for the isolated test VM.' }
if ($Run -notmatch '^installed-[0-9]+$') { throw 'Use a fresh installed-NN evidence name.' }
$root = Join-Path $env:LOCALAPPDATA 'WxNotepadWindowTools288-20261127'
$python = Join-Path $env:LOCALAPPDATA 'Programs/Python/Python314/python.exe'
$config = Join-Path $env:LOCALAPPDATA 'Notepad4/Notepad4.ini'
if (Test-Path -LiteralPath $config) { throw 'Refusing to replace preexisting installed configuration.' }
$arguments = @('-X', 'utf8', "$root/inputs/.github/scripts/ci.py", 'probe', '--report-dir',
    "$root/$Run-probe", '--timeout', '120', '--', $python, '-X', 'utf8',
    "$root/inputs/tests/file_favorites_installed_windows.py", '--package', "$root/package",
    '--owned-root', $root, '--vm-computer-name', 'VIRTUAL', '--evidence', "$root/$Run")
$process = Start-Process -FilePath $python -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput "$root/$Run.log" -RedirectStandardError "$root/$Run-error.log"
@{ exit_code=$process.ExitCode; config_exists=(Test-Path -LiteralPath $config);
   completed=(Get-Date).ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath "$root/$Run-exit.json" -Encoding utf8
if ($process.ExitCode -ne 0 -or (Test-Path -LiteralPath $config)) { exit 1 }
exit 0
