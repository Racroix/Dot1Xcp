#include "pch.h"
#include <windows.h>
#include <credentialprovider.h>
#include <new>
#include <olectl.h>
#include "SimpleCpGuids.h"
#include "ClassFactory.h"

HINSTANCE g_hInst = nullptr;
LONG g_LockCount = 0;

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == CLSID_testCPProvider) {
        auto* cf = new(std::nothrow) ClassFactory();
        if (!cf) return E_OUTOFMEMORY;
        HRESULT hr = cf->QueryInterface(riid, ppv);
        cf->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow(void) {
    return (g_LockCount == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer(void) {
    wchar_t modulePath[MAX_PATH]{};
    if (!GetModuleFileNameW(g_hInst, modulePath, ARRAYSIZE(modulePath))) return SELFREG_E_CLASS;

    // 1) HKCR\CLSID\{GUID}\InprocServer32
    const wchar_t* clsidKey = L"CLSID\\{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}\\InprocServer32";
    HKEY hInproc{};
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hInproc, nullptr) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    // (Default) = full path
    RegSetValueExW(hInproc, nullptr, 0, REG_SZ, (const BYTE*)modulePath, (DWORD)((wcslen(modulePath)+1)*sizeof(wchar_t)));
    // ThreadingModel = Apartment
    const wchar_t* tm = L"Apartment";
    RegSetValueExW(hInproc, L"ThreadingModel", 0, REG_SZ, (const BYTE*)tm, (DWORD)((wcslen(tm)+1)*sizeof(wchar_t)));
    RegCloseKey(hInproc);

    // 2) HKLM\...\Authentication\Credential Providers\{GUID}
    HKEY hKey{};
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}",
        0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;

    const wchar_t* name = L"Dot1xCP";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)name, (DWORD)((wcslen(name)+1)*sizeof(wchar_t)));
    RegCloseKey(hKey);
    return S_OK;
}

extern "C" HRESULT __stdcall DllUnregisterServer(void) {
    RegDeleteKeyW(HKEY_CLASSES_ROOT, L"CLSID\\{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}\\InprocServer32");
    RegDeleteKeyW(HKEY_CLASSES_ROOT, L"CLSID\\{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}");
    return S_OK;
}

