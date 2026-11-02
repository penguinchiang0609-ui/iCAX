@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Inspect-TpmEnrollment.ps1"
if errorlevel 1 echo Diagnostic failed. Please send the error text.
pause
