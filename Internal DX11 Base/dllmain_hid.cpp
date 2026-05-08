// =============================================================================
// dllmain_hid.cpp  –  hid.dll 프래그마 포워딩 버전
// =============================================================================
#include "pch.h"
#include "helper.h"
#include <windows.h>

// [중요] 링크 에러 방지
#pragma warning(disable: 4273)

// [중요] 모든 시스템 호출을 원본 hid.dll로 직접 전달 (링커 포워딩)
#pragma comment(linker, "/export:HidD_FlushQueue=C:\\Windows\\System32\\hid.HidD_FlushQueue")
#pragma comment(linker, "/export:HidD_FreePreparsedData=C:\\Windows\\System32\\hid.HidD_FreePreparsedData")
#pragma comment(linker, "/export:HidD_GetAttributes=C:\\Windows\\System32\\hid.HidD_GetAttributes")
#pragma comment(linker, "/export:HidD_GetConfiguration=C:\\Windows\\System32\\hid.HidD_GetConfiguration")
#pragma comment(linker, "/export:HidD_GetFeature=C:\\Windows\\System32\\hid.HidD_GetFeature")
#pragma comment(linker, "/export:HidD_GetHidGuid=C:\\Windows\\System32\\hid.HidD_GetHidGuid")
#pragma comment(linker, "/export:HidD_GetInputReport=C:\\Windows\\System32\\hid.HidD_GetInputReport")
#pragma comment(linker, "/export:HidD_GetIndexedString=C:\\Windows\\System32\\hid.HidD_GetIndexedString")
#pragma comment(linker, "/export:HidD_GetManufacturerString=C:\\Windows\\System32\\hid.HidD_GetManufacturerString")
#pragma comment(linker, "/export:HidD_GetMsGenreDescriptor=C:\\Windows\\System32\\hid.HidD_GetMsGenreDescriptor")
#pragma comment(linker, "/export:HidD_GetNumInputBuffers=C:\\Windows\\System32\\hid.HidD_GetNumInputBuffers")
#pragma comment(linker, "/export:HidD_GetPhysicalDescriptor=C:\\Windows\\System32\\hid.HidD_GetPhysicalDescriptor")
#pragma comment(linker, "/export:HidD_GetPreparsedData=C:\\Windows\\System32\\hid.HidD_GetPreparsedData")
#pragma comment(linker, "/export:HidD_GetProductString=C:\\Windows\\System32\\hid.HidD_GetProductString")
#pragma comment(linker, "/export:HidD_GetSerialNumberString=C:\\Windows\\System32\\hid.HidD_GetSerialNumberString")
#pragma comment(linker, "/export:HidD_Hello=C:\\Windows\\System32\\hid.HidD_Hello")
#pragma comment(linker, "/export:HidD_SetConfiguration=C:\\Windows\\System32\\hid.HidD_SetConfiguration")
#pragma comment(linker, "/export:HidD_SetFeature=C:\\Windows\\System32\\hid.HidD_SetFeature")
#pragma comment(linker, "/export:HidD_SetNumInputBuffers=C:\\Windows\\System32\\hid.HidD_SetNumInputBuffers")
#pragma comment(linker, "/export:HidD_SetOutputReport=C:\\Windows\\System32\\hid.HidD_SetOutputReport")
#pragma comment(linker, "/export:HidP_GetCaps=C:\\Windows\\System32\\hid.HidP_GetCaps")
#pragma comment(linker, "/export:HidP_GetButtonCaps=C:\\Windows\\System32\\hid.HidP_GetButtonCaps")
#pragma comment(linker, "/export:HidP_GetValueCaps=C:\\Windows\\System32\\hid.HidP_GetValueCaps")
#pragma comment(linker, "/export:HidP_GetExtendedAttributes=C:\\Windows\\System32\\hid.HidP_GetExtendedAttributes")
#pragma comment(linker, "/export:HidP_GetUsageValue=C:\\Windows\\System32\\hid.HidP_GetUsageValue")
#pragma comment(linker, "/export:HidP_GetScaledUsageValue=C:\\Windows\\System32\\hid.HidP_GetScaledUsageValue")
#pragma comment(linker, "/export:HidP_GetUsageValueArray=C:\\Windows\\System32\\hid.HidP_GetUsageValueArray")
#pragma comment(linker, "/export:HidP_GetButtons=C:\\Windows\\System32\\hid.HidP_GetButtons")
#pragma comment(linker, "/export:HidP_GetButtonsEx=C:\\Windows\\System32\\hid.HidP_GetButtonsEx")
#pragma comment(linker, "/export:HidP_GetUsageAndPageList=C:\\Windows\\System32\\hid.HidP_GetUsageAndPageList")
#pragma comment(linker, "/export:HidP_GetSpecificButtonCaps=C:\\Windows\\System32\\hid.HidP_GetSpecificButtonCaps")
#pragma comment(linker, "/export:HidP_GetSpecificValueCaps=C:\\Windows\\System32\\hid.HidP_GetSpecificValueCaps")
#pragma comment(linker, "/export:HidP_InitializeReportForID=C:\\Windows\\System32\\hid.HidP_InitializeReportForID")
#pragma comment(linker, "/export:HidP_SetUsageValue=C:\\Windows\\System32\\hid.HidP_SetUsageValue")
#pragma comment(linker, "/export:HidP_SetScaledUsageValue=C:\\Windows\\System32\\hid.HidP_SetScaledUsageValue")
#pragma comment(linker, "/export:HidP_SetUsageValueArray=C:\\Windows\\System32\\hid.HidP_SetUsageValueArray")
#pragma comment(linker, "/export:HidP_SetButtons=C:\\Windows\\System32\\hid.HidP_SetButtons")
#pragma comment(linker, "/export:HidP_UnsetButtons=C:\\Windows\\System32\\hid.HidP_UnsetButtons")
#pragma comment(linker, "/export:HidP_TranslateUsageAndPagesToI8042ScanCodes=C:\\Windows\\System32\\hid.HidP_TranslateUsageAndPagesToI8042ScanCodes")
#pragma comment(linker, "/export:HidP_UsageListDifference=C:\\Windows\\System32\\hid.HidP_UsageListDifference")

extern DWORD WINAPI MainThread_Initialize(LPVOID dwModule);
namespace DX11Base { void Shutdown(bool isTerminating); }

static void InitializeOnce() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // 치트 로딩 스레드 실행
    HANDLE hThread = CreateThread(nullptr, 0, MainThread_Initialize, DX11Base::g_hModule, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// -----------------------------------------------------------------------------
// DllMain
// -----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DX11Base::g_hModule = hModule;
        InitializeOnce();
        break;
    case DLL_PROCESS_DETACH:
        DX11Base::Shutdown(lpReserved != NULL);
        break;
    }
    return TRUE;
}
