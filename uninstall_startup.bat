@echo off
title Windows Hello Refined - Remove Boot Startup Service
echo ===================================================================
echo             WINDOWS HELLO REFINED - REMOVE BOOT SERVICE
echo                        Made by Aryan Das
echo          GitHub:   https://github.com/aryandas079 (@aryandas079)
echo          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)
echo ===================================================================
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
