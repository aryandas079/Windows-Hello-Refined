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

static bool IsCredUIDialogOpen();
static void SimulatePinInput(LPCWSTR pszPin, bool bIsCredUI = false);
static void FastSendKey(WORD vk, bool bExtended = false);

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
    m_bVerified = FALSE;
    m_overlay.Hide();
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, 300);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
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
    m_bVerified = FALSE;
    m_bStopThread = FALSE;

    if (m_hThread)
    {
        DWORD exitCode = 0;
        if (GetExitCodeThread(m_hThread, &exitCode) && exitCode != STILL_ACTIVE)
        {
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
    }

    if (!m_hThread)
    {
        LogMsg(L"Starting verification thread (scenario=%d)...", m_cpus);
        m_overlay.Show(m_username, (int)m_cpus);
        m_hThread = CreateThread(NULL, 0, VerificationThread, this, 0, NULL);
    }
    else
    {
        LogMsg(L"StartVerification: thread already active, re-arming overlay...");
        m_overlay.Show(m_username, (int)m_cpus);
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

static void FastSendKey(WORD vk, bool bExtended)
{
    BYTE scan = (BYTE)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    DWORD flags = bExtended ? KEYEVENTF_EXTENDEDKEY : 0;
    keybd_event((BYTE)vk, scan, flags, 0);
    Sleep(2);
    keybd_event((BYTE)vk, scan, flags | KEYEVENTF_KEYUP, 0);
    Sleep(2);
}

static void FastSendChar(WCHAR ch)
{
    SHORT vkState = VkKeyScanW(ch);
    if (vkState != -1)
    {
        BYTE vk = LOBYTE(vkState);
        BYTE shiftState = HIBYTE(vkState);
        bool bShift = (shiftState & 1) != 0;
        BYTE scan = (BYTE)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

        if (bShift)
        {
            keybd_event(VK_SHIFT, (BYTE)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC), 0, 0);
        }
        keybd_event(vk, scan, 0, 0);
        keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
        if (bShift)
        {
            keybd_event(VK_SHIFT, (BYTE)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP, 0);
        }
    }
    else
    {
        keybd_event(0, (BYTE)(ch & 0xFF), KEYEVENTF_UNICODE, 0);
        keybd_event(0, (BYTE)(ch & 0xFF), KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0);
    }
}

static void ClearPinField()
{
    BYTE controlScan = (BYTE)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
    keybd_event(VK_CONTROL, controlScan, 0, 0);
    FastSendKey('A');
    keybd_event(VK_CONTROL, controlScan, KEYEVENTF_KEYUP, 0);
    FastSendKey(VK_BACK);

    for (int i = 0; i < 10; i++)
    {
        FastSendKey(VK_BACK);
    }
}

static bool SendPinAtomically(LPCWSTR pin)
{
    if (!pin) return false;

    size_t length = wcslen(pin);
    if (length == 0 || length > 120) return false;

    INPUT inputs[243] = {};
    UINT count = 0;
    for (size_t i = 0; i < length; i++)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = pin[i];
        inputs[count].ki.dwFlags = KEYEVENTF_UNICODE;
        count++;

        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = pin[i];
        inputs[count].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        count++;
    }

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = VK_RETURN;
    inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
    count++;

    UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (sent != count)
    {
        LogMsg(L"SendPinAtomically: only sent %u of %u input events (err=%lu)", sent, count, GetLastError());
        return false;
    }

    return true;
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
    if (hwndDlg && IsWindowVisible(hwndDlg))
    {
        RECT rc = {0};
        GetWindowRect(hwndDlg, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w < 1000 && h < 800 && w > 100 && h > 100)
        {
            return true;
        }
    }
    return false;
}

struct PinSimParams
{
    WCHAR pin[128];
    bool bIsCredUI;
};

