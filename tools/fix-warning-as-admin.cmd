@echo off
rem Double-click this file. Click "Yes" on the UAC prompt.
rem It writes the Attachment Manager policy with admin rights, then restarts Explorer.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','%~dp0fix-warning.ps1'"
timeout /t 3 >nul
taskkill /f /im explorer.exe >nul 2>&1
start explorer.exe
