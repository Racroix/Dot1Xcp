#include "pch.h"
#include "Provider.h"
#include "ProviderBrokerSession.h"
#include "CpLog.h"

static LONG g_lastBrokerFlowState = 0; // 0:none, 1:pending, 2:idle
static BrokerSessionState g_session;
static SRWLOCK g_brokerSessionLock = SRWLOCK_INIT;

BrokerSessionState& GetBrokerSession()
{
    return g_session;
}

void LockBrokerSessionExclusive()
{
    AcquireSRWLockExclusive(&g_brokerSessionLock);
}

void UnlockBrokerSessionExclusive()
{
    ReleaseSRWLockExclusive(&g_brokerSessionLock);
}

void WaitAndCloseBrokerProcess(PROCESS_INFORMATION& pi, DWORD waitMs)
{
    if (pi.hProcess) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, waitMs);
        if (waitResult == WAIT_TIMEOUT) {
            CpLog(L"broker did not exit within timeout; terminating process");
            if (!TerminateProcess(pi.hProcess, 0)) {
                wchar_t tlog[160];
                swprintf_s(tlog, L"TerminateProcess failed err=%u", GetLastError());
                CpLog(tlog);
            }
            else {
                WaitForSingleObject(pi.hProcess, 500);
            }
        }
        else if (waitResult == WAIT_FAILED) {
            wchar_t wlog[160];
            swprintf_s(wlog, L"WaitForSingleObject failed err=%u", GetLastError());
            CpLog(wlog);
        }
        CloseHandle(pi.hProcess);
        pi.hProcess = nullptr;
    }

    if (pi.hThread) {
        CloseHandle(pi.hThread);
        pi.hThread = nullptr;
    }
}

void ClosePipeHandle(HANDLE& hPipe)
{
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

void UpdateBrokerFlowState(LONG state, PCWSTR transitionLog)
{
    LONG prev = InterlockedExchange(&g_lastBrokerFlowState, state);
    if (transitionLog && prev != state) {
        CpLog(transitionLog);
    }
}

void ResetBrokerSession(DWORD waitMs)
{
    ClosePipeHandle(g_session.hPipe);
    WaitAndCloseBrokerProcess(g_session.pi, waitMs);
    ZeroMemory(&g_session.pi, sizeof(g_session.pi));
    g_session.connected = false;
    SetBrokerSessionActive(false);
    UpdateBrokerFlowState(0, nullptr);
}

void CloseBrokerSessionNow(DWORD waitMs, PCWSTR reasonLog)
{
    BrokerSessionScopedLock lock;

    if (reasonLog && *reasonLog) {
        CpLog(reasonLog);
    }

    if (g_session.hPipe != INVALID_HANDLE_VALUE ||
        g_session.pi.hProcess ||
        g_session.pi.hThread ||
        g_session.connected)
    {
        ResetBrokerSession(waitMs);
    }
    else
    {
        SetBrokerSessionActive(false);
    }

    SetBrokerAwaitingFinal(false);
    UpdateBrokerFlowState(0, nullptr);
}