static DWORD WINAPI PinSimulationThread(LPVOID lpParam)
{
    PinSimParams* params = (PinSimParams*)lpParam;
    if (!params) return 0;

    LogMsg(L"PinSimulationThread: started for %d characters (bIsCredUI=%d)...", (int)wcslen(params->pin), params->bIsCredUI);

    HDESK hInputDesk = NULL;
    if (!params->bIsCredUI)
    {
        // On a pristine thread with no message queue or windows, attach to the active input desktop
        hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!hInputDesk)
        {
            hInputDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP | DESKTOP_HOOKCONTROL);
        }
        if (!hInputDesk)
        {
            hInputDesk = OpenDesktopW(L"Winlogon", 0, FALSE, GENERIC_ALL);
        }
        if (hInputDesk)
        {
            BOOL bSet = SetThreadDesktop(hInputDesk);
            LogMsg(L"PinSimulationThread: SetThreadDesktop result=%d, err=%lu, hDesk=%p", bSet, GetLastError(), hInputDesk);
        }
        else
        {
            LogMsg(L"PinSimulationThread: OpenInputDesktop failed, err=%lu", GetLastError());
        }
    }

    if (params->bIsCredUI)
    {
        // CredUI scenario: short smooth delay so the welcome smile renders before typing PIN (<0.5s total unlock)
        LogMsg(L"PinSimulationThread: swift CredUI PIN submission...");
        Sleep(120);

        // Ensure CredUI dialog window has focus
        HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
        if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
        if (!hwndDlg || !IsWindowVisible(hwndDlg))
        {
            LogMsg(L"PinSimulationThread: CredUI dialog closed before typing.");
            delete params;
            return 0;
        }

        SetForegroundWindow(hwndDlg);
        Sleep(20);
    }
    else
    {
        // Lock screen scenario: the lock screen curtain is lifted cleanly by the service with VK_UP,
        // so NO space or dot was typed into the PIN box. The field is clean and focused.
        LogMsg(L"PinSimulationThread: preparing clean lock-screen PIN field...");
        for (int i = 0; i < 4; i++)
        {
            FastSendKey(VK_BACK);
        }
        Sleep(20);
    }

    // Step 2: Type the complete PIN once with a rapid, crisp cadence.
    LogMsg(L"PinSimulationThread: typing %d characters swiftly...", (int)wcslen(params->pin));
    for (size_t i = 0; i < wcslen(params->pin); i++)
    {
        WCHAR ch = params->pin[i];
        if (ch >= L'0' && ch <= L'9')
        {
            WORD vk = (WORD)('0' + (ch - L'0'));
            BYTE scan = (BYTE)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            keybd_event((BYTE)vk, scan, 0, 0);
            Sleep(8);
            keybd_event((BYTE)vk, scan, KEYEVENTF_KEYUP, 0);
            Sleep(8);
        }
        else
        {
            FastSendChar(ch);
            Sleep(10);
        }
    }

    Sleep(30);
    BYTE enterScan = (BYTE)MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
    keybd_event(VK_RETURN, enterScan, 0, 0);
    Sleep(10);
    keybd_event(VK_RETURN, enterScan, KEYEVENTF_KEYUP, 0);
    LogMsg(L"PinSimulationThread: credentials entered and submitted.");

    if (hInputDesk)
    {
        CloseDesktop(hInputDesk);
    }

    delete params;
    return 0;
}

