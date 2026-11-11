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

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
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
