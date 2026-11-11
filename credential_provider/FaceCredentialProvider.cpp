#include "FaceCredentialProvider.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static void LogProvider(const WCHAR* fmt, ...)
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
        fwprintf(f, L"[%02d:%02d:%02d.%03d] [PROVIDER] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR s_rgFieldDescriptors[] =
{
    { FPF_LOGO,           CPFT_TILE_IMAGE,    const_cast<LPWSTR>(L"Windows Hello Face"), CPFG_CREDENTIAL_PROVIDER_LOGO },
    { FPF_STATUS_TEXT,    CPFT_LARGE_TEXT,    const_cast<LPWSTR>(L"Windows Hello"),      GUID_NULL },
    { FPF_SUBMIT_BUTTON,  CPFT_SUBMIT_BUTTON, const_cast<LPWSTR>(L"Unlock"),             GUID_NULL }
};

FaceCredentialProvider::FaceCredentialProvider() :
    m_cRef(1),
    m_cpus(CPUS_INVALID),
    m_pEvents(nullptr),
    m_upAdviseContext(0),
    m_pCredential(nullptr),
    m_pszUserSid(nullptr),
    m_bHasUsers(FALSE)
{
    EnsureGdiplus();
}

FaceCredentialProvider::~FaceCredentialProvider()
{
    CleanUpUsers();
    if (m_pCredential)
    {
        m_pCredential->Release();
        m_pCredential = nullptr;
    }
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = nullptr;
    }
    ReleaseGdiplus();
}

void FaceCredentialProvider::CleanUpUsers()
{
    if (m_pszUserSid)
    {
        CoTaskMemFree(m_pszUserSid);
        m_pszUserSid = nullptr;
    }
}

HRESULT FaceCredentialProvider::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_ICredentialProvider)
    {
        *ppv = static_cast<ICredentialProvider*>(this);
        AddRef();
        return S_OK;
    }
    else if (riid == IID_ICredentialProviderSetUserArray)
    {
        *ppv = static_cast<ICredentialProviderSetUserArray*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG FaceCredentialProvider::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

ULONG FaceCredentialProvider::Release()
{
    long cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

HRESULT FaceCredentialProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD dwFlags)
{
    LogProvider(L"SetUsageScenario: scenario=%d, flags=0x%08X", cpus, dwFlags);
    if (cpus == CPUS_LOGON || cpus == CPUS_UNLOCK_WORKSTATION || cpus == CPUS_CREDUI)
    {
        m_cpus = cpus;
        return S_OK;
    }
    return E_NOTIMPL;
}

HRESULT FaceCredentialProvider::SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs)
{
    return E_NOTIMPL;
}

HRESULT FaceCredentialProvider::Advise(ICredentialProviderEvents* pcpe, UINT_PTR upAdviseContext)
{
    LogProvider(L"Advise called");
    if (m_pEvents)
    {
        m_pEvents->Release();
    }
    m_pEvents = pcpe;
    m_upAdviseContext = upAdviseContext;
    if (m_pEvents)
    {
        m_pEvents->AddRef();
    }

    if (!m_pCredential)
    {
        m_pCredential = new FaceCredential();
        m_pCredential->Initialize(m_cpus, this, m_pszUserSid);
    }
    return S_OK;
}

HRESULT FaceCredentialProvider::UnAdvise()
{
    LogProvider(L"UnAdvise called");
    if (m_pCredential)
    {
        m_pCredential->UnAdvise();
    }
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = nullptr;
    }
    m_upAdviseContext = 0;
    return S_OK;
}

