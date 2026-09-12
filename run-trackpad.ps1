param(
    [ValidatePattern('^[A-Za-z0-9_. -]+\.exe$')][string]$Target = '3DxViewer10.exe',
    [ValidateRange(0,86400)][int]$Seconds = 0,
    [ValidateRange(0.1,50.0)][double]$Sensitivity = 5.0,
    [switch]$Validate
)
$ErrorActionPreference='Stop'
$dll=Join-Path $PSScriptRoot 'build/hidmaestro/HIDMaestro.Core.dll'
if(!(Test-Path $dll)){throw 'HIDMaestro prebuilt DLL missing. See experiments/hidmaestro/README.md.'}
[Reflection.Assembly]::LoadFrom($dll) | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'src/HidMaestroBridge.cs') -ReferencedAssemblies @($dll,'System.Diagnostics.Process','System.ComponentModel.Primitives','System.Console','System.Threading','System.Threading.Thread','System.Runtime','System.Collections')
[HidMaestroBridge]::Check()
if($Validate){return}
$hex=(Get-Content -Raw (Join-Path $PSScriptRoot 'experiments/hidmaestro/spacemouse-pro.hex')).Trim()
$builder=[HIDMaestro.HMProfileBuilder]::new()
$profile=$builder.Id('trackpad-spacemouse-live').Name('SpaceMouse Pro test').Vendor('3Dconnexion').Vid(0x046D).Pid(0xC62B).ProductString('SpaceMouse Pro').ManufacturerString('3Dconnexion').Connection('usb').DescriptorHex($hex).InputReportSize(7).Build()
$ctx=[HIDMaestro.HMContext]::new()
$controller=$null
try {
    if(!$ctx.IsDriverInstalled){throw 'HIDMaestro driver is not installed. Run the approved setup test first.'}
    $controller=$ctx.CreateController($profile)
    [HidMaestroBridge]::Run($controller,(Join-Path $PSScriptRoot 'build/trackpad-cad.exe'),$Target,$Seconds,$Sensitivity)
} finally {if($controller){$controller.Dispose()};$ctx.Dispose()}
