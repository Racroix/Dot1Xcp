#include "pch.h"
#include "Serialization.h"
#include "SerializationAuth.h"
#include "CpLog.h"

#include <windows.h>
#include <ntsecapi.h>
#include <wtsapi32.h>
#include <objbase.h>
#include <string>
#include <cstring>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "wtsapi32.lib")

namespace Ser
{

static BYTE* CoAllocZero(size_t cb)
{
    BYTE* p = (BYTE*)::CoTaskMemAlloc(cb);
    if (p) std::memset(p, 0, cb);
    return p;
}

static void FillUS(UNICODE_STRING& us, PWSTR buf, USHORT bytesNoNull, USHORT bytesWithNull)
{
    us.Buffer = buf;
    us.Length = bytesNoNull;
    us.MaximumLength = bytesWithNull;
}

static bool GetConsoleLogonId(LUID* out)
{
    if (!out) return false;
    *out = LUID{ 0, 0 };
    DWORD sid = WTSGetActiveConsoleSessionId();
    if (sid == 0xFFFFFFFF) return false;

    HANDLE hTok = nullptr;
    if (!WTSQueryUserToken(sid, &hTok)) return false;

    TOKEN_STATISTICS ts{};
    DWORD cb = 0;
    BOOL ok = GetTokenInformation(hTok, TokenStatistics, &ts, sizeof(ts), &cb);
    CloseHandle(hTok);
    if (!ok) return false;

    *out = ts.AuthenticationId;
    return true;
}

static std::wstring LocalMachineName()
{
    wchar_t buf[256];
    DWORD n = _countof(buf);
    if (GetComputerNameW(buf, &n)) return std::wstring(buf, n);
    return L".";
}

typedef struct _KERB_INTERACTIVE_UNLOCK_LOGON {
    KERB_INTERACTIVE_LOGON Logon;
    LUID                   LogonId;
} KERB_INTERACTIVE_UNLOCK_LOGON;

typedef struct _MSV1_0_WORKSTATION_UNLOCK_LOGON {
    MSV1_0_INTERACTIVE_LOGON Logon;
    LUID                     LogonId;
} MSV1_0_WORKSTATION_UNLOCK_LOGON;

static bool PackKerb(const std::wstring& domainIn,
                     const std::wstring& user,
                     const std::wstring& pass,
                     Scenario scen,
                     Packed& out)
{
    out = Packed{};
    if (user.empty()) return false;

    ULONG pkg = 0;
    if (!ResolveNegotiate(pkg)) return false;

    const std::wstring dom = (domainIn.empty() || domainIn == L".") ? LocalMachineName() : domainIn;

    const size_t ld = dom.size(), lu = user.size(), lp = pass.size();
    const size_t cbd = (ld + 1) * sizeof(wchar_t);
    const size_t cbu = (lu + 1) * sizeof(wchar_t);
    const size_t cbp = (lp + 1) * sizeof(wchar_t);
    const USHORT bD_no = (USHORT)(ld * sizeof(wchar_t));
    const USHORT bU_no = (USHORT)(lu * sizeof(wchar_t));
    const USHORT bP_no = (USHORT)(lp * sizeof(wchar_t));
    const USHORT bD_w = (USHORT)cbd;
    const USHORT bU_w = (USHORT)cbu;
    const USHORT bP_w = (USHORT)cbp;

    if (scen == Scenario::Logon) {
        const size_t cbH = sizeof(KERB_INTERACTIVE_LOGON);
        const size_t cbT = cbH + cbd + cbu + cbp;
        BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

        auto* pil = (KERB_INTERACTIVE_LOGON*)blob;
        BYTE* p = blob + cbH;

        auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
        auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
        auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);

        pil->MessageType = KerbInteractiveLogon;
        FillUS(pil->LogonDomainName, d, bD_no, bD_w);
        FillUS(pil->UserName, u, bU_no, bU_w);
        FillUS(pil->Password, w, bP_no, bP_w);

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    }

    LUID lid{};
    if (!GetConsoleLogonId(&lid)) return false;

