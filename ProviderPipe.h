#pragma once

#include <windows.h>
#include <string>

enum class PipeConnectResult {
    Connected,
    Timeout,
    BrokerExited,
    Failed
};

enum class PipeReadResult {
    Ok,
    Timeout,
    BrokerExited,
    Failed
};

std::wstring MakePipeName();
HANDLE CreateRestrictedPipeServer(const std::wstring& pipeName);
PipeConnectResult WaitPipeClientConnect(HANDLE hPipe, HANDLE hBrokerProcess, DWORD timeoutMs);
PipeReadResult ReadPipeMessageUntilDone(HANDLE hPipe, HANDLE hBrokerProcess, std::string& outMsg, DWORD sliceTimeoutMs = 30000, DWORD overallTimeoutMs = INFINITE);
bool LaunchBroker(const std::wstring& pipeName, PROCESS_INFORMATION& piOut, std::wstring& err);
