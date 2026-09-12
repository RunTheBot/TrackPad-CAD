param([switch]$Run, [switch]$Validate)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$dll = "$root\build\hidmaestro\HIDMaestro.Core.dll"
[Reflection.Assembly]::LoadFrom($dll) | Out-Null
$hex = (Get-Content -Raw "$PSScriptRoot\spacemouse-pro.hex").Trim()
$builder = [HIDMaestro.HMProfileBuilder]::new()
$profile = $builder.Id('trackpad-spacemouse-test').Name('SpaceMouse Pro test').Vendor('3Dconnexion').Vid(0x046D).Pid(0xC62B).ProductString('SpaceMouse Pro').ManufacturerString('3Dconnexion').Connection('usb').DescriptorHex($hex).InputReportSize(7).Build()
Write-Host "Profile parsed: $($hex.Length/2) descriptor bytes; VID 046D PID C62B."
if (!$Run -and !$Validate) { return }
Add-Type -ReferencedAssemblies @($dll, "System.Diagnostics.Process", "System.ComponentModel.Primitives", "System.Console", "System.Threading", "System.Threading.Thread", "System.Runtime") -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Threading;
using System.Runtime.InteropServices;
using HIDMaestro;
public static class SpaceMouseProbe {
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
  static bool ViewerActive() {
    uint pid; GetWindowThreadProcessId(GetForegroundWindow(),out pid);
    try { return Process.GetProcessById((int)pid).ProcessName.Equals("3DxViewer10",StringComparison.OrdinalIgnoreCase); } catch { return false; }
  }
  public static void Pulse(HMController c) {
    byte[] zeroT={1,0,0,0,0,0,0}, zeroR={2,0,0,0,0,0,0}, turn={2,0,0,80,0,0,0};
    Console.WriteLine("Waiting up to 90 seconds for 3Dconnexion Viewer foreground. No CAD motion elsewhere.");
    var clock=Stopwatch.StartNew();
    try {
      while(clock.ElapsedMilliseconds<90000 && !ViewerActive()) { c.SubmitRawExtendedReport(zeroR); Thread.Sleep(20); }
      if(!ViewerActive()) throw new TimeoutException("Viewer was not activated; no rotation sent.");
      // Re-check after the countdown: previously this could submit zero frames
      // on focus loss and still print a misleading success message.
      Thread.Sleep(3000);
      if(!ViewerActive()) throw new InvalidOperationException("Viewer lost focus during countdown; no motion sent.");
      clock.Restart();
      int frames=0;
      while(clock.ElapsedMilliseconds<1000 && ViewerActive()) {
        // SpaceMouse firmware streams separate translation and rotation reports.
        // Give each shared-memory update time to be consumed before replacing it.
        c.SubmitRawExtendedReport(zeroT); Thread.Sleep(8);
        if(!ViewerActive()) break;
        c.SubmitRawExtendedReport(turn); ++frames; Thread.Sleep(8);
      }
      Console.WriteLine("Submitted " + frames + " nonzero rotation reports; delivery is not yet verified.");
      if(frames==0) throw new InvalidOperationException("No nonzero reports submitted: viewer focus was lost.");
    } finally { c.SubmitRawExtendedReport(zeroT); Thread.Sleep(20); c.SubmitRawExtendedReport(zeroR); }
    Thread.Sleep(10000);
  }
}
'@
if (!$Run) { Write-Host 'Test code compiled. No driver or certificate installed.'; return }
$ctx = [HIDMaestro.HMContext]::new()
$controller = $null
try {
    Write-Host 'Installing HIDMaestro UMDF driver and local code-signing certificate...'
    $ctx.InstallDriver()
    Write-Host 'Creating SpaceMouse test device...'
    $controller = $ctx.CreateController($profile)
    [SpaceMouseProbe]::Pulse($controller)
} finally {
    if ($controller) { $controller.Dispose() }
    $ctx.Dispose()
}



