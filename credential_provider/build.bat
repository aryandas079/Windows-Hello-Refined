@echo off
setlocal

call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"

echo Compiling Modern V2 FaceCredentialProvider.dll (x64)...
cd /d "D:\Projects_New\Windows Hello Refined\credential_provider"

cl.exe /O2 /Oi /EHsc /std:c++17 /W3 /D_USRDLL /D_WINDLL /DUNICODE /D_UNICODE ^
    FaceCredentialProvider.cpp FaceCredential.cpp FaceOverlay.cpp dllmain.cpp ^
    /link /DLL /DEF:FaceCredentialProvider.def ^
    /OUT:FaceCredentialProvider.dll ^
    user32.lib advapi32.lib gdi32.lib shlwapi.lib ole32.lib secur32.lib gdiplus.lib wtsapi32.lib credui.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo =======================================================
    echo [SUCCESS] FaceCredentialProvider.dll built successfully!
    echo =======================================================
) else (
    echo.
    echo [ERROR] Build failed with code %ERRORLEVEL%
)

endlocal
