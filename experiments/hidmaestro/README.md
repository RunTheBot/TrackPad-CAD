# HIDMaestro output bridge

The local SpaceMouse Pro recognition and motion test succeeded in 3Dconnexion Viewer. Raw Input recorded all 33 submitted nonzero rotation reports, and the user confirmed visible motion. This validates that output experiment, not universal CAD support.

From the workspace in PowerShell:

```powershell
gsudo pwsh -NoProfile -File run-trackpad.ps1
```

The app starts disabled. Press F8 once to enable it and again to disable it. The tray icon is green while enabled and gray while disabled; double-clicking it also toggles. Right-click it for Enable/Disable and Exit. Output and native wheel suppression only apply while the configured target process is foreground. Ctrl+C also exits.

Sensitivity defaults to 5× the original prototype. Use `-Sensitivity 10` for stronger motion or `-Sensitivity 1` for the original gain. This scales all six motion axes; reports remain bounded to ±350. Restart the bridge after changing the setting.

```powershell
gsudo pwsh -NoProfile -File run-trackpad.ps1 -Target chrome.exe
```

The target is a process basename, so browser targeting includes all its tabs. Two fingers orbit, Shift + two fingers pan, pinch zooms, twist rolls. Three-finger tap emits button 1 (bind in 3DxWare). Double-tap pivot is only logged and needs a CAD adapter. Capture currently accepts complete parallel Precision Touchpad reports.

While enabled in the target, the app blocks low-level vertical and horizontal wheel messages. That suppresses Windows two-finger scrolling and Ctrl+wheel pinch output, but it also blocks a physical mouse wheel in that target. Windows shell-level three/four-finger actions are configured before application input and cannot be universally intercepted by this app; set those gestures to `Nothing` in Windows Touchpad settings if they conflict.

The launcher uses the existing HIDMaestro installation; it does not reinstall the WDK or certificates. It needs PowerShell 7 on .NET 10, elevation, the compiled `build/trackpad-cad.exe`, and `build/hidmaestro/HIDMaestro.Core.dll`. The latter was extracted from the upstream v1.7.3 prebuilt release (119 MB archive). Release archive SHA256: `A337DDC70E90FF969DEAAAAD8C3F3F8B7A0EE5B61A6A9FF6183CA35950BD8503`.

The C++ capture process streams frames into a C# HIDMaestro bridge. Translation, rotation and button report IDs are spaced apart to avoid immediate shared-memory overwrites. Motion accumulation is rescaled to each report group's elapsed interval. A capture timeout, focus loss or F8 release clears pending input. Normal exit submits zero reports and disposes the virtual controller. Abrupt process termination is not a verified failsafe.

Validation: user-mode CMake build and gesture tests passed; C# encoding check passed; bounded startup/shutdown smoke test uses a nonexistent process target so it cannot emit motion. No claim of live trackpad-to-CAD validation yet.

The profile reproduces a SpaceMouse Pro identity for a local compatibility experiment. Its 312-byte descriptor comes from [jfedor2/magellan-spacemouse](https://github.com/jfedor2/magellan-spacemouse/blob/master/src/descriptors.c); the source and MIT notice are preserved in `reference-descriptors.c`. [HIDMaestro](https://github.com/hifihedgehog/HIDMaestro) is third-party software, with notices retained in `build/hidmaestro`.
