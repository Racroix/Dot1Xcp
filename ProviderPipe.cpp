#include "pch.h"
#include "ProviderPipe.h"
#include "CpLog.h"

#include <vector>
#include <sddl.h>
#include <objbase.h>

#pragma comment(lib, "Ole32.lib")

std::wstring MakePipeName()
{
    GUID g{};
    HRESULT hr = CoCreateGuid(&g);
    if (FAILED(hr)) {
        ULONGLONG t = GetTickCount64();
        wchar_t fb[72]{};
        swprintf_s(fb, L"\\\\.\\pipe\\cp.dot1x.%016I64X", t);
        std::wstring name = fb;
        CpLog((L"pipe name(fallback): " + name).c_str());
        return name;
    }

    wchar_t guidBuf[64]{};
    if (StringFromGUID2(g, guidBuf, ARRAYSIZE(guidBuf)) > 0) {
        std::wstring suffix = guidBuf;
        for (auto& ch : suffix) {
            if (ch == L'{' || ch == L'}' || ch == L'-') ch = L'_';
        }
        std::wstring ret = L"\\\\.\\pipe\\cp.dot1x." + suffix;
        CpLog((L"pipe name: " + ret).c_str());
        return ret;
    }

    ULONGLONG t = GetTickCount64();
    wchar_t fb[72]{};
    swprintf_s(fb, L"\\\\.\\pipe\\cp.dot1x.%016I64X", t);
    std::wstring name = fb;
    CpLog((L"pipe name(fallback2): " + name).c_str());
    return name;
}

static void SecureZeroVector(std::vector<char>& buf)
{
    if (!buf.empty()) {
        SecureZeroMemory(&buf[0], buf.size());
    }
}

HANDLE CreateRestrictedPipeServer(const std::wstring& pipeName)
{
    LPCWSTR sddl_strict = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)";
    LPCWSTR sddl_relax = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)";

    auto make_pipe = [&](LPCWSTR sddl) -> HANDLE {
        SECURITY_ATTRIBUTES sa{};
        PSECURITY_DESCRIPTOR psd = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &psd, nullptr)) {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = psd;
            sa.bInheritHandle = FALSE;
        }
        HANDLE h = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 8192, 8192, 30000,
            psd ? &sa : nullptr
        );
        if (psd) LocalFree(psd);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            wchar_t log[256];
            swprintf_s(log, L"CreateNamedPipe failed (err=%u) sddl=%s", err, sddl ? sddl : L"(null)");
            CpLog(log);
        }
        return h;
    };

    HANDLE hPipe = make_pipe(sddl_strict);
    if (hPipe != INVALID_HANDLE_VALUE) return hPipe;

    hPipe = make_pipe(sddl_relax);
    if (hPipe != INVALID_HANDLE_VALUE) return hPipe;

    HANDLE h = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 8192, 8192, 30000,
        nullptr
    );
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        wchar_t log[256];
        swprintf_s(log, L"CreateNamedPipe failed (final, no SA) err=%u", err);
        CpLog(log);
    }
    return h;
}

PipeConnectResult WaitPipeClientConnect(HANDLE hPipe, HANDLE hBrokerProcess, DWORD timeoutMs)
{
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return PipeConnectResult::Failed;

    BOOL ok = ConnectNamedPipe(hPipe, &ov);
    if (ok) {
        CloseHandle(ov.hEvent);
        return PipeConnectResult::Connected;
    }

    DWORD le = GetLastError();
    if (le == ERROR_PIPE_CONNECTED) {
        SetEvent(ov.hEvent);
        CloseHandle(ov.hEvent);
        return PipeConnectResult::Connected;
    }
    if (le != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return PipeConnectResult::Failed;
    }

    HANDLE waits[2] = { ov.hEvent, hBrokerProcess };
    DWORD waitCount = hBrokerProcess ? 2 : 1;
    DWORD wr = WaitForMultipleObjects(waitCount, waits, FALSE, timeoutMs);

    if (wr == WAIT_OBJECT_0) {
        DWORD transferred = 0;
        BOOL done = GetOverlappedResult(hPipe, &ov, &transferred, FALSE);
        DWORD gle = done ? ERROR_SUCCESS : GetLastError();
        CloseHandle(ov.hEvent);
        if (done || gle == ERROR_PIPE_CONNECTED) return PipeConnectResult::Connected;
        return PipeConnectResult::Failed;
    }

    if (wr == WAIT_OBJECT_0 + 1) {
        CancelIoEx(hPipe, &ov);
        CloseHandle(ov.hEvent);
        return PipeConnectResult::BrokerExited;
    }

    if (wr == WAIT_TIMEOUT) {
        CancelIoEx(hPipe, &ov);
        CloseHandle(ov.hEvent);
        return PipeConnectResult::Timeout;
    }

    CancelIoEx(hPipe, &ov);
    CloseHandle(ov.hEvent);
    return PipeConnectResult::Failed;
}

