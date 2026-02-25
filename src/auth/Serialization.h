#pragma once
#include <windows.h>
#include <string>

namespace Ser {

    struct Packed {
        BYTE*  blob    = nullptr;  // CoTaskMemAlloc
        DWORD  size    = 0;
        ULONG  authPkg = 0;
    };

    // 시나리오
    enum class Scenario : int { Logon, Unlock };

    // domain이 비었거나 "."이면 MSV1_0, 그 외는 Kerberos 경로를 사용.
    bool PackAutoEx(const std::wstring& domain,
                    const std::wstring& user,
                    const std::wstring& password,
                    Scenario scen,
                    Packed& out);

}
