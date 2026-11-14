#include "FaceOverlay.h"
#include "common.h"
#include <strsafe.h>
#include <cmath>
#include <mmsystem.h>
#include <shlwapi.h>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")

extern HINSTANCE g_hInst;

static void LogOverlay(const WCHAR* fmt, ...)
{
    WCHAR buf[1024];
    va_list va;
    va_start(va, fmt);
    StringCchVPrintfW(buf, ARRAYSIZE(buf), fmt, va);
    va_end(va);

    FILE* f = _wfopen(L"C:\\Users\\Public\\WindowsHelloRefined.log", L"a, ccs=UTF-8");
    if (f)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%02d:%02d:%02d.%03d] [OVERLAY] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

static void PlayFeedbackSound()
{
    WCHAR assetPath[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_hInst, assetPath, ARRAYSIZE(assetPath)))
    {
        PathRemoveFileSpecW(assetPath);
        PathRemoveFileSpecW(assetPath);
        PathAppendW(assetPath, L"assets\\hello_verified.wav");
    }

    LPCWSTR targetSound = NULL;
    if (PathFileExistsW(assetPath))
    {
        targetSound = assetPath;
    }
    else if (PathFileExistsW(L"C:\\Windows\\Media\\Windows Notify.wav"))
    {
        targetSound = L"C:\\Windows\\Media\\Windows Notify.wav";
    }
    else if (PathFileExistsW(L"C:\\Windows\\Media\\Windows Unlock.wav"))
    {
        targetSound = L"C:\\Windows\\Media\\Windows Unlock.wav";
    }

    if (targetSound)
    {
        LogOverlay(L"PlayFeedbackSound: playing %s asynchronously", targetSound);
        BOOL bPlayed = PlaySoundW(targetSound, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        if (!bPlayed)
        {
            LogOverlay(L"PlayFeedbackSound: PlaySoundW returned FALSE (err=%lu), trying MessageBeep", GetLastError());
            MessageBeep(MB_OK);
        }
    }
    else
    {
        MessageBeep(MB_OK);
    }
}

static inline void PremultiplyAlpha(void* pBits, int totalPixels)
{
    if (!pBits) return;
    RGBQUAD* pPixel = reinterpret_cast<RGBQUAD*>(pBits);
    for (int i = 0; i < totalPixels; i++)
    {
        BYTE a = pPixel[i].rgbReserved;
        if (a == 0)
        {
            pPixel[i].rgbRed = 0;
            pPixel[i].rgbGreen = 0;
            pPixel[i].rgbBlue = 0;
        }
        else if (a < 255)
        {
            pPixel[i].rgbRed   = (BYTE)(((DWORD)pPixel[i].rgbRed * a + 127) / 255);
            pPixel[i].rgbGreen = (BYTE)(((DWORD)pPixel[i].rgbGreen * a + 127) / 255);
            pPixel[i].rgbBlue  = (BYTE)(((DWORD)pPixel[i].rgbBlue * a + 127) / 255);
        }
    }
}

static bool IsLockScreenDesktopOverlay()
{
    HDESK hInput = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!hInput) return false;
    WCHAR deskName[64] = {0};
    DWORD needed = 0;
    BOOL ok = GetUserObjectInformationW(hInput, UOI_NAME, deskName, sizeof(deskName), &needed);
    CloseDesktop(hInput);
    return (ok && _wcsicmp(deskName, L"Winlogon") == 0);
}

FaceOverlay::FaceOverlay() :
    m_hwnd(NULL),
    m_hThread(NULL),
    m_bRunning(FALSE),
    m_state(OVERLAY_HIDDEN),
    m_startTime(0),
    m_verifiedTime(0),
    m_failedTime(0),
    m_bPlayedWinkSound(false),
    m_scenario(1),
    m_spinProgress(0.0f),
    m_winkProgress(0.0f),
    m_fadeAlpha(1.0f)
{
    EnsureGdiplus();
    InitializeCriticalSection(&m_cs);
    StringCchCopyW(m_username, ARRAYSIZE(m_username), L"Aryan");
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Looking for you...");
}

FaceOverlay::~FaceOverlay()
{
    Hide(true);
    DeleteCriticalSection(&m_cs);
    ReleaseGdiplus();
}

