#pragma once

#include <windows.h>
#include <cstdio>
#include <cwchar>

inline void CpLog(const wchar_t* msg)
{
    if (!msg) return;

    wchar_t dir[MAX_PATH] = L"C:\\ProgramData\\Dot1xCP";
    CreateDirectoryW(dir, nullptr);
    wchar_t path[MAX_PATH] = L"C:\\ProgramData\\Dot1xCP\\cp.log";

    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SetFilePointer(h, 0, 0, FILE_END);
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t line[1024];
    swprintf_s(line, L"%04d-%02d-%02d %02d:%02d:%02d  %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);

    DWORD cb = (DWORD)(wcslen(line) * sizeof(wchar_t));
    WriteFile(h, line, cb, &cb, nullptr);
    CloseHandle(h);
}
