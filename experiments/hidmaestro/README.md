# HIDMaestro output bridge

The local SpaceMouse Pro recognition and motion test succeeded in 3Dconnexion Viewer. Raw Input recorded all 33 submitted nonzero rotation reports, and the user confirmed visible motion. This validates that output experiment, not universal CAD support.

From the workspace in PowerShell:

```powershell
gsudo pwsh -NoProfile -File run-trackpad.ps1
```

The app starts disabled unless `StartEnabled` is set in `TrackPad CAD.json`. Press the configured `ToggleKey` (F8 by default) once to enable it and again to disable it. The tray icon is green while enabled and gray while disabled; double-clicking it also toggles. Right-click it for Enable/Disable and Exit. Native programs use the configured executable allowlist. Browser pages use the companion extension's focused-page URL and 3DconnexionJS WebSocket heartbeat. Ctrl+C also exits when run from a console.

The packaged configuration uses sensitivity 30. Change `Sensitivity` in `TrackPad CAD.json`; reports remain bounded to ±350. Restart the bridge after changing the setting.

```powershell
gsudo pwsh -NoProfile -File run-trackpad.ps1 -Config '.\TrackPad CAD.json'
```

The target is a process basename, so browser targeting includes all its tabs. Two fingers orbit, Shift + two fingers pan, pinch zooms, twist rolls. Three-finger tap emits button 1 (bind in 3DxWare). Double-tap pivot is only logged and needs a CAD adapter. Capture currently accepts complete parallel Precision Touchpad reports.

While enabled and the target is foreground, the app reads the current `TOUCHPAD_PARAMETERS`, temporarily clears `panEnabled` and `zoomEnabled` with `SPI_SETTOUCHPADPARAMETERS`, and restores the saved parameters on focus loss, disable, or exit. It omits `SPIF_UPDATEINIFILE`, so the override is not persisted to the user's profile. Physical mouse-wheel input is unaffected. This requires Windows 11 version 24H2 or newer.

The launcher uses the existing HIDMaestro installation; it does not reinstall the WDK or certificates. It needs PowerShell 7 on .NET 10, elevation, the compiled `build/trackpad-cad.exe`, and `build/hidmaestro/HIDMaestro.Core.dll`. The latter was extracted from the upstream v1.7.3 prebuilt release (119 MB archive). Release archive SHA256: `A337DDC70E90FF969DEAAAAD8C3F3F8B7A0EE5B61A6A9FF6183CA35950BD8503`.

The C++ capture process streams frames into a C# HIDMaestro bridge. Translation, rotation and button report IDs are spaced apart to avoid immediate shared-memory overwrites. Motion accumulation is rescaled to each report group's elapsed interval. A capture timeout, focus loss or F8 release clears pending input. Normal exit submits zero reports and disposes the virtual controller. Abrupt process termination is not a verified failsafe.

Validation: user-mode CMake build and gesture tests passed; C# encoding check passed; bounded startup/shutdown smoke test uses a nonexistent process target so it cannot emit motion. No claim of live trackpad-to-CAD validation yet.

The profile reproduces a SpaceMouse Pro identity for a local compatibility experiment. Its 312-byte descriptor comes from [jfedor2/magellan-spacemouse](https://github.com/jfedor2/magellan-spacemouse/blob/master/src/descriptors.c); the source and MIT notice are preserved in `reference-descriptors.c`. [HIDMaestro](https://github.com/hifihedgehog/HIDMaestro) is third-party software, with notices retained in `build/hidmaestro`.
