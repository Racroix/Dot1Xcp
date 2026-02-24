#pragma once
#include <windows.h>
#include <unknwn.h>

class ClassFactory : public IClassFactory {
    LONG _ref;
public:
    ClassFactory();
    virtual ~ClassFactory() = default;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;
};
