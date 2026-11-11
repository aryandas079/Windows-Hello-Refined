#define SECURITY_WIN32
#include "FaceCredential.h"
#include "FaceCredentialProvider.h"
#include <security.h>
#include <wincred.h>
#include <shlwapi.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <ntsecapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "credui.lib")

extern HINSTANCE g_hInst;

static void LogMsg(const WCHAR* fmt, ...)
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
        fwprintf(f, L"[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

static void SendKey(WORD vk, bool bExtended = false);
static bool IsCredUIDialogOpen();

static LPWSTR QueryActiveSessionUserSid()
{
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) return nullptr;

    HANDLE hToken = NULL;
    if (WTSQueryUserToken(sessionId, &hToken))
    {
        DWORD dwSize = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
        if (dwSize > 0)
        {
            PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwSize);
            if (pTokenUser && GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize))
            {
                LPWSTR pszSid = NULL;
                if (ConvertSidToStringSidW(pTokenUser->User.Sid, &pszSid))
                {
                    LPWSTR result = NULL;
                    SHStrDupW(pszSid, &result);
                    LocalFree(pszSid);
                    free(pTokenUser);
                    CloseHandle(hToken);
                    return result;
                }
            }
            if (pTokenUser) free(pTokenUser);
        }
        CloseHandle(hToken);
    }
    return nullptr;
}

static HRESULT PackLogonCredentials(
    LPCWSTR pszDomain,
    LPCWSTR pszUsername,
    LPCWSTR pszPassword,
    ULONG* pulAuthPackage,
    BYTE** ppbSerialization,
    DWORD* pcbSerialization)
{
    if (!pulAuthPackage || !ppbSerialization || !pcbSerialization)
        return E_POINTER;

    *pulAuthPackage = 0;
    *ppbSerialization = NULL;
    *pcbSerialization = 0;

    HANDLE hLsa = NULL;
    NTSTATUS status = LsaConnectUntrusted(&hLsa);
    if (status != 0)
    {
        LogMsg(L"LsaConnectUntrusted failed: 0x%08X", status);
        return E_FAIL;
    }

    LSA_STRING pkgName;
    pkgName.Buffer = (PCHAR)NEGOSSP_NAME_A;
    pkgName.Length = (USHORT)strlen(NEGOSSP_NAME_A);
    pkgName.MaximumLength = pkgName.Length + 1;

    status = LsaLookupAuthenticationPackage(hLsa, &pkgName, pulAuthPackage);
    LsaDeregisterLogonProcess(hLsa);
    if (status != 0)
    {
        LogMsg(L"LsaLookupAuthenticationPackage (Negotiate) failed: 0x%08X", status);
        return E_FAIL;
    }

    WCHAR szFullUser[512] = {0};
    WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD cchComp = ARRAYSIZE(computerName);
    GetComputerNameW(computerName, &cchComp);

    if (wcsstr(pszUsername, L"@") != NULL)
    {
        // Email / Microsoft Account format (e.g. aryandas079@outlook.com)
        StringCchCopyW(szFullUser, ARRAYSIZE(szFullUser), pszUsername);
    }
    else if (pszDomain && wcslen(pszDomain) > 0 &&
             _wcsicmp(pszDomain, L".") != 0 &&
             _wcsicmp(pszDomain, computerName) != 0)
    {
        // Actual domain (not local computer name)
        StringCchPrintfW(szFullUser, ARRAYSIZE(szFullUser), L"%s\\%s", pszDomain, pszUsername);
    }
    else
    {
        // Local username without domain prefix
        StringCchCopyW(szFullUser, ARRAYSIZE(szFullUser), pszUsername);
    }

    DWORD cbBuffer = 0;
    CredPackAuthenticationBufferW(0, szFullUser, (LPWSTR)pszPassword, NULL, &cbBuffer);
    if (cbBuffer == 0)
    {
        DWORD err = GetLastError();
        LogMsg(L"CredPackAuthenticationBufferW query size failed: %d", err);
        return HRESULT_FROM_WIN32(err);
    }

    BYTE* pBuffer = (BYTE*)CoTaskMemAlloc(cbBuffer);
    if (!pBuffer)
    {
        return E_OUTOFMEMORY;
    }

    if (!CredPackAuthenticationBufferW(0, szFullUser, (LPWSTR)pszPassword, pBuffer, &cbBuffer))
    {
        DWORD err = GetLastError();
        LogMsg(L"CredPackAuthenticationBufferW pack failed: %d", err);
        CoTaskMemFree(pBuffer);
        return HRESULT_FROM_WIN32(err);
    }

    *ppbSerialization = pBuffer;
    *pcbSerialization = cbBuffer;
    LogMsg(L"Credentials packed successfully with CredPackAuthenticationBufferW for %s (pkg: %d, size: %d)", szFullUser, *pulAuthPackage, cbBuffer);
    return S_OK;
}

