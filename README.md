# TrackPad CAD

**Current path: HIDMaestro tray bridge.** Double-click `TrackPad CAD.exe` and press F8 to enable or disable it. The tray icon is green when enabled and gray when disabled. 3DxWare's live SDK routing state selects the active CAD client automatically. See [the current setup and limitations](experiments/hidmaestro/README.md). The original VHF prototype notes below are historical.

Experimental Windows bridge for Blender-inspired trackpad navigation through a virtual six-axis HID controller.

**Status: working local prototype.** HIDMaestro presents a SpaceMouse Pro-compatible device, 3DxWare recognizes it, and motion was confirmed in the 3Dconnexion Viewer. CAD support depends on each application's 3DxWare integration.

| Gesture | Output | Status |
|---|---|---|
| Two-finger movement | Rx/Ry orbit | Implemented in gesture engine |
| Shift + two-finger movement | Tx/Ty pan | Implemented |
| Pinch | Tz zoom | Implemented |
| Twist | Rz roll | Implemented |
| Three-finger tap | Button 1 pulse | Implemented in live tool; bind to frame selection per CAD |
| One-finger double tap | Pivot request at cursor | Recognized and logged; requires a CAD adapter |

This implements the requested interaction pattern, not a port of Blender source. Camera conventions, sensitivity, acceleration and pivot behavior remain controlled partly by the receiving CAD application. Motion is displacement accumulated over an approximately 8 ms interval, translated into bounded rate values. The next interval emits zero unless more movement arrives. This approximates direct manipulation; it cannot guarantee Blender's exact camera response through a rate-based interface.

## Run TrackPad CAD

Double-click `TrackPad CAD.exe`. Accept the elevation prompt, focus a CAD application connected through the 3Dconnexion SDK, then press **F8** once to enable input. Press F8 again to disable it. Right-click the tray icon for the same controls or to exit.

Edit `%LOCALAPPDATA%\TrackPad CAD\runtime\TrackPad CAD.json` to change startup behavior:

```json
{
  "Sensitivity": 30.0,
  "StartEnabled": false,
  "NativeExecutables": ["blender.exe", "FreeCAD.exe", "Fusion360.exe", "SLDWORKS.exe"],
  "BrowserUrlPrefixes": ["https://cad.onshape.com/"]
}
```

The diagnostic capture build is `build/trackpad-cad.exe`. From this directory:

```powershell
.\build\trackpad-cad.exe devices
.\build\trackpad-cad.exe replay examples/gestures.txt
.\build\trackpad-cad.exe live chrome.exe
```

## Packaged Windows app

`TrackPad CAD.exe` is the single-file application. On first launch it extracts its embedded bridge, HID descriptor, browser detector and default configuration to `%LOCALAPPDATA%\TrackPad CAD\runtime`, then starts the tray app. The configuration file is preserved across launches at `%LOCALAPPDATA%\TrackPad CAD\runtime\TrackPad CAD.json`.

`TrackPad CAD Setup.exe` installs that same single-file application per-user at `%LOCALAPPDATA%\Programs\TrackPad CAD\TrackPad CAD.exe`, adds desktop and Start menu shortcuts, registers it in **Settings → Apps → Installed apps**, and starts it. PowerShell 7 remains required because the embedded HIDMaestro bridge is compiled at launch. The tray icon is a diagonal green/gray split with the TrackPad CAD mark.

Use **Uninstall** from Windows Installed apps to remove TrackPad CAD. The uninstaller asks for confirmation, then removes the installed executable, runtime files, saved `TrackPad CAD.json`, browser-extension files, shortcuts, and its Installed Apps registration.

Native CAD programs are selected by the foreground executable allowlist in the configuration. Browser CAD is selected by the extension in `browser-extension`: it observes the page's 3DconnexionJS WebSocket before TLS encryption and publishes a short-lived SDK/URL heartbeat to `127.0.0.1:17831`. The bridge requires an open SDK connection, a focused visible tab, and a matching `BrowserUrlPrefixes` entry.

To load the browser detector in Chrome or Edge, open the extensions page, enable Developer mode, choose **Load unpacked**, and select the `browser-extension` directory. The extension runs at document start so reload existing CAD tabs after loading it.

The capture tool reads the Precision Touchpad HID collection through Windows Raw Input. It supports complete **parallel contact reports**, using contact IDs, tip switches, optional confidence flags and physically calibrated coordinates. Partial/hybrid reports are rejected instead of guessed. Device enumeration alone does not prove report delivery or successful decoding.

