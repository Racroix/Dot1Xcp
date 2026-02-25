#include "pch.h"
#include "ProviderPipe.h"
#include "CpLog.h"

#include <vector>

static wchar_t* BuildChildEnvironmentWithPipe(std::vector<wchar_t>& envBlock, const std::wstring& pipeName)
{
    envBlock.clear();

    LPWCH baseEnv = GetEnvironmentStringsW();
    if (!baseEnv) {
        return nullptr;
    }

    for (LPCWSTR p = baseEnv; *p; p += (wcslen(p) + 1)) {
        if (_wcsnicmp(p, L"CP_PIPE=", 8) == 0) {
            continue;
        }
        size_t len = wcslen(p);
        envBlock.insert(envBlock.end(), p, p + len + 1);
    }

    std::wstring cpPipe = L"CP_PIPE=" + pipeName;
    envBlock.insert(envBlock.end(), cpPipe.begin(), cpPipe.end());
    envBlock.push_back(L'\0');

    envBlock.push_back(L'\0');
    FreeEnvironmentStringsW(baseEnv);
    return envBlock.data();
}

bool LaunchBroker(const std::wstring& pipeName, PROCESS_INFORMATION& piOut, std::wstring& err)
{
    std::wstring brokerPath = L"C:\\Program Files\\Dot1xCP\\Broker\\Dot1xBroker.exe";
    DWORD attr = GetFileAttributesW(brokerPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        wchar_t log[320];
        swprintf_s(log, L"broker path not found: %s (err=%u)", brokerPath.c_str(), GetLastError());
        CpLog(log);
    }

    std::wstring cmd = L"\"";
    cmd += brokerPath;
    cmd += L"\" --pipe ";
    cmd += pipeName;
    CpLog((L"launch broker cmd: " + cmd).c_str());

    std::vector<wchar_t> childEnvBlock;
    wchar_t* childEnv = BuildChildEnvironmentWithPipe(childEnvBlock, pipeName);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    ZeroMemory(&piOut, sizeof(piOut));

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    bool usedBreakaway = true;
    DWORD createFlags = CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        createFlags | CREATE_UNICODE_ENVIRONMENT, childEnv, nullptr, &si, &piOut
    );
    if (!ok) {
        DWORD firstErr = GetLastError();
        if (firstErr == ERROR_ACCESS_DENIED || firstErr == ERROR_NOT_SUPPORTED || firstErr == ERROR_INVALID_PARAMETER) {
            wchar_t firstLog[160];
            swprintf_s(firstLog, L"CreateProcess with BREAKAWAY failed err=%u. retry without BREAKAWAY.", firstErr);
            CpLog(firstLog);
            ok = CreateProcessW(
                nullptr, buf.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, childEnv, nullptr, &si, &piOut
            );
            if (ok) {
                usedBreakaway = false;
                CpLog(L"CreateProcess fallback without BREAKAWAY succeeded.");
            }
        }
    }
    if (!ok) {
        DWORD le = GetLastError();
        wchar_t tmp[128];
        swprintf_s(tmp, L"broker launch failed (err=%u)", le);
        err = tmp;
        CpLog(tmp);
        return false;
    }

    wchar_t okLog[160];
    swprintf_s(okLog, L"broker launched pid=%lu (breakaway=%s)", (unsigned long)piOut.dwProcessId, usedBreakaway ? L"true" : L"false");
    CpLog(okLog);

    return true;
}