FaceCredential::FaceCredential() :
    m_cRef(1),
    m_cpus(CPUS_INVALID),
    m_pProvider(nullptr),
    m_pEvents(nullptr),
    m_pszUserSid(nullptr),
    m_bVerified(FALSE),
    m_hasPassword(FALSE),
    m_hThread(nullptr),
    m_bStopThread(FALSE)
{
    InitializeCriticalSection(&m_cs);
    StringCchCopyW(m_username, ARRAYSIZE(m_username), L"Aryan");
    StringCchCopyW(m_domain, ARRAYSIZE(m_domain), L"");
    StringCchCopyW(m_password, ARRAYSIZE(m_password), L"");
    StringCchCopyW(m_pin, ARRAYSIZE(m_pin), L"");
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Windows Hello: Looking for you...");
    LogMsg(L"FaceCredential constructor called");
}

FaceCredential::~FaceCredential()
{
    LogMsg(L"FaceCredential destructor called");
    m_bStopThread = TRUE;
    m_overlay.Hide();
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, 1000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    if (m_pszUserSid)
    {
        CoTaskMemFree(m_pszUserSid);
        m_pszUserSid = nullptr;
    }
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = nullptr;
    }
    DeleteCriticalSection(&m_cs);
}

void FaceCredential::Initialize(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, FaceCredentialProvider* pProvider, LPCWSTR pszUserSid)
{
    m_cpus = cpus;
    m_pProvider = pProvider;
    if (pszUserSid)
    {
        SHStrDupW(pszUserSid, &m_pszUserSid);
        LogMsg(L"Initialize: user SID provided = %s", pszUserSid);
    }
    else
    {
        m_pszUserSid = QueryActiveSessionUserSid();
        LogMsg(L"Initialize: queried active session SID = %s", m_pszUserSid ? m_pszUserSid : L"NULL");
    }

    LogMsg(L"Initialize: starting Windows Hello verification and animation from the very beginning!");
    StartVerification();
}

