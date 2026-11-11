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
    m_bPlayedWinkSound(false),
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

void FaceOverlay::Show(const WCHAR* username)
{
    EnterCriticalSection(&m_cs);
    if (username && wcslen(username) > 0)
    {
        StringCchCopyW(m_username, ARRAYSIZE(m_username), username);
    }
    m_state = OVERLAY_SCANNING;
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Looking for you...");
    m_startTime = GetTickCount();
    m_verifiedTime = 0;
    m_spinProgress = 0.0f;
    m_winkProgress = 0.0f;
    m_fadeAlpha = 1.0f;

    if (!m_bRunning)
    {
        m_bRunning = TRUE;
        LogOverlay(L"Spawning AnimationThread...");
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

    const int winW = 320;
    const int winH = 160;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = 45;

    // Check if a Windows Security dialog is open (CredUI scenario)
    HWND hwndSec = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
    if (!hwndSec) hwndSec = FindWindowW(NULL, L"Windows Security");
    if (!hwndSec) hwndSec = FindWindowW(L"Credential Dialog Xaml Host", NULL);

    if (hwndSec && IsWindowVisible(hwndSec))
    {
        RECT rc;
        if (GetWindowRect(hwndSec, &rc))
        {
            posX = rc.left + ((rc.right - rc.left) - winW) / 2;
            posY = rc.top - winH + 35;
            if (posY < 10) posY = rc.top + 10;
            LogOverlay(L"CredUI dialog found at (%ld,%ld %ldx%ld). Anchoring overlay at (%d,%d)",
                       rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, posX, posY);
        }
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

        // Anchor dynamically to CredUI dialog if active
        HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
        if (hwndDlg && IsWindowVisible(hwndDlg))
        {
            RECT rc;
            if (GetWindowRect(hwndDlg, &rc))
            {
                int targetX = rc.left + ((rc.right - rc.left) - winW) / 2;
                int targetY = rc.top - winH + 35;
                if (targetY < 10) targetY = rc.top + 10;
                SetWindowPos(m_hwnd, HWND_TOPMOST, targetX, targetY, winW, winH, SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOSIZE);
            }
        }

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
    float cx = width / 2.0f;
    float cy = 44.0f;

    Gdiplus::Color white(255, 255, 255, 255);
    Gdiplus::SolidBrush whiteBrush(white);
    Gdiplus::Pen whitePen(white, 5.5f);
    whitePen.SetStartCap(Gdiplus::LineCapRound);
    whitePen.SetEndCap(Gdiplus::LineCapRound);

    // Subtle drop shadow for high contrast on light backgrounds
    Gdiplus::Color shadowColor(100, 0, 0, 0);
    Gdiplus::Pen shadowPen(shadowColor, 5.5f);
    shadowPen.SetStartCap(Gdiplus::LineCapRound);
    shadowPen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush shadowBrush(shadowColor);

    if (m_state == OVERLAY_SCANNING)
    {
        // ── SCANNING PHASE (Looking for you...) ──
        DWORD now = GetTickCount();
        DWORD elapsed = now - m_startTime;
        float gazeX = std::sin(elapsed * 0.0035f) * 6.5f; // Eyes scan left & right

        // Top arc (shadow + white)
        Gdiplus::RectF arcShadow(cx - 28.0f, cy - 28.0f + 1.5f, 56.0f, 56.0f);
        g.DrawArc(&shadowPen, arcShadow, 200.0f, 140.0f);

        Gdiplus::RectF arcRect(cx - 28.0f, cy - 28.0f, 56.0f, 56.0f);
        g.DrawArc(&whitePen, arcRect, 200.0f, 140.0f);

        // Center animated scanning eye
        float rScan = 14.0f;
        g.FillEllipse(&shadowBrush, cx + gazeX - rScan, cy + 3.0f - rScan + 1.5f, rScan * 2.0f, rScan * 2.0f);
        g.FillEllipse(&whiteBrush, cx + gazeX - rScan, cy + 3.0f - rScan, rScan * 2.0f, rScan * 2.0f);
    }
    else if (m_state == OVERLAY_VERIFIED)
    {
        // ── VERIFIED PHASE (Spin 900 deg into smile + wink) ──
        float angle = 180.0f + m_spinProgress * 900.0f;

        // Rotated smile arc
        Gdiplus::Matrix mat;
        mat.RotateAt(angle, Gdiplus::PointF(cx, cy));
        g.SetTransform(&mat);

        Gdiplus::RectF arcRect(cx - 28.0f, cy - 28.0f, 56.0f, 56.0f);
        g.DrawArc(&shadowPen, cx - 28.0f, cy - 28.0f + 1.5f, 56.0f, 56.0f, 20.0f, 140.0f);
        g.DrawArc(&whitePen, arcRect, 20.0f, 140.0f);

        g.ResetTransform();

        // Big scanning eye fades out as spin starts
        if (m_spinProgress < 0.4f)
        {
            float eyeAlpha = (1.0f - m_spinProgress / 0.4f) * 255.0f;
            Gdiplus::SolidBrush fadeEyeBrush(Gdiplus::Color((BYTE)eyeAlpha, 255, 255, 255));
            float rScan = 14.0f * (1.0f - m_spinProgress / 0.4f);
            g.FillEllipse(&fadeEyeBrush, cx - rScan, cy + 3.0f - rScan, rScan * 2.0f, rScan * 2.0f);
        }

        // Two happy eyes pop in as spin completes
        if (m_spinProgress > 0.6f)
        {
            float pop = (m_spinProgress - 0.6f) / 0.4f;
            float eyeR = 6.0f * pop;

            // Left eye
            g.FillEllipse(&shadowBrush, cx - 18.0f - eyeR, cy - 10.0f - eyeR + 1.5f, eyeR * 2.0f, eyeR * 2.0f);
            g.FillEllipse(&whiteBrush, cx - 18.0f - eyeR, cy - 10.0f - eyeR, eyeR * 2.0f, eyeR * 2.0f);

            // Right eye (winks)
            if (m_winkProgress > 0.01f)
            {
                float winkHeight = eyeR * 2.0f * (1.0f - m_winkProgress * 0.85f);
                if (winkHeight < 2.0f) winkHeight = 2.0f;
                g.FillEllipse(&shadowBrush, cx + 18.0f - eyeR, cy - 10.0f - winkHeight / 2.0f + 1.5f, eyeR * 2.0f, winkHeight);
                g.FillEllipse(&whiteBrush, cx + 18.0f - eyeR, cy - 10.0f - winkHeight / 2.0f, eyeR * 2.0f, winkHeight);
            }
            else
            {
                g.FillEllipse(&shadowBrush, cx + 18.0f - eyeR, cy - 10.0f - eyeR + 1.5f, eyeR * 2.0f, eyeR * 2.0f);
                g.FillEllipse(&whiteBrush, cx + 18.0f - eyeR, cy - 10.0f - eyeR, eyeR * 2.0f, eyeR * 2.0f);
            }
        }
    }
    else if (m_state == OVERLAY_FAILED)
    {
        Gdiplus::Color dim(180, 210, 210, 210);
        Gdiplus::Pen dimPen(dim, 4.0f);
        dimPen.SetStartCap(Gdiplus::LineCapRound);
        dimPen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush dimBrush(dim);

        // Neutral mouth line
        g.DrawLine(&dimPen, cx - 18.0f, cy + 18.0f, cx + 18.0f, cy + 18.0f);
        // Neutral eyes
        g.FillEllipse(&dimBrush, cx - 16.0f - 4.0f, cy - 8.0f - 4.0f, 8.0f, 8.0f);
        g.FillEllipse(&dimBrush, cx + 16.0f - 4.0f, cy - 8.0f - 4.0f, 8.0f, 8.0f);
    }

    // ── Status / Welcome Text with Text Shadow ──
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF textShadowRect(0.0f, 92.5f, (float)width, 40.0f);
    Gdiplus::SolidBrush textShadowBrush(Gdiplus::Color(140, 0, 0, 0));
    g.DrawString(m_statusText, -1, &font, textShadowRect, &format, &textShadowBrush);

    Gdiplus::RectF textRect(0.0f, 91.0f, (float)width, 40.0f);
    g.DrawString(m_statusText, -1, &font, textRect, &format, &whiteBrush);
}
