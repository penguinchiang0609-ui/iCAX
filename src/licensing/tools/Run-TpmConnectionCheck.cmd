@echo off
setlocal
if not exist "%~dp0td-license-tpm-connection-probe.exe" (
  echo Missing diagnostic executable. Extract all files from the ZIP first.
  pause
  exit /b 1
)
"%~dp0td-license-tpm-connection-probe.exe" > "%~dp0TPM-Connection-Report.txt" 2>&1
if not exist "%~dp0TPM-Connection-Report.txt" (
  echo Cannot write report. Extract the ZIP to a writable folder and try again.
  pause
  exit /b 1
)
echo Report saved: "%~dp0TPM-Connection-Report.txt"
start "" notepad.exe "%~dp0TPM-Connection-Report.txt"