HRESULT FaceCredential::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_ICredentialProviderCredential)
    {
        *ppv = static_cast<ICredentialProviderCredential*>(this);
    }
    else if (riid == IID_ICredentialProviderCredential2)
    {
        *ppv = static_cast<ICredentialProviderCredential2*>(this);
    }
    else if (riid == IID_ICredentialProviderCredential3)
    {
        *ppv = static_cast<ICredentialProviderCredential3*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG FaceCredential::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

ULONG FaceCredential::Release()
{
    long cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

HRESULT FaceCredential::Advise(ICredentialProviderCredentialEvents* pcpce)
{
    LogMsg(L"FaceCredential::Advise called");
    EnterCriticalSection(&m_cs);
    if (m_pEvents)
    {
        m_pEvents->Release();
    }
    m_pEvents = pcpce;
    if (m_pEvents)
    {
        m_pEvents->AddRef();
    }
    LeaveCriticalSection(&m_cs);

    StartVerification();
    return S_OK;
}

HRESULT FaceCredential::UnAdvise()
{
    LogMsg(L"FaceCredential::UnAdvise called");
    EnterCriticalSection(&m_cs);
    m_bStopThread = TRUE;
    m_overlay.Hide();
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = nullptr;
    }
    LeaveCriticalSection(&m_cs);
    return S_OK;
}

void FaceCredential::StartVerification()
{
    EnterCriticalSection(&m_cs);
    if (m_hThread)
    {
        DWORD exitCode = 0;
        if (GetExitCodeThread(m_hThread, &exitCode) && exitCode != STILL_ACTIVE)
        {
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
    }

    if (!m_hThread && !m_bVerified)
    {
        m_bStopThread = FALSE;
        LogMsg(L"Starting verification thread...");
        m_overlay.Show(m_username, (int)m_cpus);

        bool bIsCredUI = (m_cpus == CPUS_CREDUI || (int)m_cpus == 3 || (int)m_cpus == 4 || IsCredUIDialogOpen());
        if (!bIsCredUI)
        {
            // Pre-dismiss lock screen curtain with Spacebar so LogonUI is ready immediately
            LogMsg(L"StartVerification: pre-dismissing lock screen curtain with Spacebar...");
            SendKey(VK_SPACE);
        }

        m_hThread = CreateThread(NULL, 0, VerificationThread, this, 0, NULL);
    }
    LeaveCriticalSection(&m_cs);
}

DWORD WINAPI FaceCredential::VerificationThread(LPVOID lpParam)
{
    FaceCredential* pThis = reinterpret_cast<FaceCredential*>(lpParam);
    pThis->DoVerification();
    EnterCriticalSection(&pThis->m_cs);
    if (pThis->m_hThread)
    {
        CloseHandle(pThis->m_hThread);
        pThis->m_hThread = nullptr;
    }
    LeaveCriticalSection(&pThis->m_cs);
    return 0;
}

static HBITMAP CreatePARGBDIB(int width, int height, void** ppvBits)
{
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = width;
    bi.bV5Height = height; // Bottom-up standard DIB (positive height)
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(NULL);
    HBITMAP hBitmap = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, ppvBits, NULL, 0);
    ReleaseDC(NULL, hdc);
    return hBitmap;
}

static HRESULT GetAssetBitmapBuffer(LPCWSTR filename, DWORD* pImageBufferSize, BYTE** ppImageBuffer)
{
    if (!pImageBufferSize || !ppImageBuffer) return E_POINTER;
    *pImageBufferSize = 0;
    *ppImageBuffer = nullptr;

    const int iconSize = 96;
    WCHAR pngName[64];
    StringCchCopyW(pngName, ARRAYSIZE(pngName), filename);
    WCHAR* dot = wcsrchr(pngName, L'.');
    if (dot) StringCchCopyW(dot, 5, L".png");

    WCHAR assetPath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, assetPath, ARRAYSIZE(assetPath));
    PathRemoveFileSpecW(assetPath);
    PathAppendW(assetPath, L"..\\assets");
    PathAppendW(assetPath, pngName);

    LogMsg(L"GetAssetBitmapBuffer: loading %s", assetPath);

    Gdiplus::Bitmap* pBmp = Gdiplus::Bitmap::FromFile(assetPath);
    if (!pBmp || pBmp->GetLastStatus() != Gdiplus::Ok)
    {
        if (pBmp) delete pBmp;
        PathRemoveFileSpecW(assetPath);
        PathAppendW(assetPath, filename);
        pBmp = Gdiplus::Bitmap::FromFile(assetPath);
    }

    int w = iconSize;
    int h = iconSize;
    Gdiplus::Bitmap* pTargetBmp = nullptr;

    if (pBmp && pBmp->GetLastStatus() == Gdiplus::Ok)
    {
        w = pBmp->GetWidth();
        h = pBmp->GetHeight();
        pTargetBmp = pBmp;
    }
    else
    {
        pTargetBmp = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(pTargetBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        Gdiplus::Color white(255, 255, 255, 255);
        Gdiplus::SolidBrush whiteBrush(white);
        Gdiplus::Pen whitePen(white, 6.0f);
        whitePen.SetStartCap(Gdiplus::LineCapRound);
        whitePen.SetEndCap(Gdiplus::LineCapRound);

        g.DrawArc(&whitePen, 18.0f, 16.0f, 60.0f, 60.0f, 25.0f, 130.0f);
        g.FillEllipse(&whiteBrush, 32.0f, 34.0f, 10.0f, 10.0f);
        if (wcsstr(filename, L"wink"))
            g.DrawLine(&whitePen, 54.0f, 39.0f, 68.0f, 39.0f);
        else
            g.FillEllipse(&whiteBrush, 54.0f, 34.0f, 10.0f, 10.0f);
    }

    DWORD dwBmpSize = w * h * 4;
    DWORD dwcbFullSize = sizeof(BITMAPV5HEADER) + dwBmpSize;
    BYTE* pBuffer = (BYTE*)CoTaskMemAlloc(dwcbFullSize);
    if (!pBuffer)
    {
        if (pTargetBmp) delete pTargetBmp;
        return E_OUTOFMEMORY;
    }

    ZeroMemory(pBuffer, dwcbFullSize);
    BITMAPV5HEADER* pBmi = (BITMAPV5HEADER*)pBuffer;
    pBmi->bV5Size = sizeof(BITMAPV5HEADER);
    pBmi->bV5Width = w;
    pBmi->bV5Height = h; // Bottom-up standard DIB
    pBmi->bV5Planes = 1;
    pBmi->bV5BitCount = 32;
    pBmi->bV5Compression = BI_BITFIELDS;
    pBmi->bV5RedMask   = 0x00FF0000;
    pBmi->bV5GreenMask = 0x0000FF00;
    pBmi->bV5BlueMask  = 0x000000FF;
    pBmi->bV5AlphaMask = 0xFF000000;

    BYTE* pDstPixels = pBuffer + sizeof(BITMAPV5HEADER);
    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, w, h);

    if (pTargetBmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) == Gdiplus::Ok)
    {
        for (int y = 0; y < h; y++)
        {
            BYTE* pSrcRow = (BYTE*)bmpData.Scan0 + (y * bmpData.Stride);
            BYTE* pDstRow = pDstPixels + ((h - 1 - y) * (w * 4));
            memcpy(pDstRow, pSrcRow, w * 4);
        }
        pTargetBmp->UnlockBits(&bmpData);
    }

    delete pTargetBmp;

    *pImageBufferSize = dwcbFullSize;
    *ppImageBuffer = pBuffer;
    LogMsg(L"GetAssetBitmapBuffer: successfully created %dx%d PARGB buffer (%d bytes)", w, h, dwcbFullSize);
    return S_OK;
}

