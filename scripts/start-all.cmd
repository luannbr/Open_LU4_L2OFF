@echo off
rem Double-click to start every L2OFF server in order (see start-all.ps1).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-all.ps1"
pause