HRESULT FaceCredentialProvider::SetUserArray(ICredentialProviderUserArray* users)
{
    CleanUpUsers();
    m_bHasUsers = FALSE;

    if (!users)
    {
        LogProvider(L"SetUserArray: users is NULL");
        return S_OK;
    }

    DWORD count = 0;
    if (SUCCEEDED(users->GetCount(&count)) && count > 0)
    {
        LogProvider(L"SetUserArray: user count = %d", count);
        for (DWORD i = 0; i < count; i++)
        {
            ICredentialProviderUser* pUser = nullptr;
            if (SUCCEEDED(users->GetAt(i, &pUser)))
            {
                LPWSTR pszSid = nullptr;
                if (SUCCEEDED(pUser->GetSid(&pszSid)) && pszSid)
                {
                    LogProvider(L"SetUserArray: user[%d] SID = %s", i, pszSid);
                    if (!m_pszUserSid)
                    {
                        SHStrDupW(pszSid, &m_pszUserSid);
                        m_bHasUsers = TRUE;
                    }
                    CoTaskMemFree(pszSid);
                }
                pUser->Release();
                if (m_bHasUsers) break;
            }
        }
    }

    if (!m_pCredential)
    {
        m_pCredential = new FaceCredential();
        m_pCredential->Initialize(m_cpus, this, m_pszUserSid);
    }

    return S_OK;
}

HRESULT FaceCredentialProvider::GetFieldDescriptorCount(DWORD* pdwCount)
{
    if (!pdwCount) return E_POINTER;
    *pdwCount = ARRAYSIZE(s_rgFieldDescriptors);
    return S_OK;
}

HRESULT FaceCredentialProvider::GetFieldDescriptorAt(DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd)
{
    if (!ppcpfd) return E_POINTER;
    if (dwIndex >= ARRAYSIZE(s_rgFieldDescriptors)) return E_INVALIDARG;

    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcpfd = (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));
    if (!pcpfd) return E_OUTOFMEMORY;

    pcpfd->dwFieldID = s_rgFieldDescriptors[dwIndex].dwFieldID;
    pcpfd->cpft = s_rgFieldDescriptors[dwIndex].cpft;
    pcpfd->guidFieldType = s_rgFieldDescriptors[dwIndex].guidFieldType;
    HRESULT hr = SHStrDupW(s_rgFieldDescriptors[dwIndex].pszLabel, &pcpfd->pszLabel);
    if (FAILED(hr))
    {
        CoTaskMemFree(pcpfd);
        return hr;
    }

    *ppcpfd = pcpfd;
    return S_OK;
}

HRESULT FaceCredentialProvider::GetCredentialCount(DWORD* pdwCount, DWORD* pdwDefault, BOOL* pbAutoLogonWithDefault)
{
    if (!pdwCount || !pdwDefault || !pbAutoLogonWithDefault) return E_POINTER;

    if (m_cpus == CPUS_CREDUI)
    {
        // In CredUI (e.g. Edge, Chrome, UAC, App prompts):
        // Make Windows Hello Face visible directly inside the Windows Security dialog!
        *pdwCount = 1;
        *pdwDefault = 0;
        *pbAutoLogonWithDefault = FALSE;
        LogProvider(L"GetCredentialCount: CPUS_CREDUI scenario - returning count=1, default=0");
        return S_OK;
    }

    // Return 0 for Lock Screen so LogonUI does NOT show an extra tile under "Sign-in options".
    // Windows Hello Face acts as an ambient biometric overlay on top of the default Windows Hello PIN unlock interface!
    *pdwCount = 0;
    *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
    *pbAutoLogonWithDefault = FALSE;

    LogProvider(L"GetCredentialCount: lock screen scenario - returning count=0 (ambient overlay)");
    return S_OK;
}

HRESULT FaceCredentialProvider::GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential** ppcpc)
{
    if (!ppcpc) return E_POINTER;
    if (dwIndex != 0) return E_INVALIDARG;

    if (!m_pCredential)
    {
        m_pCredential = new FaceCredential();
        m_pCredential->Initialize(m_cpus, this, m_pszUserSid);
    }

    *ppcpc = m_pCredential;
    (*ppcpc)->AddRef();
    return S_OK;
}

void FaceCredentialProvider::OnFaceVerified()
{
    if (m_pEvents)
    {
        m_pEvents->CredentialsChanged(m_upAdviseContext);
    }
}