static PipeReadResult ReadPipeMessageWithTimeout(HANDLE hPipe,
                                                 HANDLE hBrokerProcess,
                                                 DWORD timeoutMs,
                                                 std::string& outMsg)
{
    outMsg.clear();
    std::vector<char> buf(8192);
    static const size_t kMaxMsgBytes = 256 * 1024;

    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            wchar_t log[128];
            swprintf_s(log, L"ReadPipeMessageWithTimeout: CreateEventW failed err=%u", GetLastError());
            CpLog(log);
            SecureZeroVector(buf);
            return PipeReadResult::Failed;
        }

        DWORD cbRead = 0;
        BOOL ok = ReadFile(hPipe, buf.data(), (DWORD)buf.size(), &cbRead, &ov);
        if (ok) {
            CloseHandle(ov.hEvent);
            if (cbRead == 0) {
                CpLog(L"ReadPipeMessageWithTimeout: ReadFile ok but cbRead=0");
                SecureZeroVector(buf);
                return PipeReadResult::Failed;
            }
            outMsg.append(buf.data(), cbRead);
            SecureZeroMemory(buf.data(), cbRead);
            if (outMsg.size() > kMaxMsgBytes) {
                CpLog(L"ReadPipeMessageWithTimeout: message too large");
                SecureZeroVector(buf);
                outMsg.clear();
                return PipeReadResult::Failed;
            }
            return PipeReadResult::Ok;
        }

        DWORD le = GetLastError();
        if (le == ERROR_MORE_DATA) {
            CloseHandle(ov.hEvent);
            if (cbRead == 0) {
                CpLog(L"ReadPipeMessageWithTimeout: ERROR_MORE_DATA with cbRead=0");
                SecureZeroVector(buf);
                return PipeReadResult::Failed;
            }
            outMsg.append(buf.data(), cbRead);
            SecureZeroMemory(buf.data(), cbRead);
            if (outMsg.size() > kMaxMsgBytes) {
                CpLog(L"ReadPipeMessageWithTimeout: message too large");
                SecureZeroVector(buf);
                outMsg.clear();
                return PipeReadResult::Failed;
            }
            continue;
        }

        if (le == ERROR_BROKEN_PIPE) {
            CloseHandle(ov.hEvent);
            CpLog(L"ReadPipeMessageWithTimeout: ERROR_BROKEN_PIPE");
            SecureZeroVector(buf);
            return PipeReadResult::BrokerExited;
        }

        if (le != ERROR_IO_PENDING) {
            wchar_t log[128];
            swprintf_s(log, L"ReadPipeMessageWithTimeout: ReadFile failed err=%u", le);
            CpLog(log);
            CloseHandle(ov.hEvent);
            SecureZeroVector(buf);
            return PipeReadResult::Failed;
        }

        HANDLE waits[2] = { ov.hEvent, hBrokerProcess };
        DWORD waitCount = hBrokerProcess ? 2 : 1;
        DWORD wr = WaitForMultipleObjects(waitCount, waits, FALSE, timeoutMs);

        if (wr == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            BOOL done = GetOverlappedResult(hPipe, &ov, &transferred, FALSE);
            DWORD gle = done ? ERROR_SUCCESS : GetLastError();
            CloseHandle(ov.hEvent);

            if (done || gle == ERROR_MORE_DATA) {
                if (transferred == 0) {
                    CpLog(L"ReadPipeMessageWithTimeout: transferred=0");
                    SecureZeroVector(buf);
                    return PipeReadResult::Failed;
                }
                outMsg.append(buf.data(), transferred);
                SecureZeroMemory(buf.data(), transferred);
                if (outMsg.size() > kMaxMsgBytes) {
                    CpLog(L"ReadPipeMessageWithTimeout: message too large");
                    SecureZeroVector(buf);
                    outMsg.clear();
                    return PipeReadResult::Failed;
                }
                if (done) {
                    return PipeReadResult::Ok;
                }
                continue;
            }

            if (gle == ERROR_BROKEN_PIPE) {
                CpLog(L"ReadPipeMessageWithTimeout: GetOverlappedResult ERROR_BROKEN_PIPE");
                SecureZeroVector(buf);
                return PipeReadResult::BrokerExited;
            }

            wchar_t log[128];
            swprintf_s(log, L"ReadPipeMessageWithTimeout: GetOverlappedResult failed err=%u", gle);
            CpLog(log);
            SecureZeroVector(buf);
            return PipeReadResult::Failed;
        }

        if (wr == WAIT_OBJECT_0 + 1) {
            CancelIoEx(hPipe, &ov);
            CloseHandle(ov.hEvent);
            SecureZeroVector(buf);
            return PipeReadResult::BrokerExited;
        }

        if (wr == WAIT_TIMEOUT) {
            CancelIoEx(hPipe, &ov);
            CloseHandle(ov.hEvent);
            SecureZeroVector(buf);
            return PipeReadResult::Timeout;
        }

        CancelIoEx(hPipe, &ov);
        CloseHandle(ov.hEvent);
        SecureZeroVector(buf);
        return PipeReadResult::Failed;
    }
}

