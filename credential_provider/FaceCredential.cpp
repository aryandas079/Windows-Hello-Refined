#include "FaceCredential.h"
#include "FaceCredentialProvider.h"
#include <shlwapi.h>
#include <wtsapi32.h>
#include <sddl.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wtsapi32.lib")

extern HINSTANCE g_hInst;

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

FaceCredential::FaceCredential() :
    m_cRef(1),
    m_cpus(CPUS_INVALID),
    m_pProvider(nullptr),
    m_pEvents(nullptr),
    m_pszUserSid(nullptr),
    m_bVerified(FALSE),
    m_hThread(nullptr),
    m_bStopThread(FALSE)
{
    InitializeCriticalSection(&m_cs);
    StringCchCopyW(m_username, ARRAYSIZE(m_username), L"Aryan");
    StringCchCopyW(m_statusText, ARRAYSIZE(m_statusText), L"Windows Hello: Looking for you...");
}

FaceCredential::~FaceCredential()
{
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
    }
    else
    {
        m_pszUserSid = QueryActiveSessionUserSid();
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

    // Auto-start scanning on lock screen
    StartVerification();
    return S_OK;
}

HRESULT FaceCredential::UnAdvise()
{
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
        // Show smooth native transparent overlay on the Winlogon desktop
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

static HBITMAP LoadAssetBitmap(LPCWSTR filename)
{
    WCHAR bmpPath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, bmpPath, ARRAYSIZE(bmpPath));
    PathRemoveFileSpecW(bmpPath);
    PathAppendW(bmpPath, L"..\\assets");
    PathAppendW(bmpPath, filename);

    return (HBITMAP)LoadImageW(NULL, bmpPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

void FaceCredential::DoVerification()
{
    const WCHAR* pipeName = L"\\\\.\\pipe\\WindowsHelloRefinedPipe";

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

    // Send "VERIFY" command to face_service
    const char cmd[] = "VERIFY\n";
    DWORD bytesWritten = 0;
    WriteFile(hPipe, cmd, (DWORD)strlen(cmd), &bytesWritten, NULL);

    // Read verification response
    char buffer[1024] = {0};
    DWORD bytesRead = 0;
    if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
    {
        buffer[bytesRead] = '\0';
        if (strstr(buffer, "\"status\": \"verified\""))
        {
            // Extract username if provided
            const char* userKey = "\"username\": \"";
            const char* pUser = strstr(buffer, userKey);
            if (pUser)
            {
                pUser += strlen(userKey);
                char uname[64] = {0};
                int idx = 0;
                while (*pUser && *pUser != '\"' && idx < 63)
                {
                    uname[idx++] = *pUser++;
                }
                MultiByteToWideChar(CP_UTF8, 0, uname, -1, m_username, ARRAYSIZE(m_username));
            }

            EnterCriticalSection(&m_cs);
            m_bVerified = TRUE;
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
                m_pProvider->OnFaceVerified();
            }
        }
        else
        {
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

    CloseHandle(hPipe);
}

HRESULT FaceCredential::SetSelected(BOOL* pbAutoLogon)
{
    if (pbAutoLogon) *pbAutoLogon = m_bVerified;
    StartVerification();
    return S_OK;
}

HRESULT FaceCredential::SetDeselected()
{
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
        // Visible in BOTH the selected tile and the "Sign-in options" list alongside PIN!
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
    if (!pcpgsr || !pcpcs) return E_POINTER;

    if (m_bVerified)
    {
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
    return S_OK;
}

HRESULT FaceCredential::GetUserSid(LPWSTR* ppszSid)
{
    if (!ppszSid) return E_POINTER;
    if (m_pszUserSid)
    {
        return SHStrDupW(m_pszUserSid, ppszSid);
    }
    // Fall back to active console user SID
    LPWSTR activeSid = QueryActiveSessionUserSid();
    if (activeSid)
    {
        *ppszSid = activeSid;
        return S_OK;
    }
    *ppszSid = nullptr;
    return S_OK;
}
