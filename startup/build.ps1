param([string]$CompilerRoot, [string]$SdkRoot, [string]$SdkVersion, [switch]$FixturesOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if (!$CompilerRoot) {
    if ($env:VCToolsInstallDir) { $CompilerRoot=$env:VCToolsInstallDir }
    else {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (!(Test-Path -LiteralPath $vswhere)) {throw 'C++ tools not found; pass -CompilerRoot'}
        $vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$vsPath) {throw 'Install Visual Studio C++ x86 tools or pass -CompilerRoot'}
        $CompilerRoot=(Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC\Tools\MSVC') -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).FullName
    }
}
if (!$SdkRoot) {
    if ($env:WindowsSdkDir) { $SdkRoot=$env:WindowsSdkDir }
    else { $SdkRoot=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' }
}
if (!$SdkVersion) {
    $SdkVersion=(Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'Include') -Directory | Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'um\windows.h')) -and
        (Test-Path -LiteralPath (Join-Path $SdkRoot "Lib\$($_.Name)\um\x86\user32.lib"))
    } | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
}
$compiler=Join-Path $CompilerRoot 'bin\Hostx64\x86\cl.exe'
if (!(Test-Path -LiteralPath $compiler) -or !$SdkVersion) {throw 'C++ compiler/Windows SDK not found'}
$csharp=(Get-ChildItem -LiteralPath (Join-Path $env:WINDIR 'Microsoft.NET\Framework64') -Filter csc.exe -Recurse | Sort-Object FullName -Descending | Select-Object -First 1).FullName
if (!$csharp) {throw '.NET Framework C# compiler not found'}
$buildDir=Join-Path $taskRoot 'build\startup'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$includes=@("/I$CompilerRoot\include", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\ucrt")
$libraries=@("/LIBPATH:$CompilerRoot\lib\x86", "/LIBPATH:$SdkRoot\Lib\$SdkVersion\um\x86", "/LIBPATH:$SdkRoot\Lib\$SdkVersion\ucrt\x86")
Push-Location $buildDir
try {
    if (!$FixturesOnly) {
        & $compiler /nologo /LD /O2 /MT /EHsc /W4 /std:c++17 @includes "$PSScriptRoot\dinput8.cpp" /link "/DEF:$PSScriptRoot\dinput8.def" /OUT:dinput8.dll @libraries dxguid.lib user32.lib bcrypt.lib
        if($LASTEXITCODE -ne 0) {throw 'Startup proxy compilation failed'}
    }
    foreach($fixture in @(@('smoke','Rocksmith2014','dxguid.lib'),@('classifier_smoke','classifier_smoke','bcrypt.lib'),@('profile_boot_smoke','profile_boot_smoke','bcrypt.lib'),@('intro_skip_smoke','intro_skip_smoke','bcrypt.lib'),@('early_qos_smoke','early_qos_smoke','kernel32.lib'))) {
        & $compiler /nologo /O2 /MT /EHsc /W4 /std:c++17 @includes "$PSScriptRoot\$($fixture[0]).cpp" /link "/OUT:$($fixture[1]).exe" @libraries $fixture[2]
        if($LASTEXITCODE -ne 0) {throw "Fixture compilation failed: $($fixture[0])"}
    }
    if (!$FixturesOnly) {
        & $csharp /nologo /target:winexe /platform:anycpu /optimize+ /out:RocksmithLaunch.exe "$PSScriptRoot\StartupLauncher.cs"
        if($LASTEXITCODE -ne 0) {throw 'Launcher compilation failed'}
    }
    & $csharp /nologo /target:exe /platform:anycpu /optimize+ /main:LauncherSmoke /out:launcher_smoke.exe "$PSScriptRoot\StartupLauncher.cs" "$PSScriptRoot\LauncherSmoke.cs"
    if($LASTEXITCODE -ne 0) {throw 'Launcher fixture compilation failed'}
    & $csharp /nologo /target:winexe /platform:anycpu /optimize+ /out:launcher-child.exe "$PSScriptRoot\LauncherChild.cs"
    if($LASTEXITCODE -ne 0) {throw 'Child fixture compilation failed'}
} finally {Pop-Location}
