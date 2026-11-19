@echo off
setlocal DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect.ps1" "%~1"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
