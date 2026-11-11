@echo off
setlocal EnableDelayedExpansion
title Windows Hello Refined - Complete Uninstaller

:: Ensure working directory is the script directory
cd /d "%~dp0"

:: 1. Check for Administrator privileges; auto-elevate if not admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ===================================================================
    echo [!] Administrator privileges required.
    echo     Requesting elevation via User Account Control [UAC]...
    echo ===================================================================
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cls
echo ===================================================================
echo            WINDOWS HELLO REFINED - ALL-IN-ONE UNINSTALLER
echo                        Made by Aryan Das
echo          GitHub:   https://github.com/aryandas079 (@aryandas079)
echo          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)
echo ===================================================================
echo.

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"
set "DLL=%PROJ_DIR%\credential_provider\FaceCredentialProvider.dll"
set "GUID={C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}"

:: ---------------------------------------------------------------------
:: Step 1/4: Stop Background Service
:: ---------------------------------------------------------------------
echo [*] [1/4] Stopping background face service...
taskkill /F /IM pythonw.exe >nul 2>&1
echo       Background service stopped.
echo.

:: ---------------------------------------------------------------------
:: Step 2/4: Remove Boot and Logon Startup Tasks
:: ---------------------------------------------------------------------
echo [*] [2/4] Removing startup tasks and auto-start keys...
schtasks /delete /tn "WindowsHelloRefinedDaemon" /f >nul 2>&1
schtasks /delete /tn "WindowsHelloRefinedService" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WindowsHelloRefinedService" /f >nul 2>&1
echo       Scheduled boot tasks and registry run keys removed.
echo.

:: ---------------------------------------------------------------------
:: Step 3/4: Unregister Credential Provider from Windows Logon
:: ---------------------------------------------------------------------
echo [*] [3/4] Unregistering Credential Provider from Windows Logon...
if exist "%DLL%" (
    regsvr32 /u /s "%DLL%" >nul 2>&1
)
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\%GUID%" /f >nul 2>&1
echo       Credential provider unregistered from Windows LogonUI.
echo.

:: ---------------------------------------------------------------------
:: Step 4/4: Optional Biometric and PIN Data Cleanup
:: ---------------------------------------------------------------------
echo [*] [4/4] Biometric and security data cleanup:
set /p "DEL_DATA=Do you want to permanently delete saved face biometrics and PIN? [y/N]: "
if /i "!DEL_DATA!"=="Y" (
    if exist "%PROJ_DIR%\data\face_profile.enc" del /f /q "%PROJ_DIR%\data\face_profile.enc" >nul 2>&1
    if exist "%PROJ_DIR%\data\credentials.enc" del /f /q "%PROJ_DIR%\data\credentials.enc" >nul 2>&1
    if exist "%PROJ_DIR%\data\config.json" del /f /q "%PROJ_DIR%\data\config.json" >nul 2>&1
    if exist "%PROJ_DIR%\data\*.log" del /f /q "%PROJ_DIR%\data\*.log" >nul 2>&1
    echo       Saved biometric data and PIN have been deleted.
) else (
    echo       Saved data retained in data\ folder.
)
echo.

:: ---------------------------------------------------------------------
:: Uninstallation Summary
:: ---------------------------------------------------------------------
echo ===================================================================
echo                   UNINSTALLATION COMPLETE
echo ===================================================================
echo.
echo Windows Hello Refined has been completely removed from your device:
echo   - Background face service stopped and scheduled tasks deleted.
echo   - Native Credential Provider unregistered from Windows Logon.
echo   - Default Windows Hello PIN and Password login are 100%% untouched.
echo.
echo Thank you for using Windows Hello Refined!
echo   Author:   Aryan Das
echo   GitHub:   https://github.com/aryandas079 (@aryandas079)
echo   LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)
echo ===================================================================
echo.
pause
