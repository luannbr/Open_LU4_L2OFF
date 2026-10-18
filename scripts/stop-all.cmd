@echo off
rem Double-click to stop every L2OFF server in reverse order (see stop-all.ps1).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-all.ps1"
pause
