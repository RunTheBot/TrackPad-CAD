param(
    [string]$KitVersion = '10.0.28000.0',
    [string]$SdkRoot = "$PSScriptRoot\..\build\toolchain\sdk\c",
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$out = Join-Path $projectRoot 'build\driver'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -property installationPath
$compilerRoot = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
$bin = "$compilerRoot\bin\Hostx64\x64"
$includes = @("$WdkRoot\Include\$KitVersion\km", "$WdkRoot\Include\$KitVersion\shared", "$WdkRoot\Include\wdf\kmdf\1.15", "$SdkRoot\Include\$KitVersion\shared", "$SdkRoot\Include\$KitVersion\ucrt", "$compilerRoot\include")
foreach ($path in $includes) { if (!(Test-Path -LiteralPath $path)) { throw "Missing include directory: $path" } }
$compile = @('/nologo','/c','/W4','/WX','/external:W0','/kernel','/GS','/O2','/Z7','/Qspectre','/D_AMD64_','/DAMD64','/D_WIN64','/DWINVER=0x0A00','/D_WIN32_WINNT=0x0A00','/DKMDF_VERSION_MAJOR=1','/DKMDF_VERSION_MINOR=15')
$compile += $includes | ForEach-Object { '/external:I' + $_ }
$compile += @(('/Fo' + "$out\driver.obj"), "$PSScriptRoot\driver.c")
& "$bin\cl.exe" @compile
if ($LASTEXITCODE) { throw "Driver compilation failed ($LASTEXITCODE)" }
$link = @('/nologo','/DRIVER','/SUBSYSTEM:NATIVE,10.00','/ENTRY:FxDriverEntry','/MACHINE:X64','/NODEFAULTLIB','/INCREMENTAL:NO','/DYNAMICBASE','/NXCOMPAT','/DEBUG',('/OUT:' + "$out\TrackPadCAD.sys"),('/PDB:' + "$out\TrackPadCAD.pdb"), "$out\driver.obj")
$link += @(('/LIBPATH:' + "$WdkRoot\Lib\$KitVersion\km\x64"), ('/LIBPATH:' + "$WdkRoot\Lib\wdf\kmdf\x64\1.15"))
$link += @('ntoskrnl.lib','hal.lib','wmilib.lib','BufferOverflowK.lib','wdfdriverentry.lib','wdfldr.lib','vhfkm.lib')
& "$bin\link.exe" @link
if ($LASTEXITCODE) { throw "Driver link failed ($LASTEXITCODE)" }
Copy-Item -LiteralPath "$PSScriptRoot\TrackPadCAD.inf" -Destination $out -Force
$verifier = "$WdkRoot\Tools\$KitVersion\x64\infverif.exe"
& $verifier /w "$out\TrackPadCAD.inf"
if ($LASTEXITCODE) { throw "INF validation failed ($LASTEXITCODE)" }
& "$WdkRoot\bin\$KitVersion\x86\Inf2Cat.exe" "/driver:$out" /os:10_X64 /uselocaltime
if ($LASTEXITCODE) { throw "Catalog generation failed ($LASTEXITCODE)" }
Write-Host "Built unsigned driver: $out\TrackPadCAD.sys"


