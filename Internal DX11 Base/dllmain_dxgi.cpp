// =============================================================================
// dllmain_dxgi.cpp  –  최종 안정화 버전 (DEF 파일 매핑 방식)
// =============================================================================
#include "pch.h"
#include "helper.h"
#include <windows.h>

// [중요] 모든 비핵심 호출은 원본 dxgi.dll로 직접 전달 (링커 포워딩)
#pragma comment(linker, "/export:ApplyCompatResolution=C:\\Windows\\System32\\dxgi.ApplyCompatResolution")
#pragma comment(linker, "/export:CompatString=C:\\Windows\\System32\\dxgi.CompatString")
#pragma comment(linker, "/export:CompatValue=C:\\Windows\\System32\\dxgi.CompatValue")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=C:\\Windows\\System32\\dxgi.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIDumpJournal=C:\\Windows\\System32\\dxgi.DXGIDumpJournal")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface1")
#pragma comment(linker, "/export:DXGIGetDebugInterface=C:\\Windows\\System32\\dxgi.DXGIGetDebugInterface")
#pragma comment(linker, "/export:DXGIReportAdapterConfiguration=C:\\Windows\\System32\\dxgi.DXGIReportAdapterConfiguration")
#pragma comment(linker, "/export:PIXBeginCapture=C:\\Windows\\System32\\dxgi.PIXBeginCapture")
#pragma comment(linker, "/export:PIXEndCapture=C:\\Windows\\System32\\dxgi.PIXEndCapture")
#pragma comment(linker, "/export:PIXGetCaptureState=C:\\Windows\\System32\\dxgi.PIXGetCaptureState")
#pragma comment(linker, "/export:SetAppCompatString=C:\\Windows\\System32\\dxgi.SetAppCompatString")
#pragma comment(linker, "/export:SetAppCompatValue=C:\\Windows\\System32\\dxgi.SetAppCompatValue")

// 가로챌 함수 포인터 타입 정의
typedef HRESULT (WINAPI* tCreateDXGIFactory)(REFIID, void**);
typedef HRESULT (WINAPI* tCreateDXGIFactory1)(REFIID, void**);
typedef HRESULT (WINAPI* tCreateDXGIFactory2)(UINT, REFIID, void**);

static HMODULE g_hOriginalDxgi = NULL;
static tCreateDXGIFactory  oCreateDXGIFactory  = NULL;
static tCreateDXGIFactory1 oCreateDXGIFactory1 = NULL;
static tCreateDXGIFactory2 oCreateDXGIFactory2 = NULL;

static void InitializeOnce() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // 치트 로딩 스레드 실행 (Source.cpp::MainThread_Initialize에서 5초 대기)
    HANDLE hThread = CreateThread(nullptr, 0, MainThread_Initialize, DX11Base::g_hModule, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

static void LoadOriginal() {
    if (g_hOriginalDxgi) return;
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\dxgi.dll");
    g_hOriginalDxgi = LoadLibraryA(path);
    if (g_hOriginalDxgi) {
        oCreateDXGIFactory  = (tCreateDXGIFactory)GetProcAddress(g_hOriginalDxgi, "CreateDXGIFactory");
        oCreateDXGIFactory1 = (tCreateDXGIFactory1)GetProcAddress(g_hOriginalDxgi, "CreateDXGIFactory1");
        oCreateDXGIFactory2 = (tCreateDXGIFactory2)GetProcAddress(g_hOriginalDxgi, "CreateDXGIFactory2");
    }
}

// 명칭 충돌을 피하기 위해 Proxy_ 접두사 사용 (DEF 파일에서 최종 매핑함)
extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    LoadOriginal();
    InitializeOnce();
    return oCreateDXGIFactory ? oCreateDXGIFactory(riid, ppFactory) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    LoadOriginal();
    InitializeOnce();
    return oCreateDXGIFactory1 ? oCreateDXGIFactory1(riid, ppFactory) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    LoadOriginal();
    InitializeOnce();
    return oCreateDXGIFactory2 ? oCreateDXGIFactory2(Flags, riid, ppFactory) : E_FAIL;
}

namespace DX11Base { void Shutdown(bool isTerminating); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DX11Base::g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        // lpReserved가 NULL이 아니면 프로세스 종료(Terminate) 상황임
        DX11Base::Shutdown(lpReserved != NULL);
        break;
    }
    return TRUE;
}
