#include "pch.h"
#include "Provider.h"
#include "SimpleCpGuids.h"
#include "Serialization.h"
#include "CpLog.h"
#include "ProviderUtil.h"
#include "ProviderPipe.h"
#include "ProviderJson.h"

#include <string>

static const wchar_t* kMsgPipeCreateFail = L"pipe create failed";
static const wchar_t* kMsgBrokerLaunchFail = L"broker launch failed";
static const wchar_t* kMsgConnectTimeout = L"broker connect timeout";
static const wchar_t* kMsgReadTimeout = L"broker response timeout";
static const wchar_t* kMsgNoResponse = L"no response from broker";
static const wchar_t* kMsgBrokerExited = L"broker exited unexpectedly";
static const wchar_t* kMsgParseFail = L"response parse failed";
static const wchar_t* kMsgAuthFail = L"authentication failed";
static const wchar_t* kMsgSerializeNA = L"serialization not implemented";
static LONG g_lastBrokerFlowState = 0; // 0:none, 1:pending, 2:idle

struct SensitiveBrokerMessageGuard
{
    std::string& json;
    Msg& msg;
    ~SensitiveBrokerMessageGuard()
    {
        SecureClearMsg(msg);
        SecureClearString(json);
    }
};

static void SetOptionalStatusText(PWSTR* ppwszOptionalStatusText, PCWSTR text)
{
    if (ppwszOptionalStatusText) {
        *ppwszOptionalStatusText = DupSysAlloc(text);
    }
}

static void WaitAndCloseBrokerProcess(PROCESS_INFORMATION& pi, DWORD waitMs)
{
    if (pi.hProcess) {
        if (waitMs > 0) {
            WaitForSingleObject(pi.hProcess, waitMs);
        }
        CloseHandle(pi.hProcess);
        pi.hProcess = nullptr;
    }

    if (pi.hThread) {
        CloseHandle(pi.hThread);
        pi.hThread = nullptr;
    }
}

static void ClosePipeHandle(HANDLE& hPipe)
{
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

static void LogBrokerExitCodeIfAvailable(const PROCESS_INFORMATION& pi, PCWSTR context)
{
    if (!pi.hProcess)
        return;

    DWORD ec = 0;
    if (!GetExitCodeProcess(pi.hProcess, &ec))
        return;

    wchar_t elog[192];
    swprintf_s(elog, L"broker process exit code (%s)=0x%08X", context ? context : L"unknown", ec);
    CpLog(elog);
}

static PCWSTR ConnectFailureStatusText(PipeConnectResult result)
{
    return (result == PipeConnectResult::BrokerExited) ? kMsgBrokerExited : kMsgConnectTimeout;
}

static PCWSTR ConnectFailureLogText(PipeConnectResult result)
{
    if (result == PipeConnectResult::BrokerExited) return L"broker exited before pipe connect";
    if (result == PipeConnectResult::Timeout) return L"pipe connect timeout";
    return L"pipe connect failed";
}

static PCWSTR ReadFailureStatusText(PipeReadResult result)
{
    if (result == PipeReadResult::BrokerExited) return kMsgBrokerExited;
    if (result == PipeReadResult::Timeout) return kMsgReadTimeout;
    return kMsgNoResponse;
}

static PCWSTR ReadFailureLogText(PipeReadResult result)
{
    if (result == PipeReadResult::BrokerExited) return L"broker exited before sending response";
    if (result == PipeReadResult::Timeout) return L"broker read timeout";
    return L"no response from broker";
}

static HRESULT ReturnNoCredentialNotFinished(
    PROCESS_INFORMATION& pi,
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    PWSTR* ppwszOptionalStatusText,
    PCWSTR statusText,
    PCWSTR logText,
    DWORD waitMs = 1000)
{
    WaitAndCloseBrokerProcess(pi, waitMs);
    SetOptionalStatusText(ppwszOptionalStatusText, statusText);
    *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    if (logText) {
        CpLog(logText);
    }
    return S_OK;
}

static HRESULT ReturnNoCredentialFinished(
    PROCESS_INFORMATION& pi,
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    PCWSTR logText,
    DWORD waitMs = 1000)
{
    WaitAndCloseBrokerProcess(pi, waitMs);
    *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
    if (logText) {
        CpLog(logText);
    }
    return S_OK;
}

static HRESULT HandleAuthSuccessMessage(
    const Msg& m,
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    PROCESS_INFORMATION& pi,
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
    PWSTR* ppwszOptionalStatusText)
{
    std::wstring wDomain = L".";
    std::wstring wUser = Widen(m.sam);
    std::wstring wPass = Widen(m.password);

    Ser::Packed pk{};
    Ser::Scenario scen = (cpus == CPUS_UNLOCK_WORKSTATION) ? Ser::Scenario::Unlock
                                                            : Ser::Scenario::Logon;
    bool packed = Ser::PackAutoEx(wDomain, wUser, wPass, scen, pk);

    if (!wPass.empty()) {
        SecureZeroMemory(&wPass[0], wPass.size() * sizeof(wchar_t));
    }
    if (!wUser.empty()) {
        SecureZeroMemory(&wUser[0], wUser.size() * sizeof(wchar_t));
    }
    if (!wDomain.empty()) {
        SecureZeroMemory(&wDomain[0], wDomain.size() * sizeof(wchar_t));
    }

    if (!packed) {
        return ReturnNoCredentialNotFinished(
            pi,
            pcpgsr,
            ppwszOptionalStatusText,
            kMsgSerializeNA,
            L"serialization build failed"
        );
    }

    wchar_t slog[128];
    swprintf_s(slog, L"packed ok (authPkg=%lu, cb=%u)", pk.authPkg, pk.size);
    CpLog(slog);

    pcpcs->rgbSerialization = pk.blob;
    pcpcs->cbSerialization = pk.size;
    pcpcs->ulAuthenticationPackage = pk.authPkg;
    pcpcs->clsidCredentialProvider = CLSID_testCPProvider;

    *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
    CpLog(L"AUTH_SUCCESS -> RETURN_CREDENTIAL_FINISHED");
    WaitAndCloseBrokerProcess(pi, 1000);
    return S_OK;
}

static HRESULT HandleParsedBrokerMessage(
    const Msg& m,
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    PROCESS_INFORMATION& pi,
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
    PWSTR* ppwszOptionalStatusText)
{
    if (m.type == "AUTH_FAIL") {
        return ReturnNoCredentialNotFinished(
            pi,
            pcpgsr,
            ppwszOptionalStatusText,
            kMsgAuthFail,
            L"AUTH_FAIL"
        );
    }

    if (m.type == "AUTH_FALLBACK") {
        return ReturnNoCredentialFinished(pi, pcpgsr, L"AUTH_FALLBACK");
    }

    if (m.type == "AUTH_SUCCESS") {
        return HandleAuthSuccessMessage(m, cpus, pi, pcpgsr, pcpcs, ppwszOptionalStatusText);
    }

    return ReturnNoCredentialNotFinished(
        pi,
        pcpgsr,
        ppwszOptionalStatusText,
        L"unknown message type",
        L"unknown message type"
    );
}

struct BrokerSessionState
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    bool connected = false;
};

