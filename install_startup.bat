@echo off
title Windows Hello Refined - Install Boot Startup Service
echo =======================================================
echo   WINDOWS HELLO REFINED - BOOT & RESTART AUTO-START
echo =======================================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [!] Administrator privileges required.
    echo Right-click this file and select 'Run as administrator'.
    echo.
    pause
    exit /b 1
)

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"

set "PYTHONW=%PROJ_DIR%\.venv\Scripts\pythonw.exe"
set "SERVICE_PY=%PROJ_DIR%\service\face_service.py"

if not exist "%PYTHONW%" (
    echo [ERROR] Could not find pythonw.exe at: %PYTHONW%
    echo Please make sure the virtual environment (.venv) is set up.
    pause
    exit /b 1
)

echo [*] Registering Windows Scheduled Task for boot startup...
schtasks /create /tn "WindowsHelloRefinedDaemon" /tr "\"%PYTHONW%\" \"%SERVICE_PY%\"" /sc onstart /ru "SYSTEM" /rl highest /f

if %errorLevel% equ 0 (
    echo.
    echo =======================================================
    echo [SUCCESS] Boot-time startup service installed!
    echo.
    echo Windows Hello Refined will now automatically start:
    echo   1. When turning on the computer from a shutdown state.
    echo   2. Immediately after restarting Windows.
    echo   3. On-demand if triggered by the lock screen.
    echo =======================================================
) else (
    echo.
    echo [ERROR] Failed to register task with schtasks. Error code: %errorLevel%
)

echo.
pause
