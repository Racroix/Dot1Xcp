#include "pch.h"
#include "SerializationAuth.h"
#include "CpLog.h"

#include <windows.h>
#include <ntsecapi.h>
#include <cstring>

#ifndef MICROSOFT_AUTHENTICATION_PACKAGE_V1_0
#define MICROSOFT_AUTHENTICATION_PACKAGE_V1_0 "MICROSOFT_AUTHENTICATION_PACKAGE_V1_0"
#endif

namespace Ser {

static bool ResolvePkgA(const char* name, ULONG& id)
{
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

    CpLog(L"ResolvePkgA: Attempting LsaConnectUntrusted...");
    NTSTATUS st = LsaConnectUntrusted(&h);
    if (st < 0) {
        swprintf_s(log, L"ResolvePkgA: LsaConnectUntrusted failed, NTSTATUS=0x%08X", st);
        CpLog(log);
        return false;
    }
    CpLog(L"ResolvePkgA: LsaConnectUntrusted OK.");

    LSA_STRING s{};
    s.Buffer = (PCHAR)name;
    s.Length = (USHORT)std::strlen(name);
    s.MaximumLength = s.Length;
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

bool ResolveNegotiate(ULONG& id)
{
    CpLog(L"ResolveNegotiate: Looking up 'Kerberos' package...");
    return ResolvePkgCached("Kerberos", g_cachedKerberosPkg, id);
}

bool ResolveMsv(ULONG& id)
{
    return ResolvePkgCached(MICROSOFT_AUTHENTICATION_PACKAGE_V1_0, g_cachedMsvPkg, id);
}

} // namespace Ser
