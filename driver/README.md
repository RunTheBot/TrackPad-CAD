# Experimental VHF driver

This driver source has **not been compiled or loaded** in the current environment. The Windows SDK is installed, but the necessary WDK headers/toolset are absent. Treat the project and INF as a starting implementation pending WDK compiler, INF validation, Driver Verifier and hardware testing.

## Build prerequisites

Use Visual Studio 2022 C++ tools and the Windows Driver Kit matching SDK 10.0.26100.0 (or retarget both together). Open `TrackPadCAD.vcxproj` in Visual Studio, or run from a VS developer shell:

```powershell
msbuild TrackPadCAD.vcxproj /p:Configuration=Debug /p:Platform=x64
```

The project links `vhfkm.lib`; the INF installs VHF as a lower filter. Run the WDK's INF verification and code analysis, fix any findings, then generate and test-sign the catalog using the standard WDK driver testing workflow. Signing is deliberately not automated here. Do not assume a successful CMake user-mode build validates this project.

## Install and evaluate

Use a Windows driver test machine or VM configured for test-signed drivers. This repository does not change Secure Boot, enable test signing, trust certificates, install the driver or reboot the computer.

After validating and signing the built package, use the WDK's `devcon` from an elevated shell to create the root devnode and install the package:

```powershell
devcon install <absolute-path-to-built-and-signed-TrackPadCAD.inf> 'Root\TrackPadCAD'
```

Substitute the real package path. `pnputil /add-driver` alone does not create this root device. Install only one instance because the control endpoint has a fixed name.

1. Confirm Device Manager has no error on the TrackPad CAD device or its HID child.
2. Run `trackpad-cad devices` and confirm a Generic Desktop/Multi-axis Controller collection (`page=1 usage=8`).
3. Inspect 3DxWare/3Dconnexion Viewer for recognition of the experimental device.
4. Run `trackpad-cad rotation-test <target.exe>` elevated. Hold F8 in the target; confirm rotation and release after one second.
5. Confirm release on F8 up, focus loss, producer exit/crash and device disable. Validate watchdog timing under load.
6. Repeat in Onshape, then each intended CAD integration. Record versions and recognition separately from camera behavior.

To remove only this experiment's devnode:

```powershell
devcon remove 'Root\TrackPadCAD'
```

If removing the staged driver package too, first use `pnputil /enum-drivers` to identify its exact `oemNN.inf` and verify the provider/description. Do not remove another HID or 3Dconnexion driver.

## ABI and timing

`include/protocol.h` defines a 20-byte buffered command: 32-bit ABI version, six signed 16-bit axes and 32 button bits. Invalid sizes, versions and out-of-range axes are rejected. Only one elevated administrator/system client can open `\\.\TrackPadCAD`. Access is constrained to the virtual device; no physical-device handles or arbitrary memory addresses are accepted.

The driver sends report 1 (13 bytes including ID) and report 2 (5 bytes including ID). The descriptor describes absolute rate axes, not relative mouse counts. A passive one-shot timer rearms at approximately 8 ms. It sends the latest state and discards it after 100 ms without producer writes. A close resets the state; D0 exit stops the timer and submits zero; cleanup deletes VHF synchronously. Real timer jitter, dropped/duplicated pulses between independent producer/driver clocks and VHF buffering behavior require measurement. This is not a hard-real-time delivery guarantee.

No vendor feature reports or hardware-identification handshake are supplied. Windows enumeration and 3DxWare acceptance are separate tests. Keep VID/PID zero for this generic experiment; production device identity requires an appropriate assigned identity and a confirmed compatibility route.