LRESULT CALLBACK FaceOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void FaceOverlay::Show(const WCHAR* username, int scenario)
{
    EnterCriticalSection(&m_cs);
    m_scenario = scenario;
    if (username && wcslen(username) > 0)
    {
        StringCchCopyW(m_username, ARRAYSIZE(m_username), username);
    }
    m_state = OVERLAY_SCANNING;
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Looking for you...");
    m_startTime = GetTickCount();
    m_verifiedTime = 0;
    m_failedTime = 0;
    m_bPlayedWinkSound = false;
    m_spinProgress = 0.0f;
    m_winkProgress = 0.0f;
    m_fadeAlpha = 1.0f;

    if (!m_bRunning)
    {
        m_bRunning = TRUE;
        LogOverlay(L"Spawning AnimationThread (scenario=%d)...", m_scenario);
        m_hThread = CreateThread(NULL, 0, AnimationThread, this, 0, NULL);
    }
    LeaveCriticalSection(&m_cs);
}

void FaceOverlay::SetState(OVERLAY_STATE state, const WCHAR* customText)
{
    EnterCriticalSection(&m_cs);
    m_state = state;
    if (state == OVERLAY_VERIFIED)
    {
        m_verifiedTime = GetTickCount();
        m_bPlayedWinkSound = false;
        WCHAR formattedUser[128] = {0};
        StringCchCopyW(formattedUser, ARRAYSIZE(formattedUser), m_username);
        if (formattedUser[0] >= L'a' && formattedUser[0] <= L'z')
        {
            formattedUser[0] = formattedUser[0] - L'a' + L'A';
        }
        if (customText)
        {
            StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), customText);
        }
        else
        {
            StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Welcome, %s", formattedUser);
        }
        LogOverlay(L"State changed to OVERLAY_VERIFIED (%s)", m_statusText);
    }
    else if (state == OVERLAY_FAILED)
    {
        m_failedTime = GetTickCount();
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), customText ? customText : L"Couldn't recognize face. Use PIN.");
        LogOverlay(L"State changed to OVERLAY_FAILED (%s)", m_statusText);
    }
    LeaveCriticalSection(&m_cs);
}

void FaceOverlay::Hide(bool bForce)
{
    EnterCriticalSection(&m_cs);
    bool bLockScenario = (m_scenario == CPUS_LOGON || m_scenario == CPUS_UNLOCK_WORKSTATION || (int)m_scenario <= 1);
    if (!bForce && (bLockScenario || IsLockScreenDesktopOverlay()) && m_state != OVERLAY_VERIFIED)
    {
        LogOverlay(L"Hide requested but lock screen is still active; keeping overlay alive and scanning!");
        m_state = OVERLAY_SCANNING;
        m_fadeAlpha = 1.0f;
        LeaveCriticalSection(&m_cs);
        return;
    }
    m_bRunning = FALSE;
    LeaveCriticalSection(&m_cs);

    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_HIDE);
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }

    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, 500);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

DWORD WINAPI FaceOverlay::AnimationThread(LPVOID lpParam)
{
    FaceOverlay* pThis = reinterpret_cast<FaceOverlay*>(lpParam);

    bool bLockScenario = (pThis->m_scenario == CPUS_LOGON || pThis->m_scenario == CPUS_UNLOCK_WORKSTATION || (int)pThis->m_scenario <= 1);

    HDESK hTargetDesk = NULL;
    if (bLockScenario)
    {
        // On lock screen, attach directly to Winlogon desktop where the lock screen and LogonUI live!
        hTargetDesk = OpenDesktopW(L"Winlogon", 0, FALSE, MAXIMUM_ALLOWED);
        if (!hTargetDesk)
        {
            hTargetDesk = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
        }
    }
    else
    {
        // On CredUI / in-session prompt, attach to active input desktop
        hTargetDesk = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
        if (!hTargetDesk)
        {
            hTargetDesk = OpenDesktopW(L"Winlogon", 0, FALSE, MAXIMUM_ALLOWED);
        }
    }

    if (hTargetDesk)
    {
        BOOL bSet = SetThreadDesktop(hTargetDesk);
        LogOverlay(L"AnimationThread: SetThreadDesktop result=%d, err=%lu, hDesk=%p (scenario=%d)",
                   bSet, GetLastError(), hTargetDesk, pThis->m_scenario);
    }
    else
    {
        LogOverlay(L"AnimationThread: OpenDesktop failed, err=%lu", GetLastError());
    }

    pThis->RunAnimation();

    if (hTargetDesk)
    {
        CloseDesktop(hTargetDesk);
    }
    return 0;
}

