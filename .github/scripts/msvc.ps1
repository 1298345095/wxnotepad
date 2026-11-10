param([Parameter(Mandatory)][string]$CMakeArgumentsJson)
$ErrorActionPreference = 'Stop'
$taskCmakeArguments = @($CMakeArgumentsJson | ConvertFrom-Json)
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstall = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskInstall) { throw 'A Visual Studio C++ toolchain is required' }
Import-Module "$taskInstall/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $taskInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
if ($taskCmakeArguments -contains '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache') {
    # sccache #2864 can corrupt non-UTF-8 /showIncludes output and lose Ninja header dependencies.
    $taskEnglishMessages = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/1033/clui.dll'
    if (-not (Test-Path -LiteralPath $taskEnglishMessages)) {
        throw 'sccache requires English MSVC diagnostic resources (1033/clui.dll); use an uncached build until installed. See mozilla/sccache#2864.'
    }
}
# CMake and Ninja must agree on MSVC's /showIncludes prefix from the first configure.
$env:VSLANG = '1033'
# MSVC falls back to its installed language when English resources are absent.
# Match the code page CMake uses to decode with the one cl uses for output.
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
& cmake @taskCmakeArguments
exit $LASTEXITCODE
