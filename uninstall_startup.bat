@echo off
title Windows Hello Refined - Remove Boot Startup Service
echo =======================================================
echo   WINDOWS HELLO REFINED - REMOVE BOOT AUTO-START
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

echo [*] Removing Windows Scheduled Task...
schtasks /delete /tn "WindowsHelloRefinedDaemon" /f

if %errorLevel% equ 0 (
    echo [SUCCESS] Boot startup task removed.
) else (
    echo [!] Task not found or already removed.
)

echo.
pause
