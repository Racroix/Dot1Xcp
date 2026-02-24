#include "pch.h"
#include "Provider.h"
#include "CpLog.h"
#include "ProviderUtil.h"

#include <new>
#include <string>
#include <memory>
#include <credentialprovider.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

static LONG g_brokerSessionActive = 0;
static LONG g_brokerAwaitingFinal = 0;
static LONG g_refreshPending = 0;
static LONG g_brokerSessionAutoSubmitTick = 0;
static LONG g_forceNextAutoSubmit = 0;
static LONG g_lastSetSelectedDecision = -1;
static LONG g_brokerSessionGeneration = 1;
static ICredentialProviderEvents* g_cpEvents = nullptr;
static UINT_PTR g_cpEventsContext = 0;
static SRWLOCK g_cpEventsLock = SRWLOCK_INIT;

struct CredRefreshAsyncCtx
{
    ICredentialProviderEvents* events;
    UINT_PTR context;
    DWORD delayMs;
    LONG sessionGeneration;
};

LONG GetBrokerSessionGeneration()
{
    return InterlockedCompareExchange(&g_brokerSessionGeneration, 0, 0);
}

static bool IsCurrentSessionGeneration(LONG generation)
{
    return generation == GetBrokerSessionGeneration();
}

static DWORD WINAPI CredRefreshThreadProc(LPVOID param)
{
    std::unique_ptr<CredRefreshAsyncCtx> ctx(reinterpret_cast<CredRefreshAsyncCtx*>(param));
    if (!ctx || !ctx->events) {
        InterlockedExchange(&g_refreshPending, 0);
        return 0;
    }

    DWORD waited = 0;
    while (waited < ctx->delayMs) {
        if (!IsCurrentSessionGeneration(ctx->sessionGeneration)) {
            ctx->events->Release();
            InterlockedExchange(&g_refreshPending, 0);
            return 0;
        }

        DWORD remaining = ctx->delayMs - waited;
        DWORD sleepSlice = (remaining < 50) ? remaining : 50;
        Sleep(sleepSlice);
        waited += sleepSlice;
    }

    if (IsCurrentSessionGeneration(ctx->sessionGeneration)) {
        ctx->events->CredentialsChanged(ctx->context);
    }
    ctx->events->Release();
    InterlockedExchange(&g_refreshPending, 0);
    return 0;
}

bool IsBrokerSessionActive()
{
    return InterlockedCompareExchange(&g_brokerSessionActive, 0, 0) != 0;
}

void SetBrokerSessionActive(bool active)
{
    InterlockedExchange(&g_brokerSessionActive, active ? 1 : 0);
    if (!active) {
        InterlockedExchange(&g_brokerAwaitingFinal, 0);
        InterlockedExchange(&g_brokerSessionAutoSubmitTick, 0);
        InterlockedIncrement(&g_brokerSessionGeneration);
    }
}

bool IsBrokerAwaitingFinal()
{
    return InterlockedCompareExchange(&g_brokerAwaitingFinal, 0, 0) != 0;
}

void SetBrokerAwaitingFinal(bool awaiting)
{
    InterlockedExchange(&g_brokerAwaitingFinal, awaiting ? 1 : 0);
    if (awaiting) {
        // AUTH_PENDING 진입 직후에는 즉시 1회 재진입이 필요하다.
        InterlockedExchange(&g_brokerSessionAutoSubmitTick, 0);
    }
}

void ArmForceNextAutoSubmit()
{
    InterlockedExchange(&g_forceNextAutoSubmit, 1);
}

bool ConsumeForceNextAutoSubmit()
{
    return InterlockedCompareExchange(&g_forceNextAutoSubmit, 0, 1) == 1;
}

void RequestCredentialsChangedAsync(DWORD delayMs, LONG sessionGeneration)
{
    if (sessionGeneration <= 0) {
        sessionGeneration = GetBrokerSessionGeneration();
    }

    if (InterlockedCompareExchange(&g_refreshPending, 1, 0) != 0)
        return;

    ICredentialProviderEvents* events = nullptr;
    UINT_PTR context = 0;
    AcquireSRWLockShared(&g_cpEventsLock);
    events = g_cpEvents;
    context = g_cpEventsContext;
    if (events) {
        events->AddRef();
    }
    ReleaseSRWLockShared(&g_cpEventsLock);

    if (!events) {
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }

    auto* ctx = new(std::nothrow) CredRefreshAsyncCtx{ events, context, delayMs, sessionGeneration };
    if (!ctx) {
        events->Release();
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }

    HANDLE h = CreateThread(nullptr, 0, CredRefreshThreadProc, ctx, 0, nullptr);
    if (!h) {
        events->Release();
        delete ctx;
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }
    CloseHandle(h);
}

testCPProvider::testCPProvider() : _ref(1), _cpus(CPUS_INVALID) {}

