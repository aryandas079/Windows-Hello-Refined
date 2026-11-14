#pragma once

#include <windows.h>
#include <gdiplus.h>

enum OVERLAY_STATE
{
    OVERLAY_HIDDEN = 0,
    OVERLAY_SCANNING,
    OVERLAY_VERIFIED,
    OVERLAY_FAILED
};

class FaceOverlay
{
public:
    FaceOverlay();
    ~FaceOverlay();

    void Show(const WCHAR* username = L"Aryan", int scenario = 1);
    void SetState(OVERLAY_STATE state, const WCHAR* customText = nullptr);
    void SetLowLight(bool bLowLight);
    void Hide(bool bForce = false);

private:
    static DWORD WINAPI AnimationThread(LPVOID lpParam);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RunAnimation();
    void RenderFrame(Gdiplus::Graphics& g, int width, int height);

    HWND m_hwnd;
    HANDLE m_hThread;
    BOOL m_bRunning;
    OVERLAY_STATE m_state;
    int m_scenario;
    WCHAR m_username[128];
    WCHAR m_statusText[256];
    CRITICAL_SECTION m_cs;

    DWORD m_startTime;
    DWORD m_verifiedTime;
    DWORD m_failedTime;
    bool m_bPlayedWinkSound;
    bool m_bLowLight;
    float m_spinProgress;   // 0.0 to 1.0 during spin
    float m_winkProgress;   // 0.0 to 1.0 during wink
    float m_fadeAlpha;      // 0.0 to 1.0
};
