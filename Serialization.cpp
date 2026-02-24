#include "pch.h"
#include "Serialization.h"
#include "CpLog.h"

#include <windows.h>
#include <ntsecapi.h>
#include <wtsapi32.h>
#include <objbase.h>
#include <string>
#include <cstring>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "wtsapi32.lib")

#ifndef MICROSOFT_AUTHENTICATION_PACKAGE_V1_0
#define MICROSOFT_AUTHENTICATION_PACKAGE_V1_0 "MICROSOFT_AUTHENTICATION_PACKAGE_V1_0"
#endif

namespace Ser
{
// ---------- 공통 유틸 ----------
static BYTE* CoAllocZero(size_t cb) {
    BYTE* p = (BYTE*)::CoTaskMemAlloc(cb);
    if (p) std::memset(p, 0, cb);
    return p;
}

// MaximumLength = 널 포함, Length = 널 제외 (바이트 단위)
static void FillUS(UNICODE_STRING& us, PWSTR buf, USHORT bytesNoNull, USHORT bytesWithNull) {
    us.Buffer        = buf;
    us.Length        = bytesNoNull;
    us.MaximumLength = bytesWithNull;
}

// --- ResolvePkgA 함수 수정 ---
static bool ResolvePkgA(const char* name, ULONG& id) {
    id = 0;
    HANDLE h = nullptr;
    wchar_t log[256];
    auto closeLsa = [&]() {
        if (!h) return;
        NTSTATUS stClose = LsaDeregisterLogonProcess(h);
        if (stClose < 0) {
            wchar_t closeLog[256];
            swprintf_s(closeLog, L"ResolvePkgA: LsaDeregisterLogonProcess failed, NTSTATUS=0x%08X", stClose);
            CpLog(closeLog);
        }
        h = nullptr;
    };

    // 신뢰할 수 없는 연결로 복원합니다.
    CpLog(L"ResolvePkgA: Attempting LsaConnectUntrusted...");
    NTSTATUS st = LsaConnectUntrusted(&h); 
    if (st < 0) {
        swprintf_s(log, L"ResolvePkgA: LsaConnectUntrusted failed, NTSTATUS=0x%08X", st);
        CpLog(log);
        return false;
    }
    CpLog(L"ResolvePkgA: LsaConnectUntrusted OK.");

    LSA_STRING s{};
    s.Buffer = (PCHAR)name; s.Length = (USHORT)std::strlen(name); s.MaximumLength = s.Length;
    ULONG t = 0;

    swprintf_s(log, L"ResolvePkgA: Looking up package '%S'...", name);
    CpLog(log);

    st = LsaLookupAuthenticationPackage(h, &s, &t);

    if (st < 0) {
        closeLsa();
        swprintf_s(log, L"ResolvePkgA: LsaLookupAuthenticationPackage failed for '%S', NTSTATUS=0x%08X", name, st);
        CpLog(log);
        return false;
    }

    // ID 0 반환 시 실패 처리 (이전 단계의 유효한 로직)
    if (t == 0) {
        closeLsa();
        swprintf_s(log, L"ResolvePkgA: LsaLookup succeeded for '%S' but returned ID 0. Failing.", name);
        CpLog(log);
        return false; 
    }
    
    swprintf_s(log, L"ResolvePkgA: Found package '%S', ID=%lu. Success.", name, t);
    CpLog(log);
    closeLsa();
    id = t;
    return true;
}

static volatile LONG g_cachedKerberosPkg = 0;
static volatile LONG g_cachedMsvPkg = 0;

static bool ResolvePkgCached(const char* name, volatile LONG& cacheSlot, ULONG& id)
{
    LONG cached = InterlockedCompareExchange(&cacheSlot, 0, 0);
    if (cached > 0) {
        id = (ULONG)cached;
        return true;
    }

    ULONG resolved = 0;
    if (!ResolvePkgA(name, resolved)) {
        return false;
    }

    LONG prior = InterlockedCompareExchange(&cacheSlot, (LONG)resolved, 0);
    id = (prior > 0) ? (ULONG)prior : resolved;
    return true;
}

// --- ResolveNegotiate 함수 수정 ("Kerberos"로 변경) ---
static bool ResolveNegotiate(ULONG& id) {
    CpLog(L"ResolveNegotiate: Looking up 'Kerberos' package...");
    return ResolvePkgCached("Kerberos", g_cachedKerberosPkg, id);
}
static bool ResolveMsv(ULONG& id) { return ResolvePkgCached(MICROSOFT_AUTHENTICATION_PACKAGE_V1_0, g_cachedMsvPkg, id); }

// Unlock용 LogonId(LUID) 얻기: 콘솔 세션 사용자 토큰의 AuthenticationId
static bool GetConsoleLogonId(LUID* out) {
    if (!out) return false; *out = LUID{0,0};
    DWORD sid = WTSGetActiveConsoleSessionId();
    if (sid == 0xFFFFFFFF) return false;
    HANDLE hTok = nullptr;
    if (!WTSQueryUserToken(sid, &hTok)) return false;
    TOKEN_STATISTICS ts{}; DWORD cb = 0;
    BOOL ok = GetTokenInformation(hTok, TokenStatistics, &ts, sizeof(ts), &cb);
    CloseHandle(hTok);
    if (!ok) return false;
    *out = ts.AuthenticationId;
    return true;
}

// Kerb 경로에서 사용할 로컬 컴퓨터 NetBIOS 이름
static std::wstring LocalMachineName() {
    wchar_t buf[256]; DWORD n = _countof(buf);
    if (GetComputerNameW(buf, &n)) return std::wstring(buf, n);
    return L"."; // 실패 시 폴백
}

// ---------- SDK 래퍼 ----------
typedef struct _KERB_INTERACTIVE_UNLOCK_LOGON {
    KERB_INTERACTIVE_LOGON Logon;
    LUID                   LogonId;
} KERB_INTERACTIVE_UNLOCK_LOGON;

typedef struct _MSV1_0_WORKSTATION_UNLOCK_LOGON {
    MSV1_0_INTERACTIVE_LOGON Logon;
    LUID                     LogonId;
} MSV1_0_WORKSTATION_UNLOCK_LOGON;

// ---------- Kerb(Negotiate) 본문 ----------
static bool PackKerb(const std::wstring& domainIn,
                     const std::wstring& user,
                     const std::wstring& pass,
                     Scenario scen,
                     Packed& out)
{
    out = Packed{};
    if (user.empty()) return false;

    ULONG pkg = 0; if (!ResolveNegotiate(pkg)) return false;

    // Kerb에서는 로컬 계정인 경우 "." 대신 실제 컴퓨터 이름 사용
    const std::wstring dom = (domainIn.empty() || domainIn == L".") ? LocalMachineName() : domainIn;

    const size_t ld = dom.size(), lu = user.size(), lp = pass.size();
    const size_t cbd = (ld + 1) * sizeof(wchar_t);
    const size_t cbu = (lu + 1) * sizeof(wchar_t);
    const size_t cbp = (lp + 1) * sizeof(wchar_t);
    const USHORT bD_no = (USHORT)(ld * sizeof(wchar_t));
    const USHORT bU_no = (USHORT)(lu * sizeof(wchar_t));
    const USHORT bP_no = (USHORT)(lp * sizeof(wchar_t));
    const USHORT bD_w  = (USHORT)cbd;
    const USHORT bU_w  = (USHORT)cbu;
    const USHORT bP_w  = (USHORT)cbp;

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
        FillUS(pil->UserName,        u, bU_no, bU_w);
        FillUS(pil->Password,        w, bP_no, bP_w);

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    } else {
        LUID lid{}; if (!GetConsoleLogonId(&lid)) return false;
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
        FillUS(pul->Logon.UserName,        u, bU_no, bU_w);
        FillUS(pul->Logon.Password,        w, bP_no, bP_w);
        pul->LogonId = lid;

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    }
}

