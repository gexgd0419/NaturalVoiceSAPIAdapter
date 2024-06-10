// Shim functions for unsupported APIs
// As the entire references to system DLLs such as Kernel32.dll will be changed to this DLL,
// this DLL has to provide ALL export functions of the original DLLs.
// APIs supported on all systems (e.g. CreateFileW) are just redirected to the original DLLs.
// Most of the others are handled by YY-Thunks, only a few are written down below.
// Inspired by the vista2xp project

#include "framework.h"
#include <appmodel.h>
#include <ncrypt.h>

class HModule
{
    HMODULE h;
public:
    explicit HModule(LPCWSTR lib) : h(LoadLibraryW(lib)) {}
    ~HModule() { FreeLibrary(h); }
    operator HMODULE() const { return h; }
};

#define DEFINE_PFN(hModule, Proc) static const auto pfn##Proc = (decltype(Proc)*)GetProcAddress(hModule, #Proc)

static const HModule hKernel32(L"kernel32");
DEFINE_PFN(hKernel32, GetPackageFamilyName);
DEFINE_PFN(hKernel32, LoadPackagedLibrary);

static const HModule hNcrypt(L"ncrypt");
DEFINE_PFN(hNcrypt, NCryptOpenStorageProvider);
DEFINE_PFN(hNcrypt, NCryptImportKey);
DEFINE_PFN(hNcrypt, NCryptFreeObject);

LONG WINAPI Shim_GetPackageFamilyName(
    _In_ HANDLE hProcess,
    _Inout_ UINT32* packageFamilyNameLength,
    _Out_writes_opt_(*packageFamilyNameLength) PWSTR packageFamilyName
)
{
    if (pfnGetPackageFamilyName)
        return pfnGetPackageFamilyName(hProcess, packageFamilyNameLength, packageFamilyName);
    return APPMODEL_ERROR_NO_PACKAGE;
}

_Ret_maybenull_
HMODULE WINAPI Shim_LoadPackagedLibrary(
    _In_       LPCWSTR lpwLibFileName,
    _Reserved_ DWORD Reserved
)
{
    if (pfnLoadPackagedLibrary)
        return pfnLoadPackagedLibrary(lpwLibFileName, Reserved);
    SetLastError(APPMODEL_ERROR_NO_PACKAGE);
    return nullptr;
}

SECURITY_STATUS WINAPI Shim_NCryptOpenStorageProvider(
    _Out_   NCRYPT_PROV_HANDLE* phProvider,
    _In_opt_ LPCWSTR pszProviderName,
    _In_    DWORD   dwFlags)
{
    if (pfnNCryptOpenStorageProvider)
        return pfnNCryptOpenStorageProvider(phProvider, pszProviderName, dwFlags);
    return ERROR_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI Shim_NCryptImportKey(
    _In_    NCRYPT_PROV_HANDLE hProvider,
    _In_opt_ NCRYPT_KEY_HANDLE hImportKey,
    _In_    LPCWSTR pszBlobType,
    _In_opt_ NCryptBufferDesc* pParameterList,
    _Out_   NCRYPT_KEY_HANDLE* phKey,
    _In_reads_bytes_(cbData) PBYTE pbData,
    _In_    DWORD   cbData,
    _In_    DWORD   dwFlags)
{
    if (pfnNCryptImportKey)
        return pfnNCryptImportKey(hProvider, hImportKey, pszBlobType, pParameterList, phKey, pbData, cbData, dwFlags);
    return ERROR_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI Shim_NCryptFreeObject(
    _In_    NCRYPT_HANDLE hObject)
{
    if (pfnNCryptFreeObject)
        return pfnNCryptFreeObject(hObject);
    return ERROR_NOT_SUPPORTED;
}