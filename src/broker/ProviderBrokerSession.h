#pragma once

#include <windows.h>

struct BrokerSessionState
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    bool connected = false;
};

BrokerSessionState& GetBrokerSession();
void LockBrokerSessionExclusive();
void UnlockBrokerSessionExclusive();

class BrokerSessionScopedLock
{
public:
    BrokerSessionScopedLock() { LockBrokerSessionExclusive(); }
    ~BrokerSessionScopedLock() { UnlockBrokerSessionExclusive(); }
    BrokerSessionScopedLock(const BrokerSessionScopedLock&) = delete;
    BrokerSessionScopedLock& operator=(const BrokerSessionScopedLock&) = delete;
};

void WaitAndCloseBrokerProcess(PROCESS_INFORMATION& pi, DWORD waitMs);
void ClosePipeHandle(HANDLE& hPipe);
void UpdateBrokerFlowState(LONG state, PCWSTR transitionLog);
void ResetBrokerSession(DWORD waitMs);
