#include "FaceCredential.h"
#include "FaceCredentialProvider.h"
#include <shlwapi.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <ntsecapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "secur32.lib")

extern HINSTANCE g_hInst;

static void LogMsg(const WCHAR* fmt, ...)
{
    WCHAR buf[1024];
    va_list va;
    va_start(va, fmt);
    StringCchVPrintfW(buf, ARRAYSIZE(buf), fmt, va);
    va_end(va);

    FILE* f = _wfopen(L"C:\\Windows\\Temp\\WindowsHelloRefined.log", L"a, ccs=UTF-8");
    if (f)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

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

    HANDLE hLsa = NULL;
    NTSTATUS status = LsaConnectUntrusted(&hLsa);
    if (status != 0)
    {
        LogMsg(L"LsaConnectUntrusted failed: 0x%08X", status);
        return E_FAIL;
    }

    LSA_STRING pkgName;
    pkgName.Buffer = (PCHAR)MSV1_0_PACKAGE_NAME;
    pkgName.Length = (USHORT)strlen(MSV1_0_PACKAGE_NAME);
    pkgName.MaximumLength = pkgName.Length + 1;

    status = LsaLookupAuthenticationPackage(hLsa, &pkgName, pulAuthPackage);
    LsaDeregisterLogonProcess(hLsa);
    if (status != 0)
    {
        LogMsg(L"LsaLookupAuthenticationPackage failed: 0x%08X", status);
        return E_FAIL;
    }

    DWORD cbDomain = (DWORD)wcslen(pszDomain) * sizeof(WCHAR);
    DWORD cbUser = (DWORD)wcslen(pszUsername) * sizeof(WCHAR);
    DWORD cbPassword = (DWORD)wcslen(pszPassword) * sizeof(WCHAR);

    DWORD cbTotal = sizeof(MSV1_0_INTERACTIVE_LOGON) + cbDomain + cbUser + cbPassword;
    BYTE* pBuffer = (BYTE*)CoTaskMemAlloc(cbTotal);
    if (!pBuffer) return E_OUTOFMEMORY;
    ZeroMemory(pBuffer, cbTotal);

    PMSV1_0_INTERACTIVE_LOGON pLogon = (PMSV1_0_INTERACTIVE_LOGON)pBuffer;
    pLogon->MessageType = MsV1_0InteractiveLogon;

    BYTE* pCursor = pBuffer + sizeof(MSV1_0_INTERACTIVE_LOGON);

    // Domain
    pLogon->LogonDomainName.Length = (USHORT)cbDomain;
    pLogon->LogonDomainName.MaximumLength = (USHORT)cbDomain;
    pLogon->LogonDomainName.Buffer = (PWSTR)pCursor;
    CopyMemory(pCursor, pszDomain, cbDomain);
    pCursor += cbDomain;

    // User
    pLogon->UserName.Length = (USHORT)cbUser;
    pLogon->UserName.MaximumLength = (USHORT)cbUser;
    pLogon->UserName.Buffer = (PWSTR)pCursor;
    CopyMemory(pCursor, pszUsername, cbUser);
    pCursor += cbUser;

    // Password
    pLogon->Password.Length = (USHORT)cbPassword;
    pLogon->Password.MaximumLength = (USHORT)cbPassword;
    pLogon->Password.Buffer = (PWSTR)pCursor;
    CopyMemory(pCursor, pszPassword, cbPassword);

    *ppbSerialization = pBuffer;
    *pcbSerialization = cbTotal;
    LogMsg(L"Credentials packed successfully for %s\\%s (pkg: %d, size: %d)", pszDomain, pszUsername, *pulAuthPackage, cbTotal);
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
    if (!m_hThread && !m_bVerified)
    {
        m_bStopThread = FALSE;
        LogMsg(L"Starting verification thread...");
        m_overlay.Show(m_username);
        m_hThread = CreateThread(NULL, 0, VerificationThread, this, 0, NULL);
    }
}