static HBITMAP LoadAssetBitmap(LPCWSTR filename)
{
    const int iconSize = 128;
    WCHAR pngName[64];
    StringCchCopyW(pngName, ARRAYSIZE(pngName), filename);
    WCHAR* dot = wcsrchr(pngName, L'.');
    if (dot) StringCchCopyW(dot, 5, L".png");

    WCHAR assetPath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, assetPath, ARRAYSIZE(assetPath));
    PathRemoveFileSpecW(assetPath);
    PathAppendW(assetPath, L"..\\assets");
    PathAppendW(assetPath, pngName);

    LogMsg(L"LoadAssetBitmap: attempting to load %s", assetPath);

    Gdiplus::Bitmap* pBmp = Gdiplus::Bitmap::FromFile(assetPath);
    if (!pBmp || pBmp->GetLastStatus() != Gdiplus::Ok)
    {
        if (pBmp) delete pBmp;
        PathRemoveFileSpecW(assetPath);
        PathAppendW(assetPath, filename);
        pBmp = Gdiplus::Bitmap::FromFile(assetPath);
    }

    if (pBmp && pBmp->GetLastStatus() == Gdiplus::Ok)
    {
        int w = pBmp->GetWidth();
        int h = pBmp->GetHeight();
        void* pBits = NULL;
        HBITMAP hDIB = CreatePARGBDIB(w, h, &pBits);
        if (hDIB && pBits)
        {
            Gdiplus::BitmapData bmpData;
            Gdiplus::Rect rect(0, 0, w, h);
            if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) == Gdiplus::Ok)
            {
                // Bottom-up DIB row copy
                for (int y = 0; y < h; y++)
                {
                    BYTE* pSrcRow = (BYTE*)bmpData.Scan0 + (y * bmpData.Stride);
                    BYTE* pDstRow = (BYTE*)pBits + ((h - 1 - y) * (w * 4));
                    memcpy(pDstRow, pSrcRow, w * 4);
                }
                pBmp->UnlockBits(&bmpData);
                delete pBmp;
                LogMsg(L"LoadAssetBitmap: successfully loaded 32-bit PARGB transparent bitmap: %s", pngName);
                return hDIB;
            }
            DeleteObject(hDIB);
        }
        delete pBmp;
    }

    // Fallback: draw pixel-perfect 100% transparent Windows Hello face in memory
    LogMsg(L"LoadAssetBitmap: drawing vector transparent icon in memory as fallback...");
    Gdiplus::Bitmap memBmp(iconSize, iconSize, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&memBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0)); // 100% transparent background

        Gdiplus::Color white(255, 255, 255, 255);
        Gdiplus::SolidBrush whiteBrush(white);
        Gdiplus::Pen whitePen(white, 7.5f);
        whitePen.SetStartCap(Gdiplus::LineCapRound);
        whitePen.SetEndCap(Gdiplus::LineCapRound);

        // Smile arc
        g.DrawArc(&whitePen, 22.0f, 20.0f, 84.0f, 84.0f, 25.0f, 130.0f);

        // Left eye
        g.FillEllipse(&whiteBrush, 40.0f, 44.0f, 14.0f, 14.0f);

        // Right eye: wink line or open eye
        if (wcsstr(filename, L"wink"))
        {
            g.DrawLine(&whitePen, 72.0f, 51.0f, 90.0f, 51.0f);
        }
        else
        {
            g.FillEllipse(&whiteBrush, 74.0f, 44.0f, 14.0f, 14.0f);
        }
    }

    void* pBits = NULL;
    HBITMAP hDIB = CreatePARGBDIB(iconSize, iconSize, &pBits);
    if (hDIB && pBits)
    {
        Gdiplus::BitmapData bmpData;
        Gdiplus::Rect rect(0, 0, iconSize, iconSize);
        if (memBmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) == Gdiplus::Ok)
        {
            for (int y = 0; y < iconSize; y++)
            {
                BYTE* pSrcRow = (BYTE*)bmpData.Scan0 + (y * bmpData.Stride);
                BYTE* pDstRow = (BYTE*)pBits + ((iconSize - 1 - y) * (iconSize * 4));
                memcpy(pDstRow, pSrcRow, iconSize * 4);
            }
            memBmp.UnlockBits(&bmpData);
            LogMsg(L"LoadAssetBitmap: in-memory transparent vector bitmap ready.");
            return hDIB;
        }
        DeleteObject(hDIB);
    }

    return NULL;
}

static void ExtractJsonString(const char* json, const char* key, WCHAR* outBuf, DWORD maxChars)
{
    char searchKey[128];
    StringCchPrintfA(searchKey, ARRAYSIZE(searchKey), "\"%s\": \"", key);
    const char* p = strstr(json, searchKey);
    if (!p) return;
    p += strlen(searchKey);
    char val[256] = {0};
    int i = 0;
    while (*p && *p != '\"' && i < 255)
    {
        val[i++] = *p++;
    }
    MultiByteToWideChar(CP_UTF8, 0, val, -1, outBuf, maxChars);
}

