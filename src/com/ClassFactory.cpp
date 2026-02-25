#include "pch.h"
#include "ClassFactory.h"
#include "Provider.h"
#include <new>

extern LONG g_LockCount;

ClassFactory::ClassFactory() : _ref(1) {
    InterlockedIncrement(&g_LockCount);
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG ClassFactory::AddRef() { return InterlockedIncrement(&_ref); }

ULONG ClassFactory::Release() {
    ULONG c = InterlockedDecrement(&_ref);
    if (!c) {
        InterlockedDecrement(&g_LockCount);
        delete this;
    }
    return c;
}

HRESULT ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    auto* prov = new(std::nothrow) testCPProvider();
    if (!prov) return E_OUTOFMEMORY;

    HRESULT hr = prov->QueryInterface(riid, ppvObject);
    prov->Release();
    return hr;
}

HRESULT ClassFactory::LockServer(BOOL fLock) {
    if (fLock) InterlockedIncrement(&g_LockCount);
    else       InterlockedDecrement(&g_LockCount);
    return S_OK;
}