static BrokerSessionState g_session;

static void UpdateBrokerFlowState(LONG state, PCWSTR transitionLog)
{
    LONG prev = InterlockedExchange(&g_lastBrokerFlowState, state);
    if (transitionLog && prev != state) {
        CpLog(transitionLog);
    }
}

static void ResetBrokerSession(DWORD waitMs)
{
    ClosePipeHandle(g_session.hPipe);
    WaitAndCloseBrokerProcess(g_session.pi, waitMs);
    ZeroMemory(&g_session.pi, sizeof(g_session.pi));
    g_session.connected = false;
    SetBrokerSessionActive(false);
    UpdateBrokerFlowState(0, nullptr);
}

void CloseBrokerSessionNow(DWORD waitMs, PCWSTR reasonLog)
{
    if (reasonLog && *reasonLog) {
        CpLog(reasonLog);
    }

    if (g_session.hPipe != INVALID_HANDLE_VALUE ||
        g_session.pi.hProcess ||
        g_session.pi.hThread ||
        g_session.connected)
    {
        ResetBrokerSession(waitMs);
    }
    else
    {
        SetBrokerSessionActive(false);
    }

    SetBrokerAwaitingFinal(false);
    UpdateBrokerFlowState(0, nullptr);
}

static HRESULT ReturnNoCredentialNotFinishedNoCleanup(
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    PWSTR* ppwszOptionalStatusText,
    PCWSTR statusText,
    PCWSTR logText)
{
    SetOptionalStatusText(ppwszOptionalStatusText, statusText);
    *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    if (logText) {
        CpLog(logText);
    }
    return S_OK;
}