    const size_t cbH = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON);
    const size_t cbT = cbH + cbd + cbu + cbp;
    BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

    auto* pul = (KERB_INTERACTIVE_UNLOCK_LOGON*)blob;
    BYTE* p = blob + cbH;

    auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
    auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
    auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);

    pul->Logon.MessageType = KerbWorkstationUnlockLogon;
    FillUS(pul->Logon.LogonDomainName, d, bD_no, bD_w);
    FillUS(pul->Logon.UserName, u, bU_no, bU_w);
    FillUS(pul->Logon.Password, w, bP_no, bP_w);
    pul->LogonId = lid;

    out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
}

static bool PackMsv(const std::wstring& domainIn,
                    const std::wstring& user,
                    const std::wstring& pass,
                    Scenario scen,
                    Packed& out)
{
    out = Packed{};
    if (user.empty()) return false;

    ULONG pkg = 0;
    if (!ResolveMsv(pkg)) return false;
    const std::wstring dom = domainIn.empty() ? L"." : domainIn;

    const size_t ld = dom.size(), lu = user.size(), lp = pass.size();
    const size_t cbd = (ld + 1) * sizeof(wchar_t);
    const size_t cbu = (lu + 1) * sizeof(wchar_t);
    const size_t cbp = (lp + 1) * sizeof(wchar_t);
    const USHORT bD_no = (USHORT)(ld * sizeof(wchar_t));
    const USHORT bU_no = (USHORT)(lu * sizeof(wchar_t));
    const USHORT bP_no = (USHORT)(lp * sizeof(wchar_t));
    const USHORT bD_w = (USHORT)cbd;
    const USHORT bU_w = (USHORT)cbu;
    const USHORT bP_w = (USHORT)cbp;

    if (scen == Scenario::Logon)
    {
        const size_t cbH = sizeof(MSV1_0_INTERACTIVE_LOGON);
        const size_t cbT = cbH + cbd + cbu + cbp;
        BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

        auto* pil = (MSV1_0_INTERACTIVE_LOGON*)blob;
        BYTE* p = blob + cbH;

        auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
        auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
        auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);

        PWSTR d_offset_ptr = (PWSTR)((BYTE*)d - (BYTE*)pil);
        PWSTR u_offset_ptr = (PWSTR)((BYTE*)u - (BYTE*)pil);
        PWSTR w_offset_ptr = (PWSTR)((BYTE*)w - (BYTE*)pil);

        pil->MessageType = MsV1_0InteractiveLogon;
        FillUS(pil->LogonDomainName, d_offset_ptr, bD_no, bD_w);
        FillUS(pil->UserName, u_offset_ptr, bU_no, bU_w);
        FillUS(pil->Password, w_offset_ptr, bP_no, bP_w);

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    }

    LUID lid{};
    if (!GetConsoleLogonId(&lid)) return false;
    const size_t cbH = sizeof(MSV1_0_WORKSTATION_UNLOCK_LOGON);
    const size_t cbT = cbH + cbd + cbu + cbp;
    BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

    auto* pul = (MSV1_0_WORKSTATION_UNLOCK_LOGON*)blob;
    BYTE* p = blob + cbH;

    auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
    auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
    auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);

    PWSTR d_offset_ptr = (PWSTR)((BYTE*)d - (BYTE*)pul);
    PWSTR u_offset_ptr = (PWSTR)((BYTE*)u - (BYTE*)pul);
    PWSTR w_offset_ptr = (PWSTR)((BYTE*)w - (BYTE*)pul);

    pul->Logon.MessageType = MsV1_0WorkstationUnlockLogon;
    FillUS(pul->Logon.LogonDomainName, d_offset_ptr, bD_no, bD_w);
    FillUS(pul->Logon.UserName, u_offset_ptr, bU_no, bU_w);
    FillUS(pul->Logon.Password, w_offset_ptr, bP_no, bP_w);
    pul->LogonId = lid;

    out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
}

bool PackAutoEx(const std::wstring& domain,
                const std::wstring& user,
                const std::wstring& password,
                Scenario scen,
                Packed& out)
{
    if (domain.empty() || domain == L".")
    {
        CpLog(L"PackAutoEx: Local account (domain='.') detected. Using PackMsv (NTLM).");
        return PackMsv(domain, user, password, scen, out);
    }

    CpLog(L"PackAutoEx: Domain account detected. Using PackKerb (Kerberos).");
    return PackKerb(domain, user, password, scen, out);
}

} // namespace Ser