static void SimulatePinInput(LPCWSTR pszPin, bool bIsCredUI)
{
    if (!pszPin || wcslen(pszPin) == 0) return;

    LogMsg(L"SimulatePinInput: spawning dedicated worker thread for %d characters (bIsCredUI=%d)...", (int)wcslen(pszPin), bIsCredUI);
    PinSimParams* params = new PinSimParams();
    StringCchCopyW(params->pin, ARRAYSIZE(params->pin), pszPin);
    params->bIsCredUI = bIsCredUI;

    HANDLE hThread = CreateThread(NULL, 0, PinSimulationThread, params, 0, NULL);
    if (hThread)
    {
        WaitForSingleObject(hThread, 3500);
        CloseHandle(hThread);
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
    LogMsg(L"DoVerification: entering continuous wake/presence verification loop (scenario=%d)...", m_cpus);

    POINT lastCursor = {0, 0};
    GetCursorPos(&lastCursor);
    bool credUiFieldCleared = false;

    const WCHAR* pipeName = L"\\\\.\\pipe\\WindowsHelloRefinedPipe";

    while (!m_bStopThread && !m_bVerified)
    {
        bool bIsCredUI = (m_cpus == CPUS_CREDUI || (int)m_cpus == 3 || (int)m_cpus == 4 || IsCredUIDialogOpen());

        if (bIsCredUI && !IsCredUIDialogOpen())
        {
            // CredUI dialog closed
            Sleep(80);
            continue;
        }

        if (bIsCredUI && !credUiFieldCleared)
        {
            credUiFieldCleared = true;
            HWND hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
            if (!hwndDlg) hwndDlg = FindWindowW(NULL, L"Windows Security");
            if (!hwndDlg) hwndDlg = FindWindowW(L"Credential Dialog Xaml Host", NULL);
            if (hwndDlg && IsWindowVisible(hwndDlg))
            {
                SetForegroundWindow(hwndDlg);
                Sleep(20);
                for (int i = 0; i < 15; i++)
                {
                    FastSendKey(VK_BACK);
                }
                LogMsg(L"DoVerification: cleared CredUI PIN field via Backspace before face verification.");
            }
        }

        // Show scanning animation
        EnterCriticalSection(&m_cs);
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Looking for you...");
        m_overlay.SetState(OVERLAY_SCANNING, L"Looking for you...");
        LeaveCriticalSection(&m_cs);

        HANDLE hPipe = INVALID_HANDLE_VALUE;
        int retry = 0;
        while (!m_bStopThread && retry < 20)
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
                WaitNamedPipeW(pipeName, 500);
            }
            else
            {
                Sleep(80);
            }
            retry++;
        }

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            LogMsg(L"DoVerification: could not connect to pipe (err=%lu). Retrying...", GetLastError());
            Sleep(500);
            continue;
        }

        LogMsg(L"DoVerification: pipe connected. Sending VERIFY command...");
        const char cmd[] = "VERIFY\n";
        DWORD bytesWritten = 0;
        WriteFile(hPipe, cmd, (DWORD)strlen(cmd), &bytesWritten, NULL);

        char buffer[2048] = {0};
        DWORD bytesRead = 0;
        BOOL bRead = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
        CloseHandle(hPipe);

        if (bRead && bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            LogMsg(L"DoVerification: pipe response received (%d bytes)", bytesRead);

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
                StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Welcome, %s", m_username);
                m_overlay.SetState(OVERLAY_VERIFIED, m_statusText);
                LeaveCriticalSection(&m_cs);

                HWND hwndDlgCheck = FindWindowW(L"Credential Dialog Xaml Host", L"Windows Security");
                if (!hwndDlgCheck) hwndDlgCheck = FindWindowW(NULL, L"Windows Security");
                if (!hwndDlgCheck) hwndDlgCheck = FindWindowW(L"Credential Dialog Xaml Host", NULL);
                bool bLockScreenScenario =
                    m_cpus == CPUS_UNLOCK_WORKSTATION ||
                    m_cpus == CPUS_LOGON ||
                    (int)m_cpus == 0 ||
                    (int)m_cpus == 1;
                bool bIsCredUI = !bLockScreenScenario &&
                                 ((hwndDlgCheck != NULL && IsWindowVisible(hwndDlgCheck)) ||
                                  m_cpus == CPUS_CREDUI ||
                                  (int)m_cpus == 3 ||
                                  (int)m_cpus == 4);

                if (m_pProvider && !bIsCredUI)
                {
                    LogMsg(L"Face verified! Triggering CredentialsChanged for lock screen...");
                    m_pProvider->OnFaceVerified();
                }

                LPCWSTR secret = (wcslen(m_pin) > 0) ? m_pin : m_password;
                if (wcslen(secret) > 0)
                {
                    LogMsg(L"Face verified! Triggering instant password/PIN auto-fill (%d characters, bIsCredUI=%d)...",
                           (int)wcslen(secret), bIsCredUI);
                    SimulatePinInput(secret, bIsCredUI);

                    // Monitor the result without sending a second PIN sequence.
                    LogMsg(L"Starting post-verification monitor (bIsCredUI=%d)...", bIsCredUI);
                    DWORD startTime = GetTickCount();
                    bool lockScreenRetrySent = false;

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
                            m_overlay.Hide(true);
                            break;
                        }

                        if (!bIsCredUI && !lockScreenRetrySent &&
                            (GetTickCount() - startTime >= 1400) &&
                            IsLockScreenDesktop())
                        {
                            // LogonUI can consume the first wake pulse while
                            // transitioning to the PIN field. Retry once only
                            // while the workstation is still locked.
                            lockScreenRetrySent = true;
                            LogMsg(L"Monitor: lock screen still active; retrying PIN fill once.");
                            SimulatePinInput(secret, false);
                        }

                    }
                }
                break; // Verification complete and unlocked!
            }
            else
            {
                LogMsg(L"Face verification round completed: no face or match. Camera is idle. Entering wake standby...");
            }
        }

        if (m_bStopThread || m_bVerified) break;

        // ── LOW-POWER STANDBY MONITOR ──
        // The camera is completely OFF (LED is off).
        // Monitors user activity (mouse move, mouse click, keyboard key, spacebar) or periodic 3.0s re-scan.
        GetCursorPos(&lastCursor);
        DWORD standbyStart = GetTickCount();

        while (!m_bStopThread && !m_bVerified)
        {
            Sleep(30);
            if (m_bStopThread) break;

            if (bIsCredUI && !IsCredUIDialogOpen())
            {
                break;
            }

            POINT ptNow = {0, 0};
            GetCursorPos(&ptNow);
            bool mouseMoved = (abs(ptNow.x - lastCursor.x) > 3 || abs(ptNow.y - lastCursor.y) > 3);
            if (mouseMoved) lastCursor = ptNow;

            bool mouseClicked = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
                                ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);

            bool keyPressed = ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_RETURN) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_BACK) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0);

            DWORD elapsedStandby = GetTickCount() - standbyStart;
            bool periodicWake = (elapsedStandby >= 5000); // Check for user every 5.0s and pulse camera LED

            if (mouseMoved || mouseClicked || keyPressed || periodicWake)
            {
                LogMsg(L"Wake trigger (mouseMove=%d, click=%d, key=%d, periodic=%d). Resuming face verification immediately!",
                       mouseMoved, mouseClicked, keyPressed, periodicWake);
                break; // Break standby loop to start next verification attempt immediately!
            }
        }
    }

    LogMsg(L"DoVerification exited (m_bVerified=%d, m_bStopThread=%d)", m_bVerified, m_bStopThread);
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
        // Return blank string so Windows CredUI/LogonUI does not render native text
        // blocks underneath our custom FaceOverlay!
        return SHStrDupW(L" ", ppsz);
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
            m_pEvents->SetFieldString(this, FPF_STATUS_TEXT, L" ");
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