HRESULT testCPCredential::GetSerialization(
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
    PWSTR* ppwszOptionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON* pcpsi)
{
    if (!pcpgsr || !pcpcs) return E_POINTER;
    ZeroMemory(pcpcs, sizeof(*pcpcs));
    if (pcpsi) *pcpsi = CPSI_NONE;
    if (ppwszOptionalStatusText) *ppwszOptionalStatusText = nullptr;

    if (g_session.hPipe == INVALID_HANDLE_VALUE || !g_session.pi.hProcess) {
        std::wstring pipeName = MakePipeName();
        g_session.hPipe = CreateRestrictedPipeServer(pipeName);
        if (g_session.hPipe == INVALID_HANDLE_VALUE) {
            SetOptionalStatusText(ppwszOptionalStatusText, kMsgPipeCreateFail);
            *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;

            DWORD err = GetLastError();
            wchar_t log[256];
            swprintf_s(log, L"pipe create failed (err=%u)", err);
            CpLog(log);
            return S_OK;
        }

        std::wstring err;
        if (!LaunchBroker(pipeName, g_session.pi, err)) {
            ResetBrokerSession(0);
            return ReturnNoCredentialNotFinishedNoCleanup(
                pcpgsr,
                ppwszOptionalStatusText,
                kMsgBrokerLaunchFail,
                L"broker launch failed"
            );
        }

        const DWORD CONNECT_TIMEOUT_MS = 2000;
        PipeConnectResult connectResult = WaitPipeClientConnect(g_session.hPipe, g_session.pi.hProcess, CONNECT_TIMEOUT_MS);
        if (connectResult != PipeConnectResult::Connected) {
            PROCESS_INFORMATION failedPi = g_session.pi;
            ZeroMemory(&g_session.pi, sizeof(g_session.pi));
            ClosePipeHandle(g_session.hPipe);
            g_session.connected = false;
            SetBrokerSessionActive(false);
            return ReturnNoCredentialNotFinished(
                failedPi,
                pcpgsr,
                ppwszOptionalStatusText,
                ConnectFailureStatusText(connectResult),
                ConnectFailureLogText(connectResult),
                500
            );
        }

        g_session.connected = true;
        SetBrokerSessionActive(true);
        SetBrokerAwaitingFinal(false);
        UpdateBrokerFlowState(0, nullptr);
        CpLog(L"GetSerialization: broker session started.");
    }

    std::string msgJson;
    Msg m{};
    SensitiveBrokerMessageGuard sensitiveGuard{ msgJson, m };
    PipeReadResult readResult = ReadPipeMessageUntilDone(g_session.hPipe, g_session.pi.hProcess, msgJson, 250, 1200);

    if (readResult == PipeReadResult::Timeout) {
        const DWORD nextDelayMs = IsBrokerAwaitingFinal() ? 300 : 2500;
        RequestCredentialsChangedAsync(nextDelayMs);
        return ReturnNoCredentialNotFinishedNoCleanup(
            pcpgsr,
            ppwszOptionalStatusText,
            nullptr,
            nullptr
        );
    }

    if (readResult != PipeReadResult::Ok || msgJson.empty()) {
        PROCESS_INFORMATION failedPi = g_session.pi;
        ZeroMemory(&g_session.pi, sizeof(g_session.pi));
        ClosePipeHandle(g_session.hPipe);
        g_session.connected = false;
        SetBrokerSessionActive(false);
        SetBrokerAwaitingFinal(false);
        UpdateBrokerFlowState(0, nullptr);
        LogBrokerExitCodeIfAvailable(failedPi, L"at readResult");

        if (readResult == PipeReadResult::BrokerExited || readResult == PipeReadResult::Failed)
        {
            WaitAndCloseBrokerProcess(failedPi, 0);
            ArmForceNextAutoSubmit();
            RequestCredentialsChangedAsync(50);
            return ReturnNoCredentialNotFinishedNoCleanup(
                pcpgsr,
                ppwszOptionalStatusText,
                nullptr,
                L"broker exited/disconnected -> auto recover"
            );
        }

        return ReturnNoCredentialNotFinished(
            failedPi,
            pcpgsr,
            ppwszOptionalStatusText,
            ReadFailureStatusText(readResult),
            ReadFailureLogText(readResult)
        );
    }

    if (!ParseJsonLine(msgJson, m)) {
        ResetBrokerSession(0);
        SetBrokerAwaitingFinal(false);
        UpdateBrokerFlowState(0, nullptr);
        return ReturnNoCredentialNotFinishedNoCleanup(
            pcpgsr,
            ppwszOptionalStatusText,
            kMsgParseFail,
            L"response parse failed"
        );
    }

    if (m.type == "AUTH_PENDING") {
        SetBrokerAwaitingFinal(true);
        UpdateBrokerFlowState(1, L"AUTH_PENDING -> waiting final response");
        RequestCredentialsChangedAsync(300);
        return ReturnNoCredentialNotFinishedNoCleanup(
            pcpgsr,
            ppwszOptionalStatusText,
            nullptr,
            nullptr
        );
    }

    if (m.type == "AUTH_IDLE") {
        SetBrokerAwaitingFinal(false);
        UpdateBrokerFlowState(2, L"AUTH_IDLE -> broker idle");
        RequestCredentialsChangedAsync(2500);
        return ReturnNoCredentialNotFinishedNoCleanup(
            pcpgsr,
            ppwszOptionalStatusText,
            nullptr,
            nullptr
        );
    }

    PROCESS_INFORMATION terminalPi = g_session.pi;
    ZeroMemory(&g_session.pi, sizeof(g_session.pi));
    ClosePipeHandle(g_session.hPipe);
    g_session.connected = false;
    SetBrokerSessionActive(false);
    SetBrokerAwaitingFinal(false);
    UpdateBrokerFlowState(0, nullptr);
    return HandleParsedBrokerMessage(m, _cpus, terminalPi, pcpgsr, pcpcs, ppwszOptionalStatusText);
}
