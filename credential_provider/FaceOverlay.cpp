#include "FaceOverlay.h"
#include <strsafe.h>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

extern HINSTANCE g_hInst;

FaceOverlay::FaceOverlay() :
    m_hwnd(NULL),
    m_hThread(NULL),
    m_bRunning(FALSE),
    m_state(OVERLAY_HIDDEN),
    m_startTime(0),
    m_verifiedTime(0),
    m_spinProgress(0.0f),
    m_winkProgress(0.0f),
    m_fadeAlpha(1.0f)
{
    InitializeCriticalSection(&m_cs);
    StringCchCopyW(m_username, ARRAYSIZE(m_username), L"Aryan");
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Looking for you...");
}

FaceOverlay::~FaceOverlay()
{
    Hide();
    DeleteCriticalSection(&m_cs);
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
        if (customText)
        {
            StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), customText);
        }
        else
        {
            StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Hello, %s", m_username);
        }
    }
    else if (state == OVERLAY_FAILED)
    {
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), customText ? customText : L"Couldn't recognize face. Use PIN.");
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
    // Register window class
    const WCHAR* className = L"WindowsHelloNativeOverlay";
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    const int winW = 240;
    const int winH = 150;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = 45;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        className,
        L"Windows Hello Overlay",
        WS_POPUP,
        posX, posY, winW, winH,
        NULL, NULL, g_hInst, NULL
    );

    if (!m_hwnd)
    {
        m_bRunning = FALSE;
        return;
    }

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // Create 32-bit ARGB DIB Section for per-pixel alpha blending
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
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    while (m_bRunning)
    {
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
                // Wink curve: 0 -> 1 -> 0
                m_winkProgress = std::sin(wt * 3.14159265f);
            }
            else
            {
                m_winkProgress = 0.0f;
            }

            // Fade out after 1.8 seconds
            if (elapsed > 1800)
            {
                float ft = (float)(elapsed - 1800) / 400.0f;
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
        POINT ptSrc = {0, 0};
        POINT ptDst = {posX, posY};
        SIZE sz = {winW, winH};
        BYTE finalAlpha = (BYTE)(m_fadeAlpha * 255.0f);

        BLENDFUNCTION blend = {0};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = finalAlpha;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        Sleep(16); // ~60 FPS
    }

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
    float cy = 42.0f;

    Gdiplus::Color white(255, 255, 255, 255);
    Gdiplus::SolidBrush whiteBrush(white);
    Gdiplus::Pen whitePen(white, 5.5f);
    whitePen.SetStartCap(Gdiplus::LineCapRound);
    whitePen.SetEndCap(Gdiplus::LineCapRound);

    if (m_state == OVERLAY_SCANNING)
    {
        // ── Phase 1: SCANNING (Image 2 style) ──
        // Upward arc at top (smile curve rotated upward)
        Gdiplus::RectF arcRect(cx - 28.0f, cy - 28.0f, 56.0f, 56.0f);
        g.DrawArc(&whitePen, arcRect, 200.0f, 140.0f); // 200 to 340 deg = top upward curve!

        // Big single center eye below top arc
        float rScan = 14.0f;
        g.FillEllipse(&whiteBrush, cx - rScan, cy + 3.0f - rScan, rScan * 2.0f, rScan * 2.0f);
    }
    else if (m_state == OVERLAY_VERIFIED)
    {
        // ── Phase 2: SPIN + HAPPY SMILE (Image 1 & Image 3) ──
        float angle = 180.0f + m_spinProgress * 900.0f; // 180 deg -> 1080 deg (0 deg = smile at bottom)

        // Draw rotated smile arc
        Gdiplus::Matrix mat;
        mat.RotateAt(angle, Gdiplus::PointF(cx, cy));
        g.SetTransform(&mat);

        // Smile arc: at 0 deg, drawn at bottom (20 deg to 160 deg)
        Gdiplus::RectF arcRect(cx - 28.0f, cy - 28.0f, 56.0f, 56.0f);
        g.DrawArc(&whitePen, arcRect, 20.0f, 140.0f);

        // Reset transform for drawing eyes
        g.ResetTransform();

        // Big scan eye fades out as spin starts
        if (m_spinProgress < 0.4f)
        {
            float eyeAlpha = (1.0f - m_spinProgress / 0.4f) * 255.0f;
            Gdiplus::SolidBrush fadeEyeBrush(Gdiplus::Color((BYTE)eyeAlpha, 255, 255, 255));
            float rScan = 14.0f * (1.0f - m_spinProgress / 0.4f);
            g.FillEllipse(&fadeEyeBrush, cx - rScan, cy + 3.0f - rScan, rScan * 2.0f, rScan * 2.0f);
        }

        // Two small eyes pop in as spin completes (m_spinProgress > 0.6)
        if (m_spinProgress > 0.6f)
        {
            float pop = (m_spinProgress - 0.6f) / 0.4f; // 0 to 1
            float eyeR = 6.0f * pop;

            // Left Eye (always open)
            g.FillEllipse(&whiteBrush, cx - 18.0f - eyeR, cy - 10.0f - eyeR, eyeR * 2.0f, eyeR * 2.0f);

            // Right Eye (winks if m_winkProgress > 0)
            if (m_winkProgress > 0.01f)
            {
                // Wink = flat horizontal slit
                float winkHeight = eyeR * 2.0f * (1.0f - m_winkProgress * 0.85f);
                if (winkHeight < 2.0f) winkHeight = 2.0f;
                g.FillEllipse(&whiteBrush, cx + 18.0f - eyeR, cy - 10.0f - winkHeight / 2.0f, eyeR * 2.0f, winkHeight);
            }
            else
            {
                g.FillEllipse(&whiteBrush, cx + 18.0f - eyeR, cy - 10.0f - eyeR, eyeR * 2.0f, eyeR * 2.0f);
            }
        }
    }
    else if (m_state == OVERLAY_FAILED)
    {
        // Neutral expression on failure
        Gdiplus::Color dim(180, 200, 200, 200);
        Gdiplus::Pen dimPen(dim, 4.0f);
        dimPen.SetStartCap(Gdiplus::LineCapRound);
        dimPen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush dimBrush(dim);

        // Straight mouth
        g.DrawLine(&dimPen, cx - 18.0f, cy + 18.0f, cx + 18.0f, cy + 18.0f);
        // Dim eyes
        g.FillEllipse(&dimBrush, cx - 16.0f - 4.0f, cy - 8.0f - 4.0f, 8.0f, 8.0f);
        g.FillEllipse(&dimBrush, cx + 16.0f - 4.0f, cy - 8.0f - 4.0f, 8.0f, 8.0f);
    }

    // ── Status / Welcome Text ──
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, 11.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF textRect(0.0f, 88.0f, (float)width, 40.0f);
    g.DrawString(m_statusText, -1, &font, textRect, &format, &whiteBrush);
}
