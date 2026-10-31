param([Parameter(Mandatory)][string]$CMakeArgumentsJson)
$ErrorActionPreference = 'Stop'
$taskCmakeArguments = @($CMakeArgumentsJson | ConvertFrom-Json)
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstall = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskInstall) { throw 'A Visual Studio C++ toolchain is required' }
Import-Module "$taskInstall/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $taskInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
& cmake @taskCmakeArguments
exit $LASTEXITCODE
