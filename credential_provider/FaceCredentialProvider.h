#pragma once

#include "common.h"
#include "FaceCredential.h"

class FaceCredentialProvider : public ICredentialProvider,
                               public ICredentialProviderSetUserArray
{
public:
    FaceCredentialProvider();
    virtual ~FaceCredentialProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // ICredentialProvider
    IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD dwFlags);
    IFACEMETHODIMP SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs);
    IFACEMETHODIMP Advise(ICredentialProviderEvents* pcpe, UINT_PTR upAdviseContext);
    IFACEMETHODIMP UnAdvise();
    IFACEMETHODIMP GetFieldDescriptorCount(DWORD* pdwCount);
    IFACEMETHODIMP GetFieldDescriptorAt(DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd);
    IFACEMETHODIMP GetCredentialCount(DWORD* pdwCount, DWORD* pdwDefault, BOOL* pbAutoLogonWithDefault);
    IFACEMETHODIMP GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential** ppcpc);

    // ICredentialProviderSetUserArray (Modern V2 interface for Windows 10/11)
    IFACEMETHODIMP SetUserArray(ICredentialProviderUserArray* users);

    void OnFaceVerified();

private:
    void CleanUpUsers();

    long m_cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO m_cpus;
    ICredentialProviderEvents* m_pEvents;
    UINT_PTR m_upAdviseContext;
    FaceCredential* m_pCredential;
    LPWSTR m_pszUserSid;
    BOOL m_bHasUsers;
};
