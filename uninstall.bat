@echo off
:: Windows Hello Refined — Uninstaller
setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "DLL=%ROOT%\credential_provider\FaceCredentialProvider.dll"
set "GUID={C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}"

echo ================================================================
echo  Windows Hello Refined — Uninstaller
echo ================================================================
echo.

echo [1/3] Stopping background service...
taskkill /F /IM pythonw.exe >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WindowsHelloRefinedService" /f >nul 2>&1
schtasks /Delete /TN "WindowsHelloRefinedService" /F >nul 2>&1
echo       OK.

echo [2/3] Unregistering credential provider DLL...
regsvr32 /s /u "%DLL%" >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\%GUID%" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\%GUID%" /f >nul 2>&1
echo       OK.

echo [3/3] Done.
echo.
echo  Windows Hello Refined removed. Windows PIN / Password unaffected.
echo.
pause
