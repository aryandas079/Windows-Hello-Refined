#pragma once

#include <windows.h>
#include <credentialprovider.h>
#include <ntsecapi.h>
#include <strsafe.h>
#include <initguid.h>
#include <shlguid.h>

// GUID for Windows Hello Refined Credential Provider:
// {C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}
DEFINE_GUID(CLSID_FaceCredentialProvider,
    0xc85b5f30, 0x5b1d, 0x4a4a, 0x8a, 0x87, 0x3e, 0x3f, 0x45, 0xe4, 0x12, 0xa8);

// Modern V3 Interface for Credential Provider (Windows 10 / 11)
// {64A5010E-4363-41F8-9738-19045C20DABC}
DEFINE_GUID(IID_ICredentialProviderCredential3,
    0x64a5010e, 0x4363, 0x41f8, 0x97, 0x38, 0x19, 0x04, 0x5c, 0x20, 0xda, 0xbc);

MIDL_INTERFACE("64A5010E-4363-41F8-9738-19045C20DABC")
ICredentialProviderCredential3 : public ICredentialProviderCredential2
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetBitmapBufferValue(
        DWORD dwFieldID,
        DWORD* pImageBufferSize,
        BYTE** ppImageBuffer) = 0;
};

enum FACE_PROVIDER_FIELDS
{
    FPF_LOGO = 0,
    FPF_STATUS_TEXT,
    FPF_SUBMIT_BUTTON,
    FPF_NUM_FIELDS
};
