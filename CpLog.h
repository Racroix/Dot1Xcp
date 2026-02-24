#pragma once

#include <windows.h>
#include <cstdio>
#include <cwchar>

inline void CpLog(const wchar_t* msg)
{
    if (!msg) return;
    static SRWLOCK s_logLock = SRWLOCK_INIT;

    wchar_t dir[MAX_PATH] = L"C:\\ProgramData\\Dot1xCP";
    CreateDirectoryW(dir, nullptr);
    wchar_t path[MAX_PATH] = L"C:\\ProgramData\\Dot1xCP\\cp.log";

    AcquireSRWLockExclusive(&s_logLock);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ReleaseSRWLockExclusive(&s_logLock);
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t line[1024];
    swprintf_s(line, L"%04d-%02d-%02d %02d:%02d:%02d  %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);

    DWORD cbToWrite = (DWORD)(wcslen(line) * sizeof(wchar_t));
    DWORD cbWritten = 0;
    WriteFile(h, line, cbToWrite, &cbWritten, nullptr);
    CloseHandle(h);
    ReleaseSRWLockExclusive(&s_logLock);
}
