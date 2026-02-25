#include "pch.h"
#include "Provider.h"

IFACEMETHODIMP testCPCredential::GetFieldState(
    DWORD dwFieldID,
    CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis)
{
    if (!pcpfs || !pcpfis) return E_POINTER;
    if (dwFieldID >= FI_NUM_FIELDS) return E_INVALIDARG;

    switch (dwFieldID) {
    case FI_TITLE:
        *pcpfs = CPFS_DISPLAY_IN_SELECTED_TILE;
        *pcpfis = CPFIS_NONE;
        break;
    case FI_SUBMIT:
        *pcpfs = CPFS_HIDDEN;
        *pcpfis = CPFIS_NONE;
        break;
    default:
        *pcpfs = CPFS_HIDDEN;
        *pcpfis = CPFIS_NONE;
        break;
    }
    return S_OK;
}

IFACEMETHODIMP testCPCredential::GetStringValue(DWORD dwFieldID, PWSTR* ppwsz)
{
    if (!ppwsz) return E_POINTER;
    *ppwsz = nullptr;
    if (dwFieldID == FI_TITLE) {
        size_t cch = wcslen(kCpTitleText) + 1;
        *ppwsz = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
        if (!*ppwsz) return E_OUTOFMEMORY;
        wcscpy_s(*ppwsz, cch, kCpTitleText);
        return S_OK;
    }
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::GetBitmapValue(DWORD, HBITMAP* phbmp)
{
    if (phbmp) *phbmp = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::GetCheckboxValue(DWORD, BOOL* pbChecked, PWSTR* ppwszLabel)
{
    if (pbChecked) *pbChecked = FALSE;
    if (ppwszLabel) *ppwszLabel = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::GetSubmitButtonValue(DWORD dwFieldID, DWORD* pdwAdjacentTo)
{
    if (!pdwAdjacentTo) return E_POINTER;
    if (dwFieldID == FI_SUBMIT) { *pdwAdjacentTo = FI_TITLE; return S_OK; }
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::GetComboBoxValueCount(DWORD, DWORD* pcItems, DWORD* pdwSelectedItem)
{
    if (pcItems) *pcItems = 0;
    if (pdwSelectedItem) *pdwSelectedItem = 0;
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::GetComboBoxValueAt(DWORD, DWORD, PWSTR* ppwszItem)
{
    if (ppwszItem) *ppwszItem = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::SetComboBoxSelectedValue(DWORD, DWORD)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::SetStringValue(DWORD, PCWSTR)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::SetCheckboxValue(DWORD, BOOL)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP testCPCredential::CommandLinkClicked(DWORD)
{
    return E_NOTIMPL;
}
