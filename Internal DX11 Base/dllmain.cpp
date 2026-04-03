#include "pch.h"
#include "helper.h"

// --- ?ш린瑜?異붽??섏꽭??---
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
// -----------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  dwCallReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);
    if (dwCallReason == DLL_PROCESS_ATTACH)
    {
        DX11Base::g_hModule = hModule;

        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(0, 0, MainThread_Initialize, DX11Base::g_hModule, 0, 0);

        if (hThread)
            CloseHandle(hThread);
    }

    return TRUE;
}

