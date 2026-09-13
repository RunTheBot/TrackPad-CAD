# TrackPad CAD

TrackPad CAD turns Precision Touchpad gestures into SpaceMouse-style six-axis input for CAD programs that work with 3DxWare. It uses HIDMaestro to present a SpaceMouse Pro-compatible virtual HID device.

## User guide

### 1. Install TrackPad CAD

Run `TrackPad CAD Setup.exe`. It installs TrackPad CAD for the current Windows user, adds desktop and Start menu shortcuts.

### Install 3DxWare

Download and install **3DxWare 10 for Windows** from [3Dconnexion's driver page](https://3dconnexion.com/us/drivers-application/3dxware-10/). Complete the installer and restart Windows if it asks you to. TrackPad CAD uses 3DxWare to route the virtual SpaceMouse input to your CAD application.

### 2. Start it

Open TrackPad CAD from either shortcut and accept the elevation prompt. Start 3DxWare if it is not already running.

Focus the CAD program you want to control, then press **F8**. The tray icon turns green when TrackPad CAD is enabled and gray when it is disabled. Press F8 again, use the tray menu, or exit the tray app to stop it.

Pin the tray icon if needed.

### 3. Use the trackpad

| Gesture | Result |
| --- | --- |
| Two-finger move | Orbit |
| Shift + two-finger move | Pan |
| Pinch | Zoom |
| Twist | Roll |
| Three-finger tap | Button 1 pulse for frame selection |
| One-finger double tap | Pivot request at the cursor |

Movement stops when your fingers stop. Assign Button 1 to frame selection in the CAD application's 3DxWare profile if you want the three-finger tap to frame the current selection. Pivot requests are recognized, but changing the actual orbit pivot needs CAD-specific support.

### 4. Adjust sensitivity and supported apps

After the first launch, the settings file is here:

```text
%LOCALAPPDATA%\TrackPad CAD\runtime\TrackPad CAD.json
```

```json
{
  "Sensitivity": 30.0,
  "StartEnabled": false,
  "NativeExecutables": ["blender.exe", "FreeCAD.exe", "Fusion360.exe", "SLDWORKS.exe"],
  "BrowserUrlPrefixes": ["https://cad.onshape.com/"]
}
```

Set `Sensitivity` higher for faster movement. TrackPad CAD only sends input to native applications listed in `NativeExecutables` while that application is focused.

### 5. Set up Onshape

1. Open `chrome://extensions` or `edge://extensions`.
2. Turn on **Developer mode**, choose **Load unpacked**, and select `%LOCALAPPDATA%\TrackPad CAD\runtime\browser-extension`.
3. Reload Onshape, open a document at `https://cad.onshape.com/`, and click in the graphics area.
4. Enable TrackPad CAD with F8 and use the gestures above.

TrackPad CAD sends motion only when the focused Onshape tab has an active 3DconnexionJS connection. If it does not respond, make sure the tray icon is green, reload the tab, and check that `BrowserUrlPrefixes` still includes `https://cad.onshape.com/`.

### 6. Uninstall

Use **Uninstall** from Windows Installed apps. After confirmation, it removes the installed executable, saved settings, browser-extension runtime files, shortcuts, and Installed Apps registration.

## Developers

### Project layout

- `include/gesture.hpp` maps touchpad contacts to six-axis frames.
- `src/main.cpp` provides replay and live capture entry points.
- `src/windows.cpp` reads Precision Touchpad data through Windows Raw Input.
- `src/HidMaestroBridge.cs` owns the tray icon, F8 hotkey, target checks, temporary gesture suppression, and HIDMaestro output.
- `run-trackpad.ps1` compiles and starts the bridge.
- `src/launcher.cpp` packages the application and installs or uninstalls it.
- `browser-extension/` detects 3DconnexionJS activity in browser CAD.
- `tools/make-icon.ps1` generates the embedded diagonal green/gray icon.

The packaged EXE embeds the bridge, HIDMaestro assembly, HID descriptor, default configuration, and extension files. On first launch it extracts them to `%LOCALAPPDATA%\TrackPad CAD\runtime`. The existing JSON file is kept when the application updates.

The browser extension only reports whether a focused tab has an active 3DconnexionJS WebSocket. It sends that heartbeat to the local bridge at `127.0.0.1:17831`; HID reports are produced only by the desktop process.

### Build

Install CMake 3.20+, MinGW GCC, and PowerShell 7. From the repository root:

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/mingw32-make.exe `
  -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe

cmake --build build --target trackpad-cad-launcher
```

The build creates these files in the repository root:

```text
TrackPad CAD.exe
TrackPad CAD Setup.exe
```

`TrackPad CAD.exe` is the single-file application. `TrackPad CAD Setup.exe` is the same binary with installer behavior selected by its filename.

### Test changes

Run the gesture tests:

```powershell
ctest --test-dir build --output-on-failure
```

Use replay before testing live input:

```powershell
.\TrackPad CAD.exe --capture replay .\examples\gestures.txt
```

Replay prints the output frames without sending data to the virtual HID device. Exit the tray application before starting another live instance so F8 is free. For live tests, verify motion in the 3Dconnexion Viewer before testing a CAD program.

Windows touchpad settings are changed only through the dynamic touchpad API while TrackPad CAD is active for a supported target. The bridge restores the previous gesture state when input is turned off, focus leaves the target, or the bridge exits. It does not change touchpad registry settings.

### Historical VHF prototype

The `driver/` folder contains the earlier KMDF/VHF experiment. It is not the current shipping path. See [driver/README.md](driver/README.md) for its build and test notes.

## Local verification — 2026-09-12

- HIDMaestro's signed UMDF driver is installed.
- A Precision Touchpad collection was detected: VID `0488`, PID `104B`.
- 3DxWare recognized the virtual SpaceMouse Pro.
- Rotation was confirmed in the 3Dconnexion Viewer.
