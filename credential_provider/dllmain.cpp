#include "FaceCredentialProvider.h"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

HINSTANCE g_hInst = NULL;
long g_cDllRef = 0;
static ULONG_PTR g_gdiplusToken = 0;
static long g_gdiRefCount = 0;

void EnsureGdiplus()
{
    if (InterlockedIncrement(&g_gdiRefCount) == 1)
    {
        Gdiplus::GdiplusStartupInput gdiInput;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL);
    }
}

void ReleaseGdiplus()
{
    if (InterlockedDecrement(&g_gdiRefCount) == 0)
    {
        if (g_gdiplusToken)
        {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
    }
}

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_cRef(1) {}
    virtual ~CClassFactory() {}

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        long cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
    {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        FaceCredentialProvider* pProvider = new FaceCredentialProvider();
        if (!pProvider) return E_OUTOFMEMORY;
        HRESULT hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock) InterlockedIncrement(&g_cDllRef);
        else InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    long m_cRef;
};

static void CleanAllLockScreenPolicies()
{
    // 1. Remove lock screen and slideshow blocking policies in HKLM Personalization
    HKEY hPers = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Personalization", 0, KEY_SET_VALUE, &hPers) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hPers, L"NoLockScreen");
        RegDeleteValueW(hPers, L"NoLockScreenSlideshow");
        RegDeleteValueW(hPers, L"NoLockScreenCamera");
        RegCloseKey(hPers);
    }

    // 2. Remove Windows Spotlight blocking policies in HKLM CloudContent
    HKEY hCloud = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent", 0, KEY_SET_VALUE, &hCloud) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hCloud, L"DisableWindowsSpotlightFeatures");
        RegDeleteValueW(hCloud, L"ConfigureWindowsSpotlight");
        RegDeleteValueW(hCloud, L"DisableWindowsConsumerFeatures");
        RegDeleteValueW(hCloud, L"DisableSoftLanding");
        RegDeleteValueW(hCloud, L"DisableThirdPartySuggestions");
        RegCloseKey(hCloud);
    }

    // 3. Clean user hives in HKEY_USERS
    HKEY hUsers = NULL;
    if (RegOpenKeyExW(HKEY_USERS, NULL, 0, KEY_READ, &hUsers) == ERROR_SUCCESS)
    {
        WCHAR subkeyName[256];
        DWORD idx = 0;
        DWORD nameLen = ARRAYSIZE(subkeyName);
        while (RegEnumKeyExW(hUsers, idx++, subkeyName, &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            if (wcsstr(subkeyName, L"_Classes") == NULL)
            {
                WCHAR userCloudPath[512];
                StringCchPrintfW(userCloudPath, ARRAYSIZE(userCloudPath), L"%s\\Software\\Policies\\Microsoft\\Windows\\CloudContent", subkeyName);
                HKEY hUserCloud = NULL;
                if (RegOpenKeyExW(HKEY_USERS, userCloudPath, 0, KEY_SET_VALUE, &hUserCloud) == ERROR_SUCCESS)
                {
                    RegDeleteValueW(hUserCloud, L"DisableWindowsSpotlightFeatures");
                    RegDeleteValueW(hUserCloud, L"ConfigureWindowsSpotlight");
                    RegDeleteValueW(hUserCloud, L"DisableSoftLanding");
                    RegDeleteValueW(hUserCloud, L"DisableThirdPartySuggestions");
                    RegCloseKey(hUserCloud);
                }

                WCHAR userCdmPath[512];
                StringCchPrintfW(userCdmPath, ARRAYSIZE(userCdmPath), L"%s\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", subkeyName);
                HKEY hUserCdm = NULL;
                if (RegOpenKeyExW(HKEY_USERS, userCdmPath, 0, KEY_SET_VALUE, &hUserCdm) == ERROR_SUCCESS)
                {
                    DWORD one = 1;
                    RegSetValueExW(hUserCdm, L"RotatingLockScreenEnabled", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
                    RegSetValueExW(hUserCdm, L"ContentDeliveryAllowed", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
                    RegCloseKey(hUserCdm);
                }
            }
            nameLen = ARRAYSIZE(subkeyName);
        }
        RegCloseKey(hUsers);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        CleanAllLockScreenPolicies();
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid == CLSID_FaceCredentialProvider)
    {
        CClassFactory* pFactory = new CClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllRegisterServer()
{
    HKEY hKey;
    const WCHAR* clsidStr = L"{C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}";
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path));

    WCHAR subKey[256];
    StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"CLSID\\%s\\InprocServer32", clsidStr);

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)path, (DWORD)(wcslen(path) + 1) * sizeof(WCHAR));
        const WCHAR* threadingModel = L"Apartment";
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)threadingModel, (DWORD)(wcslen(threadingModel) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }

    StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\%s", clsidStr);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        const WCHAR* name = L"WindowsHelloRefined";
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)name, (DWORD)(wcslen(name) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }

    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const WCHAR* clsidStr = L"{C85B5F30-5B1D-4A4A-8A87-3E3F45E412A8}";
    WCHAR subKey[256];

    StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\%s", clsidStr);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, subKey);

    StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"CLSID\\%s\\InprocServer32", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, subKey);
    StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"CLSID\\%s", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, subKey);

    return S_OK;
}