DWORD WINAPI FaceCredential::VerificationThread(LPVOID lpParam)
{
    FaceCredential* pThis = reinterpret_cast<FaceCredential*>(lpParam);
    pThis->DoVerification();
    return 0;
}

static HBITMAP CreatePARGBDIB(int width, int height, void** ppvBits)
{
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    return CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, ppvBits, NULL, 0);
}

static HBITMAP LoadAssetBitmap(LPCWSTR filename)
{
    const int iconSize = 128;
    WCHAR pngName[64];
    StringCchCopyW(pngName, ARRAYSIZE(pngName), filename);
    // Replace .bmp extension with .png for high-fidelity alpha
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
        // Try original filename (.bmp) if .png was not found
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
            // PixelFormat32bppPARGB preserves true 100% transparent alpha (alpha = 0)
            if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bmpData) == Gdiplus::Ok)
            {
                memcpy(pBits, bmpData.Scan0, w * h * 4);
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
            memcpy(pBits, bmpData.Scan0, iconSize * iconSize * 4);
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

void FaceCredential::DoVerification()
{
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

        Sleep(200);
        retry++;
    }

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        LogMsg(L"Pipe connection failed after retries. Error: %d", err);
        EnterCriticalSection(&m_cs);
        StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Windows Hello service unreachable. Use PIN/Password.");
        m_overlay.SetState(OVERLAY_FAILED, L"Service unreachable. Use PIN.");
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

            EnterCriticalSection(&m_cs);
            m_bVerified = TRUE;
            m_hasPassword = (wcslen(m_password) > 0);
            StringCchPrintfW(m_statusText, ARRAYSIZE(m_statusText), L"Welcome back, %s!", m_username);
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

            if (m_pProvider)
            {
                LogMsg(L"Triggering CredentialsChanged for automatic unlock...");
                m_pProvider->OnFaceVerified();
            }
        }
        else
        {
            LogMsg(L"Face verification failed or timed out.");
            EnterCriticalSection(&m_cs);
            StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Couldn't recognize face. Click to retry or use PIN.");
            m_overlay.SetState(OVERLAY_FAILED, L"Couldn't recognize face. Use PIN.");
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
    StartVerification();
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
    if (!phbmp) return E_POINTER;
    *phbmp = NULL;

    if (dwFieldID == FPF_LOGO)
    {
        *phbmp = LoadAssetBitmap(m_bVerified ? L"hello_wink.bmp" : L"hello_normal.bmp");
        return S_OK;
    }
    return S_OK;
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
HRESULT FaceCredential::CommandLinkClicked(DWORD dwFieldID) { return E_NOTIMPL; }

HRESULT FaceCredential::GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
                                        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
                                        LPWSTR* ppszOptionalStatusText,
                                        CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon)
{
    LogMsg(L"FaceCredential::GetSerialization called (m_bVerified=%d, m_hasPassword=%d)", m_bVerified, m_hasPassword);
    if (!pcpgsr || !pcpcs) return E_POINTER;

    if (m_bVerified)
    {
        if (m_hasPassword)
        {
            ULONG ulPackage = 0;
            BYTE* pBuffer = NULL;
            DWORD cbBuffer = 0;
            HRESULT hr = PackLogonCredentials(m_domain, m_username, m_password, &ulPackage, &pBuffer, &cbBuffer);
            if (SUCCEEDED(hr))
            {
                *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
                pcpcs->ulAuthenticationPackage = ulPackage;
                pcpcs->cbSerialization = cbBuffer;
                pcpcs->rgbSerialization = pBuffer;
                LogMsg(L"GetSerialization: Returned credentials buffer for %s! Unlocking workstation.", m_username);
                return S_OK;
            }
        }

        // If no password stored, return finished with 0
        *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
        pcpcs->ulAuthenticationPackage = 0;
        pcpcs->cbSerialization = 0;
        pcpcs->rgbSerialization = NULL;
        return S_OK;
    }

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
