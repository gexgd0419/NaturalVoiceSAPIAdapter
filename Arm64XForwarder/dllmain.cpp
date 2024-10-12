// This is a forwarder DLL that forwards function calls to either the x64 version or the ARM64 version
// of the main NaturalVoiceSAPIAdapter.dll, depending on the architecture of the process that loads this DLL.
// This DLL project will be built with ARM64 and ARM64EC configurations, then merged together to form an ARM64X DLL.
// More about ARM64X DLLs: https://learn.microsoft.com/windows/arm/arm64x-build
// Note that we didn't choose to use a simple "pure forwarder DLL" described in the link above,
// because the x64 version and the ARM64 version aren't in the same folder.
// Unfortunately, ARM64EC processes have to load the x64 version,
// because there isn't an ARM64EC version for the Azure Speech SDK yet.

#include "framework.h"

HMODULE hThisDll = nullptr, hTargetDll = nullptr;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        hThisDll = hModule;
        break;

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr)
        {
            if (hTargetDll)
            {
                FreeLibrary(hTargetDll);
                hTargetDll = nullptr;
            }
        }
        break;
    }
    return TRUE;
}

