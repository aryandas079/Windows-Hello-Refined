#include "FaceOverlay.h"
#include "common.h"
#include <strsafe.h>
#include <cmath>
#include <mmsystem.h>
#include <shlwapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")

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
    Hide();
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
        if (customText)
        {
            StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), customText);
        }
        else
        {
            StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Hello, %s", m_username);
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

void FaceOverlay::Hide()
{
    EnterCriticalSection(&m_cs);
    m_bRunning = FALSE;
    LeaveCriticalSection(&m_cs);

    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, 500);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

DWORD WINAPI FaceOverlay::AnimationThread(LPVOID lpParam)
{
    FaceOverlay* pThis = reinterpret_cast<FaceOverlay*>(lpParam);
    pThis->RunAnimation();
    return 0;
}

void FaceOverlay::RunAnimation()
{
    LogOverlay(L"RunAnimation started");

    HINSTANCE hInst = g_hInst ? g_hInst : GetModuleHandleW(NULL);
    const WCHAR* className = L"WindowsHelloNativeOverlay";

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HWND hwndSec = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
    if (!hwndSec) hwndSec = FindWindowW(NULL, L"Windows Security");
    if (!hwndSec) hwndSec = FindWindowW(L"Credential Dialog Xaml Host", NULL);

    bool bIsCredUI = (m_scenario == CPUS_CREDUI || m_scenario == 3 || m_scenario == 4 ||
                      (hwndSec != NULL && IsWindowVisible(hwndSec)));

    const int winW = bIsCredUI ? 280 : 320;
    const int winH = bIsCredUI ? 112 : 160;

    HMONITOR hMon = MonitorFromWindow(hwndSec ? hwndSec : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    int monW = mi.rcWork.right - mi.rcWork.left;
    int monH = mi.rcWork.bottom - mi.rcWork.top;
    int posX = mi.rcWork.left + (monW - winW) / 2;
    int posY = mi.rcWork.top + 45; // Default near top (lock screen under camera)

    if (bIsCredUI)
    {
        // In CredUI (Windows Security dialog), replace the 9-dot PIN keypad icon and "Enter your PIN".
        int cardCenterX = mi.rcWork.left + (monW / 2);
        int cardCenterY = mi.rcWork.top + (monH / 2);

        if (hwndSec && IsWindowVisible(hwndSec))
        {
            RECT rc;
            if (GetWindowRect(hwndSec, &rc))
            {
                cardCenterX = rc.left + (rc.right - rc.left) / 2;
                cardCenterY = rc.top + (rc.bottom - rc.top) / 2;
            }
        }

        // Precise pixel alignment against Windows Security dialog:
        // Horizontally centered on card center (matches 9-dot icon at cardCenterX)
        // Positions Y so face icon (cy=30) lands exactly at 9-dot icon center (cardCenterY - 96)
        posX = cardCenterX - (winW / 2);
        posY = cardCenterY - 126;
        LogOverlay(L"CredUI dialog scenario: overlay positioned over PIN icon at (%d,%d)", posX, posY);
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        className,
        L"Windows Hello Overlay",
        WS_POPUP,
        posX, posY, winW, winH,
        NULL, NULL, hInst, NULL
    );

    if (!m_hwnd)
    {
        LogOverlay(L"CreateWindowExW failed: %lu", GetLastError());
        m_bRunning = FALSE;
        return;
    }
    LogOverlay(L"CreateWindowExW succeeded! HWND=%p at (%d,%d %dx%d)", m_hwnd, posX, posY, winW, winH);

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

    // Initial render before show
    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        RenderFrame(g, winW, winH);
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {posX, posY};
    SIZE sz = {winW, winH};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    BOOL ulwRes = UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    LogOverlay(L"First UpdateLayeredWindow: %d, err=%lu", ulwRes, GetLastError());

    SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, winW, winH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    while (m_bRunning)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // For CredUI dialog, ensure overlay follows dialog position if it appears or moves
        HWND hwndSecLoop = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
        if (!hwndSecLoop) hwndSecLoop = FindWindowW(NULL, L"Windows Security");
        if (!hwndSecLoop) hwndSecLoop = FindWindowW(L"Credential Dialog Xaml Host", NULL);

        bool bCredUILoop = (m_scenario == CPUS_CREDUI || m_scenario == 3 || m_scenario == 4 ||
                            (hwndSecLoop != NULL && IsWindowVisible(hwndSecLoop)));

        if (bCredUILoop && hwndSecLoop && IsWindowVisible(hwndSecLoop))
        {
            RECT rcSec;
            if (GetWindowRect(hwndSecLoop, &rcSec))
            {
                int cardCenterX = rcSec.left + (rcSec.right - rcSec.left) / 2;
                int cardCenterY = rcSec.top + (rcSec.bottom - rcSec.top) / 2;
                int targetX = cardCenterX - (winW / 2);
                int targetY = cardCenterY - 126;
                if (targetX != posX || targetY != posY)
                {
                    posX = targetX;
                    posY = targetY;
                    ptDst.x = posX;
                    ptDst.y = posY;
                }
            }
        }

        // Maintain topmost Z-order at the anchored position
        SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, winW, winH, SWP_NOACTIVATE | SWP_NOREDRAW);

        EnterCriticalSection(&m_cs);
        OVERLAY_STATE curState = m_state;
        DWORD now = GetTickCount();

        // Update animation parameters
        if (curState == OVERLAY_VERIFIED && m_verifiedTime > 0)
        {
            DWORD elapsed = now - m_verifiedTime;
            // Spin takes 0.6 seconds
            if (elapsed < 600)
            {
                float t = (float)elapsed / 600.0f;
                // EaseInOut cubic
                m_spinProgress = (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f);
            }
            else
            {
                m_spinProgress = 1.0f;
            }

            // Wink occurs after spin finishes (at 750ms to 1050ms)
            if (elapsed >= 750 && elapsed < 1050)
            {
                float wt = (float)(elapsed - 750) / 300.0f;
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

            // Fade out after 2.5 seconds
            if (elapsed > 2500)
            {
                float ft = (float)(elapsed - 2500) / 400.0f;
                if (ft >= 1.0f)
                {
                    m_fadeAlpha = 0.0f;
                    m_bRunning = FALSE;
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
            // Display error for 2.0s then smoothly fade out over 400ms to reveal native PIN box
            if (elapsed > 2000)
            {
                float ft = (float)(elapsed - 2000) / 400.0f;
                if (ft >= 1.0f)
                {
                    m_fadeAlpha = 0.0f;
                    m_bRunning = FALSE;
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
            g.Clear(Gdiplus::Color(0, 0, 0, 0)); // Pure transparent background

            RenderFrame(g, winW, winH);
        }

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
    bool bIsCredUI = (m_scenario == CPUS_CREDUI || m_scenario == 3 || m_scenario == 4 ||
                      FindWindowW(L"Credential Dialog Xaml Host", NULL) != NULL ||
                      FindWindowW(NULL, L"Windows Security") != NULL);
    bool bLightTheme = false;

    float cx = width / 2.0f;
    float cy = bIsCredUI ? 30.0f : 44.0f;

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

        // Draw solid card background to cleanly replace the 9-dots icon and "Enter your PIN"
        Gdiplus::Color cardBg = bLightTheme ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 32, 32, 32);
        Gdiplus::SolidBrush cardBgBrush(cardBg);
        g.FillRectangle(&cardBgBrush, 0, 0, width, height);
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
    Gdiplus::Pen accentPen(accentColor, bIsCredUI ? 4.5f : 5.5f);
    accentPen.SetStartCap(Gdiplus::LineCapRound);
    accentPen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::Pen shadowPen(shadowColor, bIsCredUI ? 4.5f : 5.5f);
    shadowPen.SetStartCap(Gdiplus::LineCapRound);
    shadowPen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush shadowBrush(shadowColor);

    float arcR = bIsCredUI ? 22.0f : 28.0f;
    float eyeOffset = bIsCredUI ? 14.0f : 18.0f;
    float eyeYOffset = bIsCredUI ? 8.0f : 10.0f;

    if (m_state == OVERLAY_SCANNING)
    {
        // ── SCANNING PHASE (Looking for you...) ──
        DWORD now = GetTickCount();
        DWORD elapsed = now - m_startTime;
        float gazeX = std::sin(elapsed * 0.0035f) * (bIsCredUI ? 5.0f : 6.5f); // Eyes scan left & right

        // Top arc (shadow + accent)
        if (!bLightTheme)
        {
            Gdiplus::RectF arcShadow(cx - arcR, cy - arcR + 1.5f, arcR * 2.0f, arcR * 2.0f);
            g.DrawArc(&shadowPen, arcShadow, 200.0f, 140.0f);
        }

        Gdiplus::RectF arcRect(cx - arcR, cy - arcR, arcR * 2.0f, arcR * 2.0f);
        g.DrawArc(&accentPen, arcRect, 200.0f, 140.0f);

        // Center animated scanning eye
        float rScan = bIsCredUI ? 11.0f : 14.0f;
        if (!bLightTheme)
        {
            g.FillEllipse(&shadowBrush, cx + gazeX - rScan, cy + 2.0f - rScan + 1.5f, rScan * 2.0f, rScan * 2.0f);
        }
        g.FillEllipse(&accentBrush, cx + gazeX - rScan, cy + 2.0f - rScan, rScan * 2.0f, rScan * 2.0f);
    }
    else if (m_state == OVERLAY_VERIFIED)
    {
        // ── VERIFIED PHASE (Spin 900 deg into smile + wink) ──
        float angle = 180.0f + m_spinProgress * 900.0f;

        // Rotated smile arc
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
            float rScan = (bIsCredUI ? 11.0f : 14.0f) * (1.0f - m_spinProgress / 0.4f);
            g.FillEllipse(&fadeEyeBrush, cx - rScan, cy + 2.0f - rScan, rScan * 2.0f, rScan * 2.0f);
        }

        // Two happy eyes pop in as spin completes
        if (m_spinProgress > 0.6f)
        {
            float pop = (m_spinProgress - 0.6f) / 0.4f;
            float eyeR = (bIsCredUI ? 4.8f : 6.0f) * pop;

            // Left eye
            if (!bLightTheme)
            {
                g.FillEllipse(&shadowBrush, cx - eyeOffset - eyeR, cy - eyeYOffset - eyeR + 1.5f, eyeR * 2.0f, eyeR * 2.0f);
            }
            g.FillEllipse(&accentBrush, cx - eyeOffset - eyeR, cy - eyeYOffset - eyeR, eyeR * 2.0f, eyeR * 2.0f);

            // Right eye (winks)
            if (m_winkProgress > 0.01f)
            {
                float winkHeight = eyeR * 2.0f * (1.0f - m_winkProgress * 0.85f);
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
    }

    // ── Status / Welcome Text ──
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, bIsCredUI ? 11.0f : 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    float textY = bIsCredUI ? 62.0f : 91.0f;

    if (!bLightTheme)
    {
        Gdiplus::RectF textShadowRect(0.0f, textY + 1.5f, (float)width, 36.0f);
        Gdiplus::SolidBrush textShadowBrush(Gdiplus::Color(140, 0, 0, 0));
        g.DrawString(m_statusText, -1, &font, textShadowRect, &format, &textShadowBrush);
    }

    Gdiplus::RectF textRect(0.0f, textY, (float)width, 36.0f);
    g.DrawString(m_statusText, -1, &font, textRect, &format, &textBrush);
}