static void SendKey(WORD vk, bool bExtended)
{
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inputs[0].ki.dwFlags = bExtended ? KEYEVENTF_EXTENDEDKEY : 0;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.wScan = inputs[0].ki.wScan;
    inputs[1].ki.dwFlags = (bExtended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;

    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2)
    {
        LogMsg(L"SendKey(0x%04X) SendInput failed! sent=%u, err=%lu", vk, sent, GetLastError());
    }
    Sleep(2);
}

static void SendUnicodeChar(WCHAR ch)
{
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2)
    {
        LogMsg(L"SendUnicodeChar('%c') SendInput failed! sent=%u, err=%lu", ch, sent, GetLastError());
    }
    Sleep(3);
}

static bool IsLockScreenDesktop()
{
    HDESK hInput = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!hInput) return false;
    WCHAR deskName[64] = {0};
    DWORD needed = 0;
    BOOL ok = GetUserObjectInformationW(hInput, UOI_NAME, deskName, sizeof(deskName), &needed);
    CloseDesktop(hInput);
    if (ok && _wcsicmp(deskName, L"Winlogon") == 0)
    {
        return true;
    }
    return false;
}

static bool IsCredUIDialogOpen()
{
    HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
    if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
    if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
    return (hwndDlg && IsWindowVisible(hwndDlg));
}

static void SimulatePinInput(LPCWSTR pszPin, bool bIsCredUI = false)
{
    if (!pszPin || wcslen(pszPin) == 0) return;

    LogMsg(L"SimulatePinInput: starting simulation for %d characters (bIsCredUI=%d)...", (int)wcslen(pszPin), bIsCredUI);

    HDESK hInputDesk = NULL;
    if (!bIsCredUI)
    {
        hInputDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD | DESKTOP_JOURNALPLAYBACK);
        if (!hInputDesk)
        {
            hInputDesk = OpenDesktopW(L"Winlogon", 0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD | DESKTOP_JOURNALPLAYBACK);
        }
        if (hInputDesk)
        {
            SetThreadDesktop(hInputDesk);
            LogMsg(L"SimulatePinInput: attached to Winlogon input desktop.");
        }
    }

    if (!bIsCredUI)
    {
        // Curtain was already pre-dismissed in StartVerification. Send a quick Spacebar tap and short sleep
        LogMsg(L"SimulatePinInput: ensuring lock screen curtain is dismissed...");
        SendKey(VK_SPACE);
        Sleep(60);
    }
    else
    {
        // In CredUI dialog box: ensure the dialog window has focus
        HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
        if (hwndDlg)
        {
            SetForegroundWindow(hwndDlg);
            BringWindowToTop(hwndDlg);
            Sleep(30);
        }
    }

    // Step 2: Clear any previous input from the password/PIN box
    for (int b = 0; b < 8; b++)
    {
        SendKey(VK_BACK);
    }
    Sleep(15);

    // Step 3: Type each character with genuine hardware Unicode packets (KEYEVENTF_UNICODE)
    LogMsg(L"SimulatePinInput: typing password/PIN characters with KEYEVENTF_UNICODE...");
    for (size_t i = 0; i < wcslen(pszPin); i++)
    {
        SendUnicodeChar(pszPin[i]);
    }

    // Step 4: Submit password/PIN with Enter
    Sleep(35);
    SendKey(VK_RETURN);
    LogMsg(L"SimulatePinInput: password/PIN entered and submitted.");

    if (hInputDesk)
    {
        CloseDesktop(hInputDesk);
    }
}

static void EnsureDaemonRunning()
{
    const WCHAR* pipeName = L"\\\\.\\pipe\\WindowsHelloRefinedPipe";
    if (WaitNamedPipeW(pipeName, 50))
    {
        LogMsg(L"EnsureDaemonRunning: face service pipe already active.");
        return;
    }

    LogMsg(L"EnsureDaemonRunning: pipe not detected, auto-launching face_service daemon for startup/reboot...");

    WCHAR dllPath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, dllPath, ARRAYSIZE(dllPath));
    PathRemoveFileSpecW(dllPath); // remove FaceCredentialProvider.dll -> credential_provider
    PathRemoveFileSpecW(dllPath); // remove credential_provider -> Project Root

    WCHAR pythonExe[MAX_PATH] = {0};
    StringCchPrintfW(pythonExe, ARRAYSIZE(pythonExe), L"%s\\.venv\\Scripts\\pythonw.exe", dllPath);

    WCHAR serviceScript[MAX_PATH] = {0};
    StringCchPrintfW(serviceScript, ARRAYSIZE(serviceScript), L"%s\\service\\face_service.py", dllPath);

    if (!PathFileExistsW(pythonExe) || !PathFileExistsW(serviceScript))
    {
        LogMsg(L"EnsureDaemonRunning: executable not found at %s or %s", pythonExe, serviceScript);
        return;
    }

    WCHAR cmdLine[MAX_PATH * 3] = {0};
    StringCchPrintfW(cmdLine, ARRAYSIZE(cmdLine), L"\"%s\" \"%s\"", pythonExe, serviceScript);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};

    BOOL bCreated = CreateProcessW(
        pythonExe,
        cmdLine,
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        NULL,
        dllPath, // Set working directory to project root
        &si,
        &pi
    );

    if (bCreated)
    {
        LogMsg(L"EnsureDaemonRunning: spawned background daemon PID=%lu. Waiting for pipe initialization...", pi.dwProcessId);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        for (int i = 0; i < 35; i++)
        {
            if (WaitNamedPipeW(pipeName, 100))
            {
                LogMsg(L"EnsureDaemonRunning: daemon pipe became ready in %d ms!", (i + 1) * 100);
                return;
            }
            Sleep(100);
        }
        LogMsg(L"EnsureDaemonRunning: timed out waiting for spawned pipe.");
    }
    else
    {
        LogMsg(L"EnsureDaemonRunning: CreateProcessW failed: %lu", GetLastError());
    }
}