While enabled and the target is foreground, the tray bridge uses Windows 11's dynamic touchpad API to temporarily disable Windows two-finger pan and pinch handling. It saves the current settings and restores them when F8 is toggled off, focus leaves the target, or the bridge exits. The override is not written to the user profile, and physical mouse-wheel input remains available.

## For developers

The application has three cooperating layers. `src/main.cpp` and `include/gesture.hpp` turn decoded touchpad contacts into six-axis frames. `src/windows.cpp` reads Precision Touchpad Raw Input. `src/HidMaestroBridge.cs`, compiled by `run-trackpad.ps1`, owns the tray icon, F8 hotkey, target selection, temporary Windows gesture suppression, and writes frames to HIDMaestro.

`src/launcher.cpp` is the Windows GUI entry point. It embeds the PowerShell bridge, C# source, HIDMaestro assembly, descriptor, configuration template, and browser extension as resources, extracts them to the local runtime directory, then launches PowerShell 7. `tools/make-icon.ps1` draws the embedded diagonal green/gray `.ico`; it does not use an external image asset or image generator. `TrackPad CAD Setup.exe` is the same binary and switches to per-user installation based on its filename.

The browser extension is deliberately small. `browser-extension/main-hook.js` runs in the page's main world and observes creation of 3DconnexionJS WebSockets. `relay.js` forwards an active, focused CAD tab heartbeat to the loopback listener in the bridge. Keep this boundary narrow: the extension identifies a valid browser CAD context, while all HID output remains in the desktop process.

To make code changes, build the launcher target after editing any embedded resource so CMake regenerates the resource bundle:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/mingw32-make.exe -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe
cmake --build build --target trackpad-cad-launcher
```

Use the replay command before testing live input. It runs the gesture engine against `examples/gestures.txt` and prints the generated frames without touching the virtual HID device:

```powershell
.\TrackPad CAD.exe --capture replay .\examples\gestures.txt
```

For live development, exit the tray app before relaunching it so the global F8 hotkey is available. The HIDMaestro driver must be installed and 3DxWare must be running. Test changes first in the 3Dconnexion Viewer, then in a CAD application. Do not modify Windows touchpad registry settings; the bridge uses the dynamic Windows API and restores the prior gesture state when it deactivates.

## Build and test user mode

C++17 and CMake 3.20+ are required. Windows builds link the system HID and user32 libraries. No third-party runtime dependencies are needed for the supplied static MinGW build.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/mingw32-make.exe -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe
cmake --build build
ctest --test-dir build --output-on-failure
```

Alternatively use a Visual Studio CMake generator in a separate build directory. The gesture engine and replay/tests are portable C++; live capture and driver transport are Windows-only.

Tests exercise mapping, contact identity changes, Shift transitions, saturation, lift-off, stale input, invalid coordinates, taps, cancellation and report byte encoding. Live HID motion was also confirmed with 3DxWare's viewer.

Replay input is `milliseconds shift contact_count [id x_mm y_mm]...`, one complete contact frame per line. Timestamps must be ordered. Output CSV contains motion samples at 125 Hz; semantic tap actions are printed separately to stderr. Settings are currently the `Settings` struct in `include/gesture.hpp`.

## Historical VHF driver experiment

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

## Local verification — 2026-09-12

- Windows build 26200; 3DxWare directory present.
- User-mode executable built with MinGW GCC 13.2; CTest passed.
- Replay produced expected motion and tap actions, with zero on stop.
- Raw Input enumerated one Precision Touchpad collection: VID 0488, PID 104B.
- HIDMaestro's signed UMDF driver is installed; the standalone WDK was removed to recover disk space.
- 3DxWare recognized the virtual SpaceMouse Pro and rotation was confirmed in the 3Dconnexion Viewer.

## Design references

- [Microsoft Virtual HID Framework](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-) describes the KMDF source driver, `vhfkm.lib` and VHF lower filter used here.
- [VHF configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/vhf/ns-vhf-_vhf_config) describes descriptor and identity fields.
- [Precision Touchpad HID collections](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-required-hid-top-level-collections) describes the device collection exposed to Windows.
- [3Dconnexion supported software](https://3dconnexion.com/us/software/) lists application integrations; it is not a promise of generic virtual-device compatibility.