HRESULT testCPProvider::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ICredentialProvider)) {
        *ppv = static_cast<ICredentialProvider*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG testCPProvider::AddRef() { return InterlockedIncrement(&_ref); }
ULONG testCPProvider::Release() { ULONG c = InterlockedDecrement(&_ref); if (!c) delete this; return c; }

HRESULT testCPProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD)
{
    if (cpus == CPUS_LOGON) {
        if (HasActiveConsoleUser()) {
            _cpus = CPUS_INVALID;
            CloseBrokerSessionNow(0, L"SetUsageScenario: LOGON with active console user -> close broker session");
            CpLog(L"SetUsageScenario: LOGON with active console user -> not supported");
            return E_NOTIMPL;
        }

        _cpus = cpus;
        if (!IsBrokerSessionActive()) {
            CloseBrokerSessionNow(0, L"SetUsageScenario: LOGON init -> close stale broker session");
            InterlockedExchange(&g_forceNextAutoSubmit, 0);
            SetBrokerSessionActive(false);
            SetBrokerAwaitingFinal(false);
        }
        else {
            CpLog(L"SetUsageScenario: LOGON re-enter with active broker -> keep current session");
        }
        CpLog(L"SetUsageScenario: LOGON supported");
        return S_OK;
    }

    _cpus = CPUS_INVALID;
    InterlockedExchange(&g_forceNextAutoSubmit, 0);
    CloseBrokerSessionNow(0, L"SetUsageScenario: non-LOGON -> close broker session");
    SetBrokerSessionActive(false);
    SetBrokerAwaitingFinal(false);
    if (cpus == CPUS_UNLOCK_WORKSTATION) {
        CpLog(L"SetUsageScenario: UNLOCK not supported");
    }
    return E_NOTIMPL;
}

HRESULT testCPProvider::SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) { return E_NOTIMPL; }
HRESULT testCPProvider::Advise(ICredentialProviderEvents* pcpe, UINT_PTR upAdviseContext)
{
    AcquireSRWLockExclusive(&g_cpEventsLock);
    if (g_cpEvents) {
        g_cpEvents->Release();
        g_cpEvents = nullptr;
    }
    g_cpEventsContext = upAdviseContext;
    g_cpEvents = pcpe;
    if (g_cpEvents) {
        g_cpEvents->AddRef();
    }
    ReleaseSRWLockExclusive(&g_cpEventsLock);
    return S_OK;
}

HRESULT testCPProvider::UnAdvise()
{
    AcquireSRWLockExclusive(&g_cpEventsLock);
    if (g_cpEvents) {
        g_cpEvents->Release();
        g_cpEvents = nullptr;
    }
    g_cpEventsContext = 0;
    ReleaseSRWLockExclusive(&g_cpEventsLock);
    return S_OK;
}

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR g_fields_template[FI_NUM_FIELDS] = {
    { FI_TITLE,  CPFT_LARGE_TEXT,    nullptr, GUID_NULL },
    { FI_SUBMIT, CPFT_SUBMIT_BUTTON, nullptr, GUID_NULL },
};

static HRESULT AllocLabelCopy(PCWSTR src, PWSTR* pOut)
{
    if (!pOut) return E_POINTER;
    *pOut = nullptr;
    if (!src) return S_OK;

    size_t cch = wcslen(src) + 1;
    PWSTR buf = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
    if (!buf) return E_OUTOFMEMORY;
    wcscpy_s(buf, cch, src);
    *pOut = buf;
    return S_OK;
}

HRESULT testCPProvider::GetFieldDescriptorCount(DWORD* pdwCount)
{
    if (!pdwCount) return E_POINTER;
    *pdwCount = FI_NUM_FIELDS;
    return S_OK;
}

HRESULT testCPProvider::GetFieldDescriptorAt(DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd)
{
    if (!ppcpfd) return E_POINTER;
    *ppcpfd = nullptr;
    if (dwIndex >= FI_NUM_FIELDS) return E_INVALIDARG;

    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pfd =
        (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(sizeof(*pfd));
    if (!pfd) return E_OUTOFMEMORY;

    *pfd = g_fields_template[dwIndex];

    PCWSTR label = nullptr;
    switch (dwIndex)
    {
    case FI_TITLE:
        label = kCpTitleText;
        break;
    case FI_SUBMIT:
        label = kCpSubmitLabel;
        break;
    default:
        label = nullptr;
        break;
    }

    HRESULT hr = AllocLabelCopy(label, &pfd->pszLabel);
    if (FAILED(hr)) {
        CoTaskMemFree(pfd);
        return hr;
    }

    *ppcpfd = pfd;
    return S_OK;
}

HRESULT testCPProvider::GetCredentialCount(DWORD* pdwCount, DWORD* pdwDefault, BOOL* pbAutoLogonWithDefault)
{
    if (!pdwCount || !pdwDefault || !pbAutoLogonWithDefault) return E_POINTER;
    if (_cpus != CPUS_LOGON) {
        *pdwCount = 0;
        *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
        *pbAutoLogonWithDefault = FALSE;
        return S_OK;
    }

    *pdwCount = 1;
    *pdwDefault = 0;
    *pbAutoLogonWithDefault = FALSE;
    return S_OK;
}

HRESULT testCPProvider::GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential** ppcpc)
{
    if (!ppcpc) return E_POINTER;
    *ppcpc = nullptr;
    if (dwIndex != 0) return E_INVALIDARG;
    auto* cred = new(std::nothrow) testCPCredential(_cpus);
    if (!cred) return E_OUTOFMEMORY;
    *ppcpc = cred;
    return S_OK;
}

