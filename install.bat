@echo off
:: Windows Hello Refined — Full Installer
:: Run this as Administrator ONCE after building the DLL.
setlocal EnableDelayedExpansion

echo ================================================================
echo  Windows Hello Refined — Lock Screen Installer
echo ================================================================
echo.

:: ── Paths ────────────────────────────────────────────────────────
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "DLL=%ROOT%\credential_provider\FaceCredentialProvider.dll"
set "PYTHON=%ROOT%\.venv\Scripts\pythonw.exe"
set "SERVICE_SCRIPT=%ROOT%\service\face_service.py"
set "GUID={C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}"

:: ── 1. Build the DLL ─────────────────────────────────────────────
echo [1/5] Building credential provider DLL...
call "%ROOT%\credential_provider\build.bat"
if not exist "%DLL%" (
    echo [ERROR] DLL build failed. Aborting.
    pause
    exit /b 1
)
echo       OK: %DLL%
echo.

:: ── 2. Register the DLL ──────────────────────────────────────────
echo [2/5] Registering credential provider DLL...
regsvr32 /s "%DLL%"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] regsvr32 failed. Make sure you're running as Administrator.
    pause
    exit /b 1
)
echo       OK: DLL registered.
echo.

:: ── 3. Add registry entry for the credential provider ────────────
echo [3/5] Writing credential provider registry entries...

:: Register under LogonUI's list of credential providers
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /v "Disabled" /t REG_DWORD /d 0 /f >nul

:: Also register on WOW6432 node for 32-bit LogonUI on 64-bit systems
reg add "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul

echo       OK: Registry entries written.
echo.

:: ── 4. Create Windows Task for auto-starting face service ─────────
echo [4/5] Creating auto-start task for face recognition service...

:: Delete old task if it exists
schtasks /Delete /TN "WindowsHelloRefinedService" /F >nul 2>&1

:: Create task: starts at logon (session 1), runs always, highest privileges
schtasks /Create /TN "WindowsHelloRefinedService" ^
  /TR "\"%PYTHON%\" \"%SERVICE_SCRIPT%\"" ^
  /SC ONLOGON ^
  /RU SYSTEM ^
  /RL HIGHEST ^
  /F >nul

if %ERRORLEVEL% neq 0 (
    echo [WARN] Could not create scheduled task. Service won't auto-start.
    echo        Start it manually: "%PYTHON%" "%SERVICE_SCRIPT%"
) else (
    echo       OK: Task 'WindowsHelloRefinedService' created.
)
echo.

:: ── 5. Start the face service NOW ────────────────────────────────
echo [5/5] Starting face recognition service...
start "" /B "%PYTHON%" "%SERVICE_SCRIPT%"
echo       OK: Service started in background.
echo.

echo ================================================================
echo  Installation complete!
echo ================================================================
echo.
echo  What happens now:
echo   - Lock your screen (Win+L)
echo   - The Windows Hello Refined tile appears alongside PIN
echo   - Face scanning starts automatically
echo   - The animation plays while your face is being verified
echo   - On match: screen unlocks instantly
echo   - If face fails: click "Sign-in options" for PIN/Password
echo.
echo  To uninstall: run uninstall.bat as Administrator
echo.
pause
