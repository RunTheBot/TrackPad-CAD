param(
    [ValidatePattern('^[A-Za-z0-9_. -]+\.exe$')][string]$Target = 'chrome.exe',
    [ValidateRange(0,86400)][int]$Seconds = 0,
    [ValidateRange(0.1,50.0)][double]$Sensitivity = 5.0,
    [string]$Config,
    [string]$CaptureExecutable,
    [switch]$Validate
)
$ErrorActionPreference='Stop'
if(!$CaptureExecutable){$CaptureExecutable=Join-Path $PSScriptRoot 'build/trackpad-cad.exe'}
if(!(Test-Path -LiteralPath $CaptureExecutable)){throw "Capture executable missing: $CaptureExecutable"}
if($Config){
    $configPath=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Config)
    $settings=Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
    if($null -ne $settings.Sensitivity){$Sensitivity=[double]$settings.Sensitivity}
    $StartEnabled=($settings.StartEnabled -eq $true)
    $ToggleKey=if($null -ne $settings.ToggleKey){[string]$settings.ToggleKey}else{'F8'}
    $NativeExecutables=@($settings.NativeExecutables | ForEach-Object {[string]$_})
    $BrowserUrlPrefixes=@($settings.BrowserUrlPrefixes | ForEach-Object {[string]$_})
}else{$StartEnabled=$false;$ToggleKey='F8';$NativeExecutables=@();$BrowserUrlPrefixes=@()}
if($Sensitivity -lt 0.1 -or $Sensitivity -gt 50){throw 'Sensitivity in the configuration must be between 0.1 and 50.'}
$dll=Join-Path $PSScriptRoot 'build/hidmaestro/HIDMaestro.Core.dll'
if(!(Test-Path $dll)){throw 'HIDMaestro prebuilt DLL missing. See experiments/hidmaestro/README.md.'}
[Reflection.Assembly]::LoadFrom($dll) | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'src/HidMaestroBridge.cs') -ReferencedAssemblies @($dll,'System.Diagnostics.Process','System.ComponentModel.Primitives','System.Console','System.Threading','System.Threading.Thread','System.Runtime','System.Collections','System.Drawing.Common','System.Drawing.Primitives','System.Private.Windows.Core','System.Private.Windows.GdiPlus','System.Windows.Extensions','System.Windows.Forms','System.Windows.Forms.Primitives','System.Net.Primitives','System.Net.Sockets','System.Text.Encoding.Extensions')
[HidMaestroBridge]::Check()
if($Validate){return}
$hex=(Get-Content -Raw (Join-Path $PSScriptRoot 'experiments/hidmaestro/spacemouse-pro.hex')).Trim()
$builder=[HIDMaestro.HMProfileBuilder]::new()
$profile=$builder.Id('trackpad-spacemouse-live').Name('SpaceMouse Pro test').Vendor('3Dconnexion').Vid(0x046D).Pid(0xC62B).ProductString('SpaceMouse Pro').ManufacturerString('3Dconnexion').Connection('usb').DescriptorHex($hex).InputReportSize(7).Build()
$ctx=[HIDMaestro.HMContext]::new()
$controller=$null
try {
    if(!$ctx.IsDriverInstalled){Write-Host 'Installing the HIDMaestro virtual-device driver...';$ctx.InstallDriver();if(!$ctx.IsDriverInstalled){throw 'HIDMaestro driver installation did not complete.'}}
    $controller=$ctx.CreateController($profile)
    [HidMaestroBridge]::Run($controller,$CaptureExecutable,$Target,$Seconds,$Sensitivity,$StartEnabled,$ToggleKey,$NativeExecutables,$BrowserUrlPrefixes)
} finally {if($controller){$controller.Dispose()};$ctx.Dispose()}
