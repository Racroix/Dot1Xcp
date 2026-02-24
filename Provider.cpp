#include "pch.h"
#include "Provider.h"

#include <new>

static LONG g_brokerSessionActive = 0;
static LONG g_brokerAwaitingFinal = 0;
static LONG g_refreshPending = 0;
static LONG g_brokerSessionAutoSubmitTick = 0;
static LONG g_forceNextAutoSubmit = 0;
static LONG g_brokerSessionGeneration = 1;
static ICredentialProviderEvents* g_cpEvents = nullptr;
static UINT_PTR g_cpEventsContext = 0;
static SRWLOCK g_cpEventsLock = SRWLOCK_INIT;

struct CredRefreshAsyncCtx
{
    ICredentialProviderEvents* events;
    UINT_PTR context;
    LONG sessionGeneration;
    PTP_TIMER timer;
};

LONG GetBrokerSessionGeneration()
{
    return InterlockedCompareExchange(&g_brokerSessionGeneration, 0, 0);
}

static bool IsCurrentSessionGeneration(LONG generation)
{
    return generation == GetBrokerSessionGeneration();
}

bool TryAcquireAutoSubmitWindow(DWORD minIntervalMs)
{
    const DWORD nowTick = GetTickCount();
    const DWORD lastTick = (DWORD)InterlockedCompareExchange(&g_brokerSessionAutoSubmitTick, 0, 0);
    const DWORD elapsed = nowTick - lastTick;
    if (lastTick == 0 || elapsed >= minIntervalMs) {
        InterlockedExchange(&g_brokerSessionAutoSubmitTick, (LONG)nowTick);
        return true;
    }
    return false;
}

void MarkAutoSubmitTickNow()
{
    InterlockedExchange(&g_brokerSessionAutoSubmitTick, (LONG)GetTickCount());
}

static VOID CALLBACK CredRefreshTimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER)
{
    CredRefreshAsyncCtx* ctx = reinterpret_cast<CredRefreshAsyncCtx*>(context);
    if (!ctx || !ctx->events) {
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }

    if (IsCurrentSessionGeneration(ctx->sessionGeneration)) {
        ctx->events->CredentialsChanged(ctx->context);
    }
    ctx->events->Release();
    if (ctx->timer) {
        CloseThreadpoolTimer(ctx->timer);
        ctx->timer = nullptr;
    }
    delete ctx;
    InterlockedExchange(&g_refreshPending, 0);
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
        InterlockedExchange(&g_brokerSessionAutoSubmitTick, 0);
    }
}

void ArmForceNextAutoSubmit()
{
    InterlockedExchange(&g_forceNextAutoSubmit, 1);
}

void ResetForceNextAutoSubmit()
{
    InterlockedExchange(&g_forceNextAutoSubmit, 0);
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

    auto* ctx = new(std::nothrow) CredRefreshAsyncCtx{ events, context, sessionGeneration, nullptr };
    if (!ctx) {
        events->Release();
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }

    ctx->timer = CreateThreadpoolTimer(CredRefreshTimerCallback, ctx, nullptr);
    if (!ctx->timer) {
        events->Release();
        delete ctx;
        InterlockedExchange(&g_refreshPending, 0);
        return;
    }

    ULARGE_INTEGER due{};
    due.QuadPart = static_cast<ULONGLONG>(-(static_cast<LONGLONG>(delayMs) * 10000LL));
    FILETIME ft{};
    ft.dwLowDateTime = due.LowPart;
    ft.dwHighDateTime = due.HighPart;
    SetThreadpoolTimer(ctx->timer, &ft, 0, 0);
}

void SetProviderEvents(ICredentialProviderEvents* events, UINT_PTR context)
{
    AcquireSRWLockExclusive(&g_cpEventsLock);
    if (g_cpEvents) {
        g_cpEvents->Release();
        g_cpEvents = nullptr;
    }

    g_cpEventsContext = context;
    g_cpEvents = events;
    if (g_cpEvents) {
        g_cpEvents->AddRef();
    }
    ReleaseSRWLockExclusive(&g_cpEventsLock);
}

void ClearProviderEvents()
{
    AcquireSRWLockExclusive(&g_cpEventsLock);
    if (g_cpEvents) {
        g_cpEvents->Release();
        g_cpEvents = nullptr;
    }
    g_cpEventsContext = 0;
    ReleaseSRWLockExclusive(&g_cpEventsLock);
}