void FaceCredential::DoVerification()
{
    EnsureDaemonRunning();

    const WCHAR* pipeName = L"\\\\.\\pipe\\WindowsHelloRefinedPipe";
    LogMsg(L"DoVerification connecting to pipe: %s", pipeName);

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    int retry = 0;
    while (!m_bStopThread && retry < 25)
    {
        hPipe = CreateFileW(
            pipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeW(pipeName, 1000);
        }
        else
        {
            Sleep(150);
        }
        retry++;
    }

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        LogMsg(L"Pipe connection failed after retries. Error: %d", err);
        EnterCriticalSection(&m_cs);
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Windows Hello service unreachable. Enter your PIN.");
        m_overlay.SetState(OVERLAY_FAILED, L"Service unreachable. Enter your PIN.");
        LeaveCriticalSection(&m_cs);
        if (m_pEvents)
        {
            m_pEvents->SetFieldString(this, FPF_STATUS_TEXT, m_statusText);
        }
        return;
    }

    LogMsg(L"Pipe connected. Sending VERIFY command...");
    const char cmd[] = "VERIFY\n";
    DWORD bytesWritten = 0;
    WriteFile(hPipe, cmd, (DWORD)strlen(cmd), &bytesWritten, NULL);

    char buffer[2048] = {0};
    DWORD bytesRead = 0;
    if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
    {
        buffer[bytesRead] = '\0';
        LogMsg(L"Pipe response received (length %d)", bytesRead);

        if (strstr(buffer, "\"status\": \"verified\""))
        {
            LogMsg(L"Face verified by daemon!");
            ExtractJsonString(buffer, "username", m_username, ARRAYSIZE(m_username));
            ExtractJsonString(buffer, "domain", m_domain, ARRAYSIZE(m_domain));
            ExtractJsonString(buffer, "password", m_password, ARRAYSIZE(m_password));
            ExtractJsonString(buffer, "pin", m_pin, ARRAYSIZE(m_pin));

            EnterCriticalSection(&m_cs);
            m_bVerified = TRUE;
            m_hasPassword = (wcslen(m_password) > 0);
            if (m_hasPassword || wcslen(m_pin) > 0)
            {
                StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Welcome back, %s!", m_username);
            }
            else
            {
                StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Face verified! Hello %s", m_username);
            }
            m_overlay.SetState(OVERLAY_VERIFIED);
            LeaveCriticalSection(&m_cs);

            if (m_pEvents)
            {
                m_pEvents->SetFieldString(this, FPF_STATUS_TEXT, m_statusText);
                m_pEvents->SetFieldState(this, FPF_SUBMIT_BUTTON, CPFS_DISPLAY_IN_SELECTED_TILE);

                HBITMAP hbmpWink = LoadAssetBitmap(L"hello_wink.bmp");
                if (hbmpWink)
                {
                    m_pEvents->SetFieldBitmap(this, FPF_LOGO, hbmpWink);
                }
            }

            bool bIsCredUI = (m_cpus == CPUS_CREDUI || (int)m_cpus == 3 || (int)m_cpus == 4 || IsCredUIDialogOpen());
            LPCWSTR secret = (wcslen(m_pin) > 0) ? m_pin : m_password;

            if (m_pProvider && !bIsCredUI)
            {
                LogMsg(L"Face verified! Triggering CredentialsChanged for lock screen...");
                m_pProvider->OnFaceVerified();
            }

            if (wcslen(secret) > 0)
            {
                if (!bIsCredUI)
                {
                    LogMsg(L"Face verified: ensuring lock screen curtain is awake...");
                    SendKey(VK_SPACE);
                }

                // Fire PIN auto-fill immediately while wink animation and chime play concurrently
                LogMsg(L"Face verified! Triggering instant password/PIN auto-fill (%d characters, bIsCredUI=%d)...",
                       (int)wcslen(secret), bIsCredUI);
                SimulatePinInput(secret, bIsCredUI);

                // Guardian watcher loop:
                // If the user clicks the screen or dialog AFTER verification has finished,
                // we detect the interaction and re-type the credentials so the user is never stuck with an empty box!
                LogMsg(L"Starting post-verification guardian loop (bIsCredUI=%d)...", bIsCredUI);
                DWORD startTime = GetTickCount();
                DWORD lastAutoRetry = startTime;

                while (!m_bStopThread && (GetTickCount() - startTime < 8000))
                {
                    Sleep(60);
                    if (m_bStopThread) break;

                    if (!bIsCredUI && !IsLockScreenDesktop())
                    {
                        LogMsg(L"Guardian: Workstation unlocked or desktop switched. Exiting guardian loop.");
                        break;
                    }

                    if (bIsCredUI && !IsCredUIDialogOpen())
                    {
                        LogMsg(L"Guardian: CredUI dialog closed. Authentication successful.");
                        break;
                    }

                    // Detect mouse click (VK_LBUTTON) or wake key (VK_SPACE, VK_RETURN)
                    bool userClicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                    bool userKey = ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) ||
                                   ((GetAsyncKeyState(VK_RETURN) & 0x8000) != 0);

                    DWORD now = GetTickCount();
                    bool periodicRetry = !bIsCredUI && (now - lastAutoRetry > 2000);

                    if (userClicked || userKey || periodicRetry)
                    {
                        LogMsg(L"Guardian: Interaction detected (click=%d, key=%d, periodic=%d). Auto-filling credentials...",
                               userClicked, userKey, periodicRetry);

                        if (userClicked || userKey)
                        {
                            Sleep(180);
                        }

                        if (m_bStopThread) break;
                        if (!bIsCredUI && !IsLockScreenDesktop()) break;
                        if (bIsCredUI && !IsCredUIDialogOpen()) break;

                        if (bIsCredUI)
                        {
                            HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
                            if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
                            if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
                            if (hwndDlg)
                            {
                                SetForegroundWindow(hwndDlg);
                                BringWindowToTop(hwndDlg);
                                SetActiveWindow(hwndDlg);
                                Sleep(60);
                            }
                        }

                        // Clear and re-type
                        for (int b = 0; b < 16; b++) SendKey(VK_BACK);
                        Sleep(25);
                        for (size_t i = 0; i < wcslen(secret); i++) SendUnicodeChar(secret[i]);
                        Sleep(50);
                        SendKey(VK_RETURN);

                        lastAutoRetry = GetTickCount();
                        Sleep(400); // Debounce
                    }
                }
            }
        }
        else
        {
            LogMsg(L"Face verification failed or timed out. Waiting for user interaction on lock screen...");
            EnterCriticalSection(&m_cs);
            StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Couldn't recognize face. Enter your PIN.");
            m_overlay.SetState(OVERLAY_FAILED, L"Couldn't recognize face. Enter your PIN.");
            LeaveCriticalSection(&m_cs);
            if (m_pEvents)
            {
                m_pEvents->SetFieldString(this, FPF_STATUS_TEXT, m_statusText);
            }
        }
    }
    else
    {
        LogMsg(L"ReadFile from pipe failed: %d", GetLastError());
    }

    CloseHandle(hPipe);
}