PipeReadResult ReadPipeMessageUntilDone(HANDLE hPipe, HANDLE hBrokerProcess, std::string& outMsg, DWORD sliceTimeoutMs, DWORD overallTimeoutMs)
{
    const DWORD readSliceTimeoutMs = (sliceTimeoutMs == 0 ? 30000 : sliceTimeoutMs);
    const ULONGLONG startTick = GetTickCount64();

    for (;;) {
        PipeReadResult rr = ReadPipeMessageWithTimeout(hPipe, hBrokerProcess, readSliceTimeoutMs, outMsg);
        if (rr != PipeReadResult::Timeout) {
            return rr;
        }

        if (!hBrokerProcess) {
            return PipeReadResult::Timeout;
        }

        if (WaitForSingleObject(hBrokerProcess, 0) == WAIT_OBJECT_0) {
            return PipeReadResult::BrokerExited;
        }

        if (overallTimeoutMs != INFINITE) {
            ULONGLONG elapsed = GetTickCount64() - startTick;
            if (elapsed >= overallTimeoutMs) {
                return PipeReadResult::Timeout;
            }
        }

    }
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

    SetEnvironmentVariableW(L"CP_PIPE", pipeName.c_str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    ZeroMemory(&piOut, sizeof(piOut));

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    bool usedBreakaway = true;
    DWORD createFlags = CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        createFlags, nullptr, nullptr, &si, &piOut
    );
    if (!ok) {
        DWORD firstErr = GetLastError();
        if (firstErr == ERROR_ACCESS_DENIED || firstErr == ERROR_NOT_SUPPORTED || firstErr == ERROR_INVALID_PARAMETER) {
            wchar_t firstLog[160];
            swprintf_s(firstLog, L"CreateProcess with BREAKAWAY failed err=%u. retry without BREAKAWAY.", firstErr);
            CpLog(firstLog);
            ok = CreateProcessW(
                nullptr, buf.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &piOut
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