testCPCredential::testCPCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus)
    : _ref(1), _cpus(cpus) {}

IFACEMETHODIMP testCPCredential::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ICredentialProviderCredential)) {
        *ppv = static_cast<ICredentialProviderCredential*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE testCPCredential::AddRef() { return InterlockedIncrement(&_ref); }
ULONG STDMETHODCALLTYPE testCPCredential::Release() { ULONG c = InterlockedDecrement(&_ref); if (!c) delete this; return c; }

IFACEMETHODIMP testCPCredential::Advise(ICredentialProviderCredentialEvents*) { return S_OK; }
IFACEMETHODIMP testCPCredential::UnAdvise() { return S_OK; }

IFACEMETHODIMP testCPCredential::SetSelected(BOOL* pbAutoLogon)
{
    const bool isLogonScenario = (_cpus == CPUS_LOGON);
    const bool brokerActive = IsBrokerSessionActive();
    const bool awaitingFinal = IsBrokerAwaitingFinal();
    bool shouldAutoSubmit = false;
    bool forceConsumed = false;

    if (isLogonScenario) {
        if (brokerActive) {
            if (awaitingFinal) {
                shouldAutoSubmit = true;
            } else {
                const DWORD nowTick = GetTickCount();
                const DWORD lastTick = (DWORD)InterlockedCompareExchange(&g_brokerSessionAutoSubmitTick, 0, 0);
                const DWORD elapsed = nowTick - lastTick;
                const DWORD kMinSessionSubmitIntervalMs = 2500;
                if (lastTick == 0 || elapsed >= kMinSessionSubmitIntervalMs) {
                    InterlockedExchange(&g_brokerSessionAutoSubmitTick, (LONG)nowTick);
                    shouldAutoSubmit = true;
                }
            }
        } else {
            forceConsumed = ConsumeForceNextAutoSubmit();
            if (forceConsumed) {
                InterlockedExchange(&g_brokerSessionAutoSubmitTick, (LONG)GetTickCount());
                shouldAutoSubmit = true;
            } else {
                const DWORD nowTick = GetTickCount();
                const DWORD lastTick = (DWORD)InterlockedCompareExchange(&g_brokerSessionAutoSubmitTick, 0, 0);
                const DWORD elapsed = nowTick - lastTick;
                const DWORD kMinInactiveSubmitIntervalMs = 2500;
                if (lastTick == 0 || elapsed >= kMinInactiveSubmitIntervalMs) {
                    InterlockedExchange(&g_brokerSessionAutoSubmitTick, (LONG)nowTick);
                    shouldAutoSubmit = true;
                }
            }
        }
    }

    if (pbAutoLogon) {
        *pbAutoLogon = shouldAutoSubmit ? TRUE : FALSE;
    }

    LONG logCode = 0;
    if (isLogonScenario) logCode |= 0x01;
    if (brokerActive) logCode |= 0x02;
    if (awaitingFinal) logCode |= 0x04;
    if (forceConsumed) logCode |= 0x08;
    if (shouldAutoSubmit) logCode |= 0x10;

    if (InterlockedExchange(&g_lastSetSelectedDecision, logCode) != logCode) {
        wchar_t log[196];
        swprintf_s(
            log,
            L"SetSelected: logon=%d active=%d awaiting=%d force=%d autoSubmit=%d",
            isLogonScenario ? 1 : 0,
            brokerActive ? 1 : 0,
            awaitingFinal ? 1 : 0,
            forceConsumed ? 1 : 0,
            shouldAutoSubmit ? 1 : 0
        );
        CpLog(log);
    }
    return S_OK;
}

IFACEMETHODIMP testCPCredential::SetDeselected()
{
    if (_cpus == CPUS_LOGON && IsBrokerSessionActive()) {
        CloseBrokerSessionNow(0, L"SetDeselected: closing broker session (tile deselected/unloaded)");
        ArmForceNextAutoSubmit();
    }
    return S_OK;
}

HRESULT testCPCredential::ReportResult(
    NTSTATUS ntsStatus,
    NTSTATUS ntsSubstatus,
    PWSTR* ppwszOptionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON* pcpsi)
{
    if (pcpsi) *pcpsi = CPSI_ERROR;

    if (ppwszOptionalStatusText) {
        *ppwszOptionalStatusText = NtStatusToText(ntsStatus, ntsSubstatus);
        if (!*ppwszOptionalStatusText) {
            *ppwszOptionalStatusText = DupSysAlloc(L"로그온 실패");
        }
    }

    wchar_t log[256];
    swprintf_s(log, L"ReportResult: NT=0x%08X, Sub=0x%08X (cpus=%d)", ntsStatus, ntsSubstatus, (int)_cpus);
    CpLog(log);

    return S_OK;
}