HRESULT FaceCredential::SetSelected(BOOL* pbAutoLogon)
{
    LogMsg(L"FaceCredential::SetSelected called (m_bVerified = %d)", m_bVerified);
    if (pbAutoLogon) *pbAutoLogon = m_bVerified;
    if (!m_bVerified)
    {
        StartVerification();
    }
    return S_OK;
}

HRESULT FaceCredential::SetDeselected()
{
    LogMsg(L"FaceCredential::SetDeselected called");
    m_bStopThread = TRUE;
    m_overlay.Hide();
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, 300);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    return S_OK;
}

HRESULT FaceCredential::GetFieldState(DWORD dwFieldID,
                                      CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
                                      CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis)
{
    if (!pcpfs || !pcpfis) return E_POINTER;

    switch (dwFieldID)
    {
    case FPF_LOGO:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        *pcpfis = CPFIS_NONE;
        break;
    case FPF_STATUS_TEXT:
        *pcpfs = CPFS_DISPLAY_IN_BOTH;
        *pcpfis = CPFIS_NONE;
        break;
    case FPF_SUBMIT_BUTTON:
        *pcpfs = m_bVerified ? CPFS_DISPLAY_IN_SELECTED_TILE : CPFS_HIDDEN;
        *pcpfis = CPFIS_FOCUSED;
        break;
    default:
        *pcpfs = CPFS_HIDDEN;
        *pcpfis = CPFIS_NONE;
        break;
    }
    return S_OK;
}

HRESULT FaceCredential::GetStringValue(DWORD dwFieldID, LPWSTR* ppsz)
{
    if (!ppsz) return E_POINTER;
    *ppsz = nullptr;

    if (dwFieldID == FPF_STATUS_TEXT)
    {
        EnterCriticalSection(&m_cs);
        HRESULT hr = SHStrDupW(m_statusText, ppsz);
        LeaveCriticalSection(&m_cs);
        return hr;
    }
    return E_INVALIDARG;
}

