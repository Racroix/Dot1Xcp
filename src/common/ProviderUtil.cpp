#include "pch.h"
#include "ProviderUtil.h"

#include <shlwapi.h>
#include <wtsapi32.h>

bool HasActiveConsoleUser()
{
    DWORD sid = WTSGetActiveConsoleSessionId();
    if (sid == 0xFFFFFFFF) {
        return false;
    }

    LPWSTR user = nullptr;
    DWORD cb = 0;
    BOOL ok = WTSQuerySessionInformationW(
        WTS_CURRENT_SERVER_HANDLE,
        sid,
        WTSUserName,
        &user,
        &cb
    );
    if (!ok) {
        return false;
    }

    bool hasUser = (user && user[0] != L'\0');
    if (user) WTSFreeMemory(user);
    return hasUser;
}

PWSTR DupSysAlloc(PCWSTR s)
{
    if (!s) return nullptr;
    size_t n = wcslen(s) + 1;
    PWSTR p = (PWSTR)CoTaskMemAlloc(n * sizeof(WCHAR));
    if (!p) return nullptr;
    wcscpy_s(p, n, s);
    return p;
}

PWSTR NtStatusToText(NTSTATUS st, NTSTATUS sub)
{
    DWORD w32 = LsaNtStatusToWinError(st);
    wchar_t msgBuf[512] = L"";
    if (w32 != ERROR_MR_MID_NOT_FOUND) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, w32, 0, msgBuf, _countof(msgBuf), nullptr);
        PathRemoveBlanksW(msgBuf);
        size_t len = wcslen(msgBuf);
        while (len && (msgBuf[len - 1] == L'.' || msgBuf[len - 1] == L'\r' || msgBuf[len - 1] == L'\n' || msgBuf[len - 1] == L' '))
            msgBuf[--len] = 0;
    }

    const struct { NTSTATUS code; PCWSTR text; } map[] = {
        { (NTSTATUS)0xC000000DL, L"직렬화 형식 오류 (STATUS_INVALID_PARAMETER)" },
        { (NTSTATUS)0xC0000064L, L"사용자 없음 (STATUS_NO_SUCH_USER)" },
        { (NTSTATUS)0xC000006AL, L"비밀번호 불일치 (STATUS_WRONG_PASSWORD)" },
        { (NTSTATUS)0xC000006DL, L"로그온 실패 (STATUS_LOGON_FAILURE)" },
        { (NTSTATUS)0xC000006EL, L"계정 제한 (STATUS_ACCOUNT_RESTRICTION)" },
        { (NTSTATUS)0xC0000225L, L"인터랙티브 로그온 불가 (STATUS_NOT_FOUND)" },
    };

    PCWSTR picked = nullptr;
    for (auto& e : map) if (e.code == st) { picked = e.text; break; }

    wchar_t buf[768];
    if (picked && msgBuf[0])
        swprintf_s(buf, L"%s (NT=0x%08X, Sub=0x%08X) — %s", picked, st, sub, msgBuf);
    else if (picked)
        swprintf_s(buf, L"%s (NT=0x%08X, Sub=0x%08X)", picked, st, sub);
    else if (msgBuf[0])
        swprintf_s(buf, L"%s (NT=0x%08X, Sub=0x%08X)", msgBuf, st, sub);
    else
        swprintf_s(buf, L"로그온 실패 (NT=0x%08X, Sub=0x%08X)", st, sub);

    return DupSysAlloc(buf);
}

std::wstring Widen(const std::string& u8)
{
    if (u8.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring w;
    w.resize(wlen);
    int converted = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &w[0], wlen);
    if (converted <= 0) {
        w.clear();
    }
    return w;
}
