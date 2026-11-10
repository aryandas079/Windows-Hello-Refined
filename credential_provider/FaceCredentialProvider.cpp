#include "FaceCredentialProvider.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR s_rgFieldDescriptors[] =
{
    { FPF_LOGO,           CPFT_TILE_IMAGE,    const_cast<LPWSTR>(L"Logo") },
    { FPF_STATUS_TEXT,    CPFT_LARGE_TEXT,    const_cast<LPWSTR>(L"Status") },
    { FPF_SUBMIT_BUTTON,  CPFT_SUBMIT_BUTTON, const_cast<LPWSTR>(L"Unlock") }
};

FaceCredentialProvider::FaceCredentialProvider() :
    m_cRef(1),
    m_cpus(CPUS_INVALID),
    m_pEvents(nullptr),
    m_upAdviseContext(0),
    m_pCredential(nullptr)
{
}

FaceCredentialProvider::~FaceCredentialProvider()
{
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
    return S_OK;
}

HRESULT FaceCredentialProvider::UnAdvise()
{
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = nullptr;
    }
    m_upAdviseContext = 0;
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

    *pdwCount = 1;
    *pdwDefault = 0;
    *pbAutoLogonWithDefault = FALSE;
    return S_OK;
}

HRESULT FaceCredentialProvider::GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential** ppcpc)
{
    if (!ppcpc) return E_POINTER;
    if (dwIndex != 0) return E_INVALIDARG;

    if (!m_pCredential)
    {
        m_pCredential = new FaceCredential();
        m_pCredential->Initialize(m_cpus, this);
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
