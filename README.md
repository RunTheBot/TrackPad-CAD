# TrackPad CAD

**Current path: HIDMaestro live bridge.** The SpaceMouse emulation test now works in 3Dconnexion Viewer, without our custom kernel driver. Run `gsudo pwsh -NoProfile -File run-trackpad.ps1`, focus the Viewer, and hold F8 while gesturing. See [the current setup and limitations](experiments/hidmaestro/README.md). The WDK was uninstalled at the user's request. The original VHF prototype notes below are historical.

Experimental Windows bridge for Blender-inspired trackpad navigation through a virtual six-axis HID controller.

**Status: compiled user-mode prototype and tested gesture engine; unbuilt VHF driver source. Not yet a working SpaceMouse replacement.** 3DxWare recognition and CAD motion have not been demonstrated. Generic HID enumeration does not establish 3DxWare compatibility, and this project does not claim support for every CAD application.

| Gesture | Output | Status |
|---|---|---|
| Two-finger movement | Rx/Ry orbit | Implemented in gesture engine |
| Shift + two-finger movement | Tx/Ty pan | Implemented |
| Pinch | Tz zoom | Implemented |
| Twist | Rz roll | Implemented |
| Three-finger tap | Button 1 pulse | Implemented in live tool; bind to frame selection per CAD |
| One-finger double tap | Pivot request at cursor | Recognized and logged; requires a CAD adapter |

This implements the requested interaction pattern, not a port of Blender source. Camera conventions, sensitivity, acceleration and pivot behavior remain controlled partly by the receiving CAD application. Motion is displacement accumulated over an approximately 8 ms interval, translated into bounded rate values. The next interval emits zero unless more movement arrives. This approximates direct manipulation; it cannot guarantee Blender's exact camera response through a rate-based interface.

## Run the available prototype

The current workspace build is `build/trackpad-cad.exe`. From this directory:

```powershell
.\build\trackpad-cad.exe devices
.\build\trackpad-cad.exe replay examples/gestures.txt
.\build\trackpad-cad.exe live chrome.exe
```

For live capture, focus the target application and **hold F8** while gesturing. Release F8 to stop. Ctrl+C exits the console. Choose the CAD executable's basename; for Onshape, use its browser process. Browser gating applies to the whole browser, not a particular tab. Default live mode prints six-axis values and sends no input to CAD.

The capture tool reads the Precision Touchpad HID collection through Windows Raw Input. It supports complete **parallel contact reports**, using contact IDs, tip switches, optional confidence flags and physically calibrated coordinates. Partial/hybrid reports are rejected instead of guessed. Device enumeration alone does not prove report delivery or successful decoding.

**Windows' own gestures are not suppressed.** Using the eventual driver output alongside native pan/scroll/zoom can cause double navigation. Reliable suppression, palm/tap tuning, hybrid report assembly, a tray UI and saved per-app sensitivity profiles remain development work. F8 is also visible to the foreground app; choose a test document where it has no disruptive binding.

## Build and test user mode

C++17 and CMake 3.20+ are required. Windows builds link the system HID and user32 libraries. No third-party runtime dependencies are needed for the supplied static MinGW build.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/mingw32-make.exe -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe
cmake --build build
ctest --test-dir build --output-on-failure
```

Alternatively use a Visual Studio CMake generator in a separate build directory. The gesture engine and replay/tests are portable C++; live capture and driver transport are Windows-only.

Tests exercise mapping, contact identity changes, Shift transitions, saturation, lift-off, stale input, invalid coordinates, taps, cancellation and report byte encoding. They do not validate a real HID parser, the kernel driver, timing under load or 3DxWare.

Replay input is `milliseconds shift contact_count [id x_mm y_mm]...`, one complete contact frame per line. Timestamps must be ordered. Output CSV contains motion samples at 125 Hz; semantic tap actions are printed separately to stderr. Settings are currently the `Settings` struct in `include/gesture.hpp`.

## Driver feasibility test

See [driver/README.md](driver/README.md). The source provides a root-enumerated KMDF device, VHF multi-axis descriptor, versioned buffered IOCTL, exclusive administrator-only control channel, timer-based reporting, zero-on-close and a 100 ms producer watchdog. VID/PID are left unassigned at zero for this local generic-device experiment. It does not impersonate 3Dconnexion hardware or implement vendor feature-report handshakes.

After the driver is built, validated, test-signed and installed in a driver test environment:

```powershell
# Elevated console; focuses only the named CAD/browser process.
.\build\trackpad-cad.exe rotation-test chrome.exe
# Then, only if the rotation test works:
.\build\trackpad-cad.exe live chrome.exe --driver
```

`rotation-test` waits for the target to be foreground and F8 to be held, emits Ry=80 for up to one second, then zero. Releasing F8, losing focus or Ctrl+C stops it early. Use the 3Dconnexion viewer and then an expendable Onshape model to distinguish driver enumeration, 3DxWare recognition and actual CAD navigation.

If Windows sees the multi-axis HID but 3DxWare does not, this architecture's main compatibility gate has failed. Do not spend more time on gesture polish before resolving recognized-device support with 3Dconnexion or choosing a CAD-specific backend. A failure with this descriptor does not prove all generic descriptors are rejected.

## Local verification — 2026-09-11

- Windows build 26200; 3DxWare directory present.
- Windows SDK 10.0.26100.0 found; WDK `vhf.h` was not found under the installed SDK headers.
- User-mode executable built with MinGW GCC 13.2; CTest passed.
- Replay produced expected motion and tap actions, with zero on stop.
- Raw Input enumerated one Precision Touchpad collection: VID 0488, PID 104B.
- No driver installed, no boot settings changed, no CAD motion tested.

## Design references

- [Microsoft Virtual HID Framework](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-) describes the KMDF source driver, `vhfkm.lib` and VHF lower filter used here.
- [VHF configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/vhf/ns-vhf-_vhf_config) describes descriptor and identity fields.
- [Precision Touchpad HID collections](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-required-hid-top-level-collections) describes the device collection exposed to Windows.
- [3Dconnexion supported software](https://3dconnexion.com/us/software/) lists application integrations; it is not a promise of generic virtual-device compatibility.