static bool PackMsv(const std::wstring& domainIn,
                    const std::wstring& user,
                    const std::wstring& pass,
                    Scenario scen, // <-- 파라미터 추가
                    Packed& out)
{
    out = Packed{};
    if (user.empty()) return false;

    ULONG pkg = 0; if (!ResolveMsv(pkg)) return false;
    const std::wstring dom = domainIn.empty() ? L"." : domainIn;

    const size_t ld = dom.size(), lu = user.size(), lp = pass.size();
    const size_t cbd = (ld + 1) * sizeof(wchar_t);
    const size_t cbu = (lu + 1) * sizeof(wchar_t);
    const size_t cbp = (lp + 1) * sizeof(wchar_t);
    const USHORT bD_no = (USHORT)(ld * sizeof(wchar_t));
    const USHORT bU_no = (USHORT)(lu * sizeof(wchar_t));
    const USHORT bP_no = (USHORT)(lp * sizeof(wchar_t));
    const USHORT bD_w  = (USHORT)cbd;
    const USHORT bU_w  = (USHORT)cbu;
    const USHORT bP_w  = (USHORT)cbp;

    if (scen == Scenario::Logon) // --- Logon 시나리오 ---
{
        const size_t cbH = sizeof(MSV1_0_INTERACTIVE_LOGON);
        const size_t cbT = cbH + cbd + cbu + cbp;
        BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

        auto* pil = (MSV1_0_INTERACTIVE_LOGON*)blob;
        BYTE* p = blob + cbH;

        auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
        auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
        auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);
        
        // --- 수정된 부분: 포인터를 BLOB 시작 주소 기준 오프셋으로 변환 ---
        // (wchar_t*)p는 (BYTE*)pil + Offset이므로, LSA에 전달할 때는 
        // (BYTE*)pil을 빼서 오프셋을 구한 후, 그 오프셋을 포인터로 다시 캐스팅해야 합니다.
        
        // 오프셋 계산: (현재 포인터 주소) - (BLOB 시작 주소)
        PWSTR d_offset_ptr = (PWSTR)((BYTE*)d - (BYTE*)pil);
        PWSTR u_offset_ptr = (PWSTR)((BYTE*)u - (BYTE*)pil);
        PWSTR w_offset_ptr = (PWSTR)((BYTE*)w - (BYTE*)pil);
        // -----------------------------------------------------------------

        pil->MessageType = MsV1_0InteractiveLogon;
        
        // FillUS 함수에 오프셋 포인터 전달
        FillUS(pil->LogonDomainName, d_offset_ptr, bD_no, bD_w);
        FillUS(pil->UserName,        u_offset_ptr, bU_no, bU_w);
        FillUS(pil->Password,        w_offset_ptr, bP_no, bP_w);

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    }
    else // --- Unlock 시나리오 (추가) ---
    {
        LUID lid{}; if (!GetConsoleLogonId(&lid)) return false;
        const size_t cbH = sizeof(MSV1_0_WORKSTATION_UNLOCK_LOGON);
        const size_t cbT = cbH + cbd + cbu + cbp;
        BYTE* blob = CoAllocZero(cbT); if (!blob) return false;

        auto* pul = (MSV1_0_WORKSTATION_UNLOCK_LOGON*)blob;
        BYTE* p = blob + cbH;

        auto* d = (wchar_t*)p; std::memcpy(d, dom.c_str(), cbd); p += cbd;
        auto* u = (wchar_t*)p; std::memcpy(u, user.c_str(), cbu); p += cbu;
        auto* w = (wchar_t*)p; std::memcpy(w, pass.c_str(), cbp);
        
        // --- 수정된 부분: 포인터를 BLOB 시작 주소 기준 오프셋으로 변환 ---
        PWSTR d_offset_ptr = (PWSTR)((BYTE*)d - (BYTE*)pul);
        PWSTR u_offset_ptr = (PWSTR)((BYTE*)u - (BYTE*)pul);
        PWSTR w_offset_ptr = (PWSTR)((BYTE*)w - (BYTE*)pul);
        // -----------------------------------------------------------------

        pul->Logon.MessageType = MsV1_0WorkstationUnlockLogon;
        
        // FillUS 함수에 오프셋 포인터 전달
        FillUS(pul->Logon.LogonDomainName, d_offset_ptr, bD_no, bD_w);
        FillUS(pul->Logon.UserName,        u_offset_ptr, bU_no, bU_w);
        FillUS(pul->Logon.Password,        w_offset_ptr, bP_no, bP_w);
        pul->LogonId = lid;

        out.blob = blob; out.size = (DWORD)cbT; out.authPkg = pkg; return true;
    }
}

// ---------- 공개 API ----------
bool PackAutoEx(const std::wstring& domain,
                const std::wstring& user,
                const std::wstring& password,
                Scenario scen,
                Packed& out)
{
    // --- 수정된 로직 ---
    // domain이 비어 있거나 "." 이면 로컬 계정으로 간주하고 MSV1_0 (NTLM) 사용
    if (domain.empty() || domain == L".")
    {
        CpLog(L"PackAutoEx: Local account (domain='.') detected. Using PackMsv (NTLM).");
        return PackMsv(domain, user, password, scen, out);
    }
    else
    {
        // 그 외 (실제 도메인 이름)는 Kerberos 사용
        CpLog(L"PackAutoEx: Domain account detected. Using PackKerb (Kerberos).");
        return PackKerb(domain, user, password, scen, out);
    }
}

} // namespace Ser