void FaceOverlay::RunAnimation()
{
    LogOverlay(L"RunAnimation started (scenario=%d)", m_scenario);

    HINSTANCE hInst = g_hInst ? g_hInst : GetModuleHandleW(NULL);
    const WCHAR* className = L"WindowsHelloNativeOverlay";

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    bool bLockScenario = (m_scenario == CPUS_LOGON || m_scenario == CPUS_UNLOCK_WORKSTATION || (int)m_scenario <= 1);
    HWND hwndSec = NULL;

    // Only search for CredUI window if NOT in lock screen scenario
    if (!bLockScenario || m_scenario == CPUS_CREDUI || (int)m_scenario >= 3)
    {
        for (int i = 0; i < 20; i++)
        {
            hwndSec = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
            if (!hwndSec) hwndSec = FindWindowW(NULL, L"Windows Security");
            if (!hwndSec) hwndSec = FindWindowW(L"Credential Dialog Xaml Host", NULL);
            if (hwndSec && IsWindowVisible(hwndSec))
            {
                RECT rc = {0};
                GetWindowRect(hwndSec, &rc);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                if (w < 1000 && h < 800 && w > 100 && h > 100)
                {
                    break;
                }
                hwndSec = NULL;
            }
            Sleep(25);
        }
    }

    bool bIsCredUI = (hwndSec != NULL && IsWindowVisible(hwndSec)) && !bLockScenario;
    bool bIsLockScreen = bLockScenario || !bIsCredUI;

    // On interactive desktop with NO CredUI prompt and NOT lock screen, skip creation
    if (!bLockScenario && !bIsCredUI)
    {
        LogOverlay(L"Interactive desktop without CredUI prompt: skipping overlay creation.");
        m_bRunning = FALSE;
        return;
    }

    HMONITOR hMon = MonitorFromWindow(hwndSec ? hwndSec : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    int winW = 440;
    int winH = 100;
    int posX = mi.rcMonitor.left + (monW - winW) / 2;
    // Exactly at the top, just below the webcam as requested
    int posY = mi.rcMonitor.top + 24;

    if (bIsCredUI)
    {
        RECT rc = {0};
        if (FAILED(DwmGetWindowAttribute(hwndSec, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
        {
            GetWindowRect(hwndSec, &rc);
        }
        int dlgW = rc.right - rc.left;
        int dlgH = rc.bottom - rc.top;

        // Position the overlay to completely cover the horizontal separating line
        // and the PIN logo / title band, while leaving the native app title and description visible.
        posX = rc.left;
        posY = rc.top + (int)(dlgH * 0.215);
        winW = dlgW;
        winH = (int)(dlgH * 0.320);
        LogOverlay(L"CredUI dialog scenario: attached replacement overlay (%d,%d %dx%d)", posX, posY, winW, winH);
    }
    else
    {
        LogOverlay(L"Lock screen scenario: top-of-screen webcam position (%d,%d %dx%d)", posX, posY, winW, winH);
    }

    typedef HWND (WINAPI *pfnCreateWindowInBand)(
        DWORD dwExStyle,
        LPCWSTR lpClassName,
        LPCWSTR lpWindowName,
        DWORD dwStyle,
        int X,
        int Y,
        int nWidth,
        int nHeight,
        HWND hWndParent,
        HMENU hMenu,
        HINSTANCE hInstance,
        LPVOID lpParam,
        DWORD dwBand
    );

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    pfnCreateWindowInBand pCreateWindowInBand = hUser32 ? (pfnCreateWindowInBand)GetProcAddress(hUser32, "CreateWindowInBand") : NULL;

    DWORD dwExStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    if (!bIsCredUI)
    {
        dwExStyle |= WS_EX_TOPMOST;
    }
    DWORD dwStyle = WS_POPUP;

    HWND hwndCreated = NULL;
    if (pCreateWindowInBand && bIsLockScreen)
    {
        DWORD bands[] = { 16, 15, 14, 7, 6, 0 };
        for (DWORD band : bands)
        {
            hwndCreated = pCreateWindowInBand(
                dwExStyle,
                className,
                L"Windows Hello Overlay",
                dwStyle,
                posX, posY, winW, winH,
                NULL, NULL, hInst, NULL,
                band
            );
            if (hwndCreated)
            {
                LogOverlay(L"CreateWindowInBand (band=%lu) succeeded! HWND=%p at (%d,%d %dx%d)", band, hwndCreated, posX, posY, winW, winH);
                break;
            }
        }
    }

    if (!hwndCreated)
    {
        hwndCreated = CreateWindowExW(
            dwExStyle,
            className,
            L"Windows Hello Overlay",
            dwStyle,
            posX, posY, winW, winH,
            bIsCredUI ? hwndSec : NULL, // ATTACH AS OWNED WINDOW OF CREDUI LOGIN PROMPT!
            NULL, hInst, NULL
        );
    }

    m_hwnd = hwndCreated;
    if (!m_hwnd)
    {
        LogOverlay(L"Overlay window creation failed: %lu", GetLastError());
        m_bRunning = FALSE;
        return;
    }
    LogOverlay(L"Overlay window created successfully! HWND=%p at (%d,%d %dx%d)", m_hwnd, posX, posY, winW, winH);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = winW;
    bmi.bmiHeader.biHeight = -winH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    POINT ptSrc = {0, 0};
    POINT ptDst = {posX, posY};
    SIZE sz = {winW, winH};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    // Initial render
    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        RenderFrame(g, winW, winH);
    }
    PremultiplyAlpha(pBits, winW * winH);
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    if (bIsCredUI)
    {
        SetWindowPos(m_hwnd, NULL, posX, posY, winW, winH, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
    }
    else
    {
        SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, winW, winH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    while (m_bRunning)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (bIsCredUI)
        {
            // For CredUI dialog, ensure overlay follows dialog position and exits IMMEDIATELY when dialog is hidden or closed
            if (!hwndSec || !IsWindow(hwndSec) || !IsWindowVisible(hwndSec))
            {
                LogOverlay(L"CredUI dialog closed or hidden. Exiting overlay immediately.");
                ShowWindow(m_hwnd, SW_HIDE);
                DestroyWindow(m_hwnd);
                m_hwnd = NULL;
                m_bRunning = FALSE;
                break;
            }

            RECT rcSec = {0};
            bool bGotRect = SUCCEEDED(DwmGetWindowAttribute(hwndSec, DWMWA_EXTENDED_FRAME_BOUNDS, &rcSec, sizeof(rcSec)));
            if (!bGotRect)
            {
                bGotRect = (GetWindowRect(hwndSec, &rcSec) != FALSE);
            }
            if (bGotRect)
            {
                int dlgW = rcSec.right - rcSec.left;
                int dlgH = rcSec.bottom - rcSec.top;
                int targetX = rcSec.left;
                int targetY = rcSec.top + (int)(dlgH * 0.215);
                int targetW = dlgW;
                int targetH = (int)(dlgH * 0.320);

                if (targetX != posX || targetY != posY || targetW != winW || targetH != winH)
                {
                    posX = targetX;
                    posY = targetY;
                    if (targetW != winW || targetH != winH)
                    {
                        winW = targetW;
                        winH = targetH;
                        sz.cx = winW;
                        sz.cy = winH;

                        // Recreate DIB section dynamically for resizing
                        SelectObject(hdcMem, hOldBitmap);
                        DeleteObject(hBitmap);

                        bmi.bmiHeader.biWidth = winW;
                        bmi.bmiHeader.biHeight = -winH;
                        hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                        hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
                    }
                    ptDst.x = posX;
                    ptDst.y = posY;
                    SetWindowPos(m_hwnd, NULL, posX, posY, winW, winH, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
                }
            }
        }
        else
        {
            SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, winW, winH, SWP_NOACTIVATE | SWP_NOREDRAW);
        }

        EnterCriticalSection(&m_cs);
        OVERLAY_STATE curState = m_state;
        DWORD now = GetTickCount();

        // Update animation parameters
        if (curState == OVERLAY_VERIFIED && m_verifiedTime > 0)
        {
            DWORD elapsed = now - m_verifiedTime;
            // 1. Spin into smiley face (0 to 500ms)
            if (elapsed < 500)
            {
                float t = (float)elapsed / 500.0f;
                m_spinProgress = (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f);
            }
            else
            {
                m_spinProgress = 1.0f;
            }

            // 2. Wink right eye from 1050ms to 1350ms
            if (elapsed >= 1050 && elapsed < 1350)
            {
                float wt = (float)(elapsed - 1050) / 300.0f;
                m_winkProgress = std::sin(wt * 3.14159265f);

                if (!m_bPlayedWinkSound)
                {
                    m_bPlayedWinkSound = true;
                    PlayFeedbackSound();
                }
            }
            else
            {
                m_winkProgress = 0.0f;
            }

            // 3. Fade out after 2.5s (only if desktop unlocked)
            if (elapsed > 2500)
            {
                float ft = (float)(elapsed - 2500) / 400.0f;
                if (ft >= 1.0f)
                {
                    m_fadeAlpha = 0.0f;
                    if (!bIsLockScreen)
                    {
                        m_bRunning = FALSE;
                    }
                }
                else
                {
                    m_fadeAlpha = 1.0f - ft;
                }
            }
        }
        else if (curState == OVERLAY_FAILED && m_failedTime > 0)
        {
            DWORD elapsed = now - m_failedTime;
            if (elapsed > 2000)
            {
                float ft = (float)(elapsed - 2000) / 400.0f;
                if (ft >= 1.0f)
                {
                    m_fadeAlpha = 0.0f;
                    if (!bIsLockScreen)
                    {
                        m_bRunning = FALSE;
                    }
                }
                else
                {
                    m_fadeAlpha = 1.0f - ft;
                }
            }
        }
        LeaveCriticalSection(&m_cs);

        // Render with GDI+
        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));

            RenderFrame(g, winW, winH);
        }
        PremultiplyAlpha(pBits, winW * winH);

        // Commit to screen with UpdateLayeredWindow
        blend.SourceConstantAlpha = (BYTE)(m_fadeAlpha * 255.0f);
        UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        Sleep(16); // ~60 FPS
    }

    LogOverlay(L"RunAnimation loop exited. Cleaning up.");
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

void FaceOverlay::RenderFrame(Gdiplus::Graphics& g, int width, int height)
{
    HWND hwndSecDlg = NULL;
    if (m_scenario == CPUS_CREDUI || (int)m_scenario >= 3)
    {
        hwndSecDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
        if (!hwndSecDlg) hwndSecDlg = FindWindowW(NULL, L"Windows Security");
        if (!hwndSecDlg) hwndSecDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
    }
    bool bLockScenario = (m_scenario == CPUS_LOGON || m_scenario == CPUS_UNLOCK_WORKSTATION || (int)m_scenario <= 1);
    bool bIsCredUI = (hwndSecDlg != NULL && IsWindowVisible(hwndSecDlg)) && !bLockScenario;
    bool bIsLockScreen = bLockScenario || !bIsCredUI;
    bool bLightTheme = false;

    float cx = width / 2.0f;
    float cy = bIsCredUI ? 38.0f : 34.0f;

    if (bIsCredUI)
    {
        DWORD val = 1;
        DWORD cb = sizeof(val);
        if (RegGetValueW(HKEY_CURRENT_USER,
                         L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme",
                         RRF_RT_REG_DWORD, NULL, &val, &cb) == ERROR_SUCCESS)
        {
            bLightTheme = (val != 0);
        }
        else
        {
            bLightTheme = true;
        }

        // Fill the entire overlay rectangle with solid background to hide ONLY
        // the 9-dot blue PIN logo and "Enter your PIN" text completely
        Gdiplus::SolidBrush bgBrush(bLightTheme ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 32, 32, 32));
        g.FillRectangle(&bgBrush, 0.0f, 0.0f, (float)width, (float)height);
    }

    Gdiplus::Color accentColor;
    Gdiplus::Color textColor;
    Gdiplus::Color shadowColor;

    if (bIsCredUI && bLightTheme)
    {
        accentColor = Gdiplus::Color(255, 0, 103, 192); // Windows 11 Fluent Blue #0067C0
        textColor   = Gdiplus::Color(255, 25, 25, 25);   // Dark Charcoal #191919
        shadowColor = Gdiplus::Color(25, 0, 0, 0);       // Soft subtle shadow
    }
    else
    {
        accentColor = Gdiplus::Color(255, 255, 255, 255); // Pure white
        textColor   = Gdiplus::Color(255, 255, 255, 255);
        shadowColor = Gdiplus::Color(110, 0, 0, 0);       // Drop shadow
    }

    Gdiplus::SolidBrush accentBrush(accentColor);
    Gdiplus::SolidBrush textBrush(textColor);
    Gdiplus::Pen accentPen(accentColor, bIsCredUI ? 4.5f : 4.5f);
    accentPen.SetStartCap(Gdiplus::LineCapRound);
    accentPen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::Pen shadowPen(shadowColor, bIsCredUI ? 4.5f : 4.5f);
    shadowPen.SetStartCap(Gdiplus::LineCapRound);
    shadowPen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush shadowBrush(shadowColor);

    float arcR = 22.0f;
    float eyeOffset = 14.0f;
    float eyeYOffset = 8.0f;

    if (m_state == OVERLAY_SCANNING)
    {
        // ── SCANNING PHASE (Dynamic gaze & natural eyelid blinking) ──
        DWORD now = GetTickCount();
        DWORD elapsed = now - m_startTime;
        float gazeX = std::sin(elapsed * 0.0035f) * (bIsCredUI ? 5.0f : 6.0f);

        // Periodic eyelid blink every 2200ms lasting 240ms
        DWORD blinkCycle = elapsed % 2200;
        float blinkScale = 1.0f;
        if (blinkCycle < 240)
        {
            float bt = (float)blinkCycle / 240.0f;
            blinkScale = 1.0f - std::sin(bt * 3.14159265f) * 0.88f;
        }

        // Top arc (shadow + accent)
        float scaledArcH = arcR * 2.0f * blinkScale;
        if (!bLightTheme)
        {
            Gdiplus::RectF arcShadow(cx - arcR, cy - (scaledArcH / 2.0f) + 1.5f, arcR * 2.0f, scaledArcH);
            g.DrawArc(&shadowPen, arcShadow, 200.0f, 140.0f);
        }

        Gdiplus::RectF arcRect(cx - arcR, cy - (scaledArcH / 2.0f), arcR * 2.0f, scaledArcH);
        g.DrawArc(&accentPen, arcRect, 200.0f, 140.0f);

        // Center animated scanning eye / pupil
        float rScan = bIsCredUI ? 11.0f : 12.0f;
        float pupilH = rScan * 2.0f * blinkScale;
        if (!bLightTheme)
        {
            g.FillEllipse(&shadowBrush, cx + gazeX - rScan, cy + 2.0f - (pupilH / 2.0f) + 1.5f, rScan * 2.0f, pupilH);
        }
        g.FillEllipse(&accentBrush, cx + gazeX - rScan, cy + 2.0f - (pupilH / 2.0f), rScan * 2.0f, pupilH);

        // On Lock Screen: Keep clean look (no status text while scanning, matching Image 1)
        // On CredUI: Display "Looking for you..."
        if (bIsCredUI)
        {
            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font font(&ff, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            float textY = bIsCredUI ? 78.0f : 82.0f;
            float textH = 30.0f;
            if (!bLightTheme)
            {
                Gdiplus::RectF ts(0.0f, textY + 1.5f, (float)width, textH);
                g.DrawString(m_statusText, -1, &font, ts, &sf, &shadowBrush);
            }
            Gdiplus::RectF tr(0.0f, textY, (float)width, textH);
            g.DrawString(m_statusText, -1, &font, tr, &sf, &textBrush);
        }
    }
    else if (m_state == OVERLAY_VERIFIED)
    {
        // ── VERIFIED PHASE (Smile spin + happy eyes + wink) ──
        float angle = 180.0f + m_spinProgress * 900.0f;

        Gdiplus::Matrix mat;
        mat.RotateAt(angle, Gdiplus::PointF(cx, cy));
        g.SetTransform(&mat);

        Gdiplus::RectF arcRect(cx - arcR, cy - arcR, arcR * 2.0f, arcR * 2.0f);
        if (!bLightTheme)
        {
            g.DrawArc(&shadowPen, cx - arcR, cy - arcR + 1.5f, arcR * 2.0f, arcR * 2.0f, 20.0f, 140.0f);
        }
        g.DrawArc(&accentPen, arcRect, 20.0f, 140.0f);

        g.ResetTransform();

        // Big scanning eye fades out as spin starts
        if (m_spinProgress < 0.4f)
        {
            float eyeAlpha = (1.0f - m_spinProgress / 0.4f) * (bLightTheme ? 200.0f : 255.0f);
            BYTE r = accentColor.GetR();
            BYTE gCol = accentColor.GetG();
            BYTE b = accentColor.GetB();
            Gdiplus::SolidBrush fadeEyeBrush(Gdiplus::Color((BYTE)eyeAlpha, r, gCol, b));
            float rScan = (bIsCredUI ? 11.0f : 12.0f) * (1.0f - m_spinProgress / 0.4f);
            g.FillEllipse(&fadeEyeBrush, cx - rScan, cy + 2.0f - rScan, rScan * 2.0f, rScan * 2.0f);
        }

        // Two happy eyes pop in as spin completes
        if (m_spinProgress > 0.6f)
        {
            float pop = (m_spinProgress - 0.6f) / 0.4f;
            float eyeR = 4.5f * pop;

            // Left eye
            if (!bLightTheme)
            {
                g.FillEllipse(&shadowBrush, cx - eyeOffset - eyeR, cy - eyeYOffset - eyeR + 1.5f, eyeR * 2.0f, eyeR * 2.0f);
            }
            g.FillEllipse(&accentBrush, cx - eyeOffset - eyeR, cy - eyeYOffset - eyeR, eyeR * 2.0f, eyeR * 2.0f);

            // Right eye (winks)
            if (m_winkProgress > 0.01f)
            {
                float winkHeight = eyeR * 2.0f * (1.0f - m_winkProgress * 0.90f);
                if (winkHeight < 2.0f) winkHeight = 2.0f;
                if (!bLightTheme)
                {
                    g.FillEllipse(&shadowBrush, cx + eyeOffset - eyeR, cy - eyeYOffset - winkHeight / 2.0f + 1.5f, eyeR * 2.0f, winkHeight);
                }
                g.FillEllipse(&accentBrush, cx + eyeOffset - eyeR, cy - eyeYOffset - winkHeight / 2.0f, eyeR * 2.0f, winkHeight);
            }
            else
            {
                if (!bLightTheme)
                {
                    g.FillEllipse(&shadowBrush, cx + eyeOffset - eyeR, cy - eyeYOffset - eyeR + 1.5f, eyeR * 2.0f, eyeR * 2.0f);
                }
                g.FillEllipse(&accentBrush, cx + eyeOffset - eyeR, cy - eyeYOffset - eyeR, eyeR * 2.0f, eyeR * 2.0f);
            }
        }

        // Status text below the face
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font font(&fontFamily, bIsCredUI ? 13.0f : 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        float textY = bIsCredUI ? 78.0f : 66.0f;
        float textH = 30.0f;

        if (!bLightTheme)
        {
            Gdiplus::RectF textShadowRect(0.0f, textY + 2.0f, (float)width, textH);
            g.DrawString(m_statusText, -1, &font, textShadowRect, &format, &shadowBrush);
        }

        Gdiplus::RectF textRect(0.0f, textY, (float)width, textH);
        g.DrawString(m_statusText, -1, &font, textRect, &format, &textBrush);
    }
    else if (m_state == OVERLAY_FAILED)
    {
        Gdiplus::Color dim = bLightTheme ? Gdiplus::Color(180, 100, 100, 100) : Gdiplus::Color(180, 210, 210, 210);
        Gdiplus::Pen dimPen(dim, bIsCredUI ? 3.5f : 4.0f);
        dimPen.SetStartCap(Gdiplus::LineCapRound);
        dimPen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush dimBrush(dim);

        // Neutral mouth line
        g.DrawLine(&dimPen, cx - 15.0f, cy + 14.0f, cx + 15.0f, cy + 14.0f);
        // Neutral eyes
        g.FillEllipse(&dimBrush, cx - 13.0f - 3.5f, cy - 7.0f - 3.5f, 7.0f, 7.0f);
        g.FillEllipse(&dimBrush, cx + 13.0f - 3.5f, cy - 7.0f - 3.5f, 7.0f, 7.0f);

        // Status text
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font font(&fontFamily, bIsCredUI ? 11.5f : 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        float textY = bIsCredUI ? 78.0f : 78.0f;
        float textH = 30.0f;

        if (!bLightTheme)
        {
            Gdiplus::RectF textShadowRect(0.0f, textY + 1.5f, (float)width, textH);
            g.DrawString(m_statusText, -1, &font, textShadowRect, &format, &shadowBrush);
        }
        Gdiplus::RectF textRect(0.0f, textY, (float)width, textH);
        g.DrawString(m_statusText, -1, &font, textRect, &format, &textBrush);
    }
}
