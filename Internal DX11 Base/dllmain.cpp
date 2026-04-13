// =============================================================================
// dllmain.cpp  –  dinput8.dll 프래그마 포워딩 버전 (통합 안정화)
// =============================================================================
#include "pch.h"
#include "helper.h"
#include <windows.h>

// [중요] 링크 에러 방지
#pragma warning(disable: 4273)

// [중요] 모든 시스템 호출을 원본 dinput8.dll로 직접 전달 (링커 포워딩)
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow")
#pragma comment(linker, "/export:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject")
#pragma comment(linker, "/export:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer")
#pragma comment(linker, "/export:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer")
#pragma comment(linker, "/export:GetdfDIJoystick=C:\\Windows\\System32\\dinput8.GetdfDIJoystick")

extern DWORD WINAPI MainThread_Initialize(LPVOID dwModule);

static void InitializeOnce() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // 치트 로딩 스레드 실행 (Source.cpp::MainThread_Initialize에서 5초 대기 수행)
    HANDLE hThread = CreateThread(nullptr, 0, MainThread_Initialize, DX11Base::g_hModule, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// -----------------------------------------------------------------------------
// DllMain: 로더 락(Loader Lock) 상태이므로 최소한의 작업만 수행
// -----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DX11Base::g_hModule = hModule;
        // dinput8.dll은 로드 즉시 초기화 트리거 가능 (포워딩이 링커 수준에서 처리됨)
        InitializeOnce();
        break;
    }
    return TRUE;
}