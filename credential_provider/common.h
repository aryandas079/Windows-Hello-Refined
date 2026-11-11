#pragma once

#include <windows.h>
#include <credentialprovider.h>
#include <ntsecapi.h>
#include <strsafe.h>
#include <initguid.h>

// GUID for Windows Hello Refined Credential Provider:
// {C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}
DEFINE_GUID(CLSID_FaceCredentialProvider,
    0xc85b5f30, 0x5b1d, 0x4a4a, 0x8a, 0x87, 0x3e, 0x3f, 0x45, 0xe4, 0x12, 0xa8);

enum FACE_PROVIDER_FIELDS
{
    FPF_LOGO = 0,
    FPF_STATUS_TEXT,
    FPF_SUBMIT_BUTTON,
    FPF_NUM_FIELDS
};
