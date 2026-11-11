@echo off
setlocal

echo ================================================================
echo  Windows Hello Refined - Lock Screen Installer
echo ================================================================
echo.

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "DLL=%ROOT%\credential_provider\FaceCredentialProvider.dll"
set "PYTHON=%ROOT%\.venv\Scripts\pythonw.exe"
set "SERVICE_SCRIPT=%ROOT%\service\face_service.py"
set "GUID={C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}"

echo [1/5] Stopping previous instances...
taskkill /F /IM pythonw.exe >nul 2>&1
echo       OK.
echo.

echo [2/5] Building native Credential Provider DLL...
call "%ROOT%\credential_provider\build.bat"
if not exist "%DLL%" (
    echo [ERROR] DLL build failed.
    pause
    exit /b 1
)
echo       OK: %DLL%
echo.

echo [3/5] Registering credential provider...
regsvr32 /s "%DLL%"

reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /v "Disabled" /t REG_DWORD /d 0 /f >nul
reg add "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /ve /d "WindowsHelloRefined" /f >nul
echo       OK: Credential provider registered in Windows LogonUI.
echo.

echo [4/5] Configuring background service auto-start...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WindowsHelloRefinedService" /t REG_SZ /d "\"%PYTHON%\" \"%SERVICE_SCRIPT%\"" /f >nul
echo       OK: Startup key registered in HKCU.
echo.

echo [5/5] Starting face service in background...
start "" "%PYTHON%" "%SERVICE_SCRIPT%"
echo       OK: Face service is active.
echo.

echo ================================================================
echo  Installation Successful!
echo ================================================================
echo.
echo  - Camera is IDLE (LED is completely OFF when not verifying)
echo  - When screen locks (Win+L), camera turns on ONLY to scan your face
echo  - Smooth native Windows Hello animation appears at top-center
echo  - PIN and Password remain available under Sign-in options
echo  - Camera turns OFF immediately after verification finishes
echo.
echo  Press Win+L to test!
echo.
pause
