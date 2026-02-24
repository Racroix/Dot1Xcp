#pragma once

#include <windows.h>
#include <ntsecapi.h>
#include <string>

bool HasRecentUserInput(DWORD maxIdleMs);
bool HasActiveConsoleUser();
PWSTR DupSysAlloc(PCWSTR s);
PWSTR NtStatusToText(NTSTATUS st, NTSTATUS sub);
std::wstring Widen(const std::string& u8);
