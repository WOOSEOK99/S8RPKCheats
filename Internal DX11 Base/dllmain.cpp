#include "pch.h"
#include "helper.h"
#include <fstream>
#include <chrono>
#include <iomanip>

// --- dinput8.dll 프록시 익스포트 연동 (시스템 dinput8.dll로 포워딩) ---
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
// ------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  dwCallReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);
    if (dwCallReason == DLL_PROCESS_ATTACH)
    {
        DX11Base::g_hModule = hModule;

        // --- 아주 초기 로딩 확인 로그 (데이터 포함) ---
        std::ofstream logFile("S8RPK_cheat.log", std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            struct tm tm_info;
            localtime_s(&tm_info, &now);
            logFile << "\n[" << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S") << "] [System] dinput8.dll Proxy Loaded (DLL_PROCESS_ATTACH)" << std::endl;
            logFile.close();
        }

        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(0, 0, MainThread_Initialize, DX11Base::g_hModule, 0, 0);

        if (hThread)
            CloseHandle(hThread);
    }

    return TRUE;
}

