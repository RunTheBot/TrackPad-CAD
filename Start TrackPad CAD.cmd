@echo off
cd /d "%~dp0"
"C:\Program Files\gsudo\Current\gsudo.exe" pwsh -NoProfile -File "%~dp0run-trackpad.ps1" -Target chrome.exe -Sensitivity 20
if errorlevel 1 pause
