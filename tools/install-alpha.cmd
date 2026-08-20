@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-alpha.ps1" -PayloadPath "%~dp0payload.zip"
if errorlevel 1 pause