HRESULT FaceCredential::GetBitmapValue(DWORD dwFieldID, HBITMAP* phbmp)
{
    LogMsg(L"FaceCredential::GetBitmapValue called for fieldID=%d", dwFieldID);
    if (!phbmp) return E_POINTER;
    *phbmp = NULL;

    if (dwFieldID == FPF_LOGO)
    {
        *phbmp = LoadAssetBitmap(m_bVerified ? L"hello_wink.bmp" : L"hello_normal.bmp");
        return S_OK;
    }
    return S_OK;
}

HRESULT FaceCredential::GetBitmapBufferValue(DWORD dwFieldID, DWORD* pImageBufferSize, BYTE** ppImageBuffer)
{
    LogMsg(L"FaceCredential::GetBitmapBufferValue called for fieldID=%d", dwFieldID);
    if (!pImageBufferSize || !ppImageBuffer) return E_POINTER;
    *pImageBufferSize = 0;
    *ppImageBuffer = nullptr;

    if (dwFieldID == FPF_LOGO)
    {
        return GetAssetBitmapBuffer(m_bVerified ? L"hello_wink.bmp" : L"hello_normal.bmp", pImageBufferSize, ppImageBuffer);
    }
    return E_INVALIDARG;
}

HRESULT FaceCredential::GetCheckboxValue(DWORD dwFieldID, BOOL* pbChecked, LPWSTR* ppszLabel)
{
    return E_NOTIMPL;
}

HRESULT FaceCredential::GetSubmitButtonValue(DWORD dwFieldID, DWORD* pdwAdjacentTo)
{
    if (!pdwAdjacentTo) return E_POINTER;
    if (dwFieldID == FPF_SUBMIT_BUTTON)
    {
        *pdwAdjacentTo = FPF_STATUS_TEXT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT FaceCredential::GetComboBoxValueCount(DWORD dwFieldID, DWORD* pcItems, DWORD* pdwSelectedItem) { return E_NOTIMPL; }
HRESULT FaceCredential::GetComboBoxValueAt(DWORD dwFieldID, DWORD dwItem, LPWSTR* ppszItem) { return E_NOTIMPL; }
HRESULT FaceCredential::SetStringValue(DWORD dwFieldID, LPCWSTR psz) { return E_NOTIMPL; }
HRESULT FaceCredential::SetCheckboxValue(DWORD dwFieldID, BOOL bChecked) { return E_NOTIMPL; }
HRESULT FaceCredential::SetComboBoxSelectedValue(DWORD dwFieldID, DWORD dwSelectedItem) { return E_NOTIMPL; }
HRESULT FaceCredential::CommandLinkClicked(DWORD dwFieldID)
{
    LogMsg(L"CommandLinkClicked: fieldID=%d", dwFieldID);
    if (!m_bVerified)
    {
        EnterCriticalSection(&m_cs);
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Windows Hello: Looking for you...");
        m_overlay.SetState(OVERLAY_SCANNING);
        LeaveCriticalSection(&m_cs);
        if (m_pEvents)
        {
            m_pEvents->SetFieldString(this, FPF_STATUS_TEXT, m_statusText);
        }
        StartVerification();
    }
    return S_OK;
}

HRESULT FaceCredential::GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
                                        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
                                        LPWSTR* ppszOptionalStatusText,
                                        CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon)
{
    LogMsg(L"FaceCredential::GetSerialization called (m_bVerified=%d, scenario=%d)", m_bVerified, m_cpus);
    if (!pcpgsr || !pcpcs) return E_POINTER;

    // Both Lock Screen and CredUI scenarios use ambient biometric verification and
    // direct hardware-level PIN input (SimulatePinInput).
    // Always return CPGSR_NO_CREDENTIAL_FINISHED so LSA never receives an invalid NTLM buffer
    // and never throws "The username or password is incorrect".
    *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
    return S_OK;
}

HRESULT FaceCredential::ReportResult(NTSTATUS ntsStatus, NTSTATUS ntsSubstatus,
                                     LPWSTR* ppszOptionalStatusText,
                                     CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon)
{
    LogMsg(L"FaceCredential::ReportResult: status=0x%08X, substatus=0x%08X", ntsStatus, ntsSubstatus);
    return S_OK;
}

HRESULT FaceCredential::GetUserSid(LPWSTR* ppszSid)
{
    if (!ppszSid) return E_POINTER;
    if (m_pszUserSid)
    {
        return SHStrDupW(m_pszUserSid, ppszSid);
    }
    LPWSTR activeSid = QueryActiveSessionUserSid();
    if (activeSid)
    {
        *ppszSid = activeSid;
        return S_OK;
    }
    *ppszSid = nullptr;
    return S_OK;
}
