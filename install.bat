@echo off
setlocal EnableDelayedExpansion
title Windows Hello Refined - Complete Installer

:: Ensure working directory is the script directory
cd /d "%~dp0"

:: Disable Console QuickEdit Mode so accidental mouse clicks do not pause execution
powershell -NoProfile -Command "Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public class CQ { [DllImport(\"kernel32.dll\")] public static extern IntPtr GetStdHandle(int n); [DllImport(\"kernel32.dll\")] public static extern bool GetConsoleMode(IntPtr h, out uint m); [DllImport(\"kernel32.dll\")] public static extern bool SetConsoleMode(IntPtr h, uint m); public static void Fix() { IntPtr h = GetStdHandle(-10); uint m; if (GetConsoleMode(h, out m)) { m &= ~0x0040u; m |= 0x0080u; SetConsoleMode(h, m); } } }'; [CQ]::Fix();" >nul 2>&1

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
echo             WINDOWS HELLO REFINED - ALL-IN-ONE INSTALLER
echo                        Made by Aryan Das
echo          GitHub:   https://github.com/aryandas079 (@aryandas079)
echo          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)
echo ===================================================================
echo.

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"

set "PYTHON=%PROJ_DIR%\.venv\Scripts\python.exe"
set "PYTHONW=%PROJ_DIR%\.venv\Scripts\pythonw.exe"
set "SERVICE_PY=%PROJ_DIR%\service\face_service.py"
set "DLL=%PROJ_DIR%\credential_provider\FaceCredentialProvider.dll"
set "GUID={C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}"

:: ---------------------------------------------------------------------
:: Step 1/5: Check / Prepare Python Environment
:: ---------------------------------------------------------------------
echo [*] [1/5] Verifying Python and dependencies...
if not exist "%PYTHON%" (
    echo       Virtual environment [.venv] not found. Checking system Python...
    where python >nul 2>&1
    if !errorLevel! neq 0 (
        echo [ERROR] Python 3.10+ is required but not installed or not in PATH.
        echo Please install Python from https://www.python.org/ or Windows Store and run install.bat again.
        echo.
        pause
        exit /b 1
    )
    echo       Creating virtual environment [.venv]...
    python -m venv "%PROJ_DIR%\.venv"
    if !errorLevel! neq 0 (
        echo [ERROR] Failed to create virtual environment [.venv].
        echo.
        pause
        exit /b 1
    )
    echo       Installing required dependencies from requirements.txt...
    "%PROJ_DIR%\.venv\Scripts\pip.exe" install -r "%PROJ_DIR%\requirements.txt"
    if !errorLevel! neq 0 (
        echo [ERROR] Failed to install dependencies.
        echo.
        pause
        exit /b 1
    )
)
echo       Python environment ready.
echo.

:: ---------------------------------------------------------------------
:: Step 2/5: Face Biometric Enrollment, Windows Hello PIN & Lock Screen
:: ---------------------------------------------------------------------
echo [*] [2/5] Setting up Face Biometrics, Windows Hello PIN, and Lock Screen...
echo       A camera window will open. Follow the on-screen head-turn prompts.
echo.
"%PYTHON%" "%PROJ_DIR%\setup_face.py"
if %errorLevel% neq 0 (
    echo.
    echo [ERROR] Setup was cancelled or failed. Installation aborted.
    echo.
    pause
    exit /b %errorLevel%
)
echo.

:: ---------------------------------------------------------------------
:: Step 3/5: Register Native Credential Provider DLL
:: ---------------------------------------------------------------------
echo [*] [3/5] Registering Credential Provider into Windows Logon...
if not exist "%DLL%" (
    echo       Building Credential Provider DLL...
    call "%PROJ_DIR%\credential_provider\build.bat"
    if not exist "%DLL%" (
        echo [ERROR] Build failed. Could not find %DLL%.
        echo.
        pause
        exit /b 1
    )
)

echo       Configuring Windows Registry entries...
reg add "HKCR\CLSID\%GUID%\InprocServer32" /ve /d "%DLL%" /f >nul 2>&1
reg add "HKCR\CLSID\%GUID%\InprocServer32" /v "ThreadingModel" /d "Apartment" /f >nul 2>&1
reg add "HKLM\SOFTWARE\Classes\CLSID\%GUID%\InprocServer32" /ve /d "%DLL%" /f >nul 2>&1
reg add "HKLM\SOFTWARE\Classes\CLSID\%GUID%\InprocServer32" /v "ThreadingModel" /d "Apartment" /f >nul 2>&1

reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul 2>&1
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /v "Disabled" /t REG_DWORD /d 0 /f >nul 2>&1
reg add "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul 2>&1

echo       Registering COM module with regsvr32...
regsvr32 /s "%DLL%"
echo       [SUCCESS] Credential provider registered in Windows LogonUI.
echo.

:: ---------------------------------------------------------------------
:: Step 4/5: Configure Automatic Startup (Boot, Restart, and User Logon)
:: ---------------------------------------------------------------------
echo [*] [4/5] Configuring automatic background startup...
taskkill /F /IM pythonw.exe >nul 2>&1

:: Register Windows Scheduled Task to run at system boot
schtasks /create /tn "WindowsHelloRefinedDaemon" /tr "\"%PYTHONW%\" \"%SERVICE_PY%\"" /sc onstart /ru "SYSTEM" /rl highest /f >nul 2>&1

:: Register HKCU Run key for user logon redundancy
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WindowsHelloRefinedService" /t REG_SZ /d "\"%PYTHONW%\" \"%SERVICE_PY%\"" /f >nul
echo       Boot startup task and user logon startup configured.
echo.

:: ---------------------------------------------------------------------
:: Step 5/5: Start Face Service in Background
:: ---------------------------------------------------------------------
echo [*] [5/5] Starting face service daemon in background...
start "" "%PYTHONW%" "%SERVICE_PY%"
echo       Background service is now active and listening.
echo.

:: ---------------------------------------------------------------------
:: Installation Summary
:: ---------------------------------------------------------------------
echo ===================================================================
echo                     INSTALLATION SUCCESSFUL!
echo ===================================================================
echo.
echo What was configured:
echo   1. Face biometric profile enrolled with liveness verification.
echo   2. Windows Hello PIN encrypted locally via Windows DPAPI.
echo   3. Ambient Windows Hello face overlay integrated with lock screen.
echo   4. Credential Provider registered without cluttering sign-in tiles.
echo   5. Automatic boot-time service scheduled for instant startup.
echo   6. Camera turns ON only to verify face and turns OFF immediately.
echo.
echo Test it right now:
echo   Press [Win + L] to lock your screen!
echo.
echo For support or updates:
echo   Author:   Aryan Das
echo   GitHub:   https://github.com/aryandas079 (@aryandas079)
echo   LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)
echo ===================================================================
echo.
pause
