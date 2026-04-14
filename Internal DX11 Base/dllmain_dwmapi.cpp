// =============================================================================
// dllmain_dwmapi.cpp  –  dwmapi.dll 프록시 버전
// =============================================================================
#include "pch.h"
#include "helper.h"
#include <windows.h>

// 프록시 DLL 빌드 시 발생하는 경고/오류 억제
#pragma warning(disable: 4273) // dll linkage mismatch
#pragma warning(disable: 4995) // name was marked as #pragma deprecated

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

extern DWORD WINAPI MainThread_Initialize(LPVOID dwModule);
namespace DX11Base { void Shutdown(bool isTerminating); }

static HMODULE g_hOriginalDwm = NULL;

// ──────────────────────────────────────────────────────────
//  게임이 실제로 호출하는 DWM 함수 포인터 (썸네일 제외)
// ──────────────────────────────────────────────────────────
typedef HRESULT(WINAPI* tDwmIsCompositionEnabled)(BOOL*);
typedef HRESULT(WINAPI* tDwmEnableComposition)(UINT);
typedef HRESULT(WINAPI* tDwmExtendFrameIntoClientArea)(HWND, const MARGINS*);
typedef HRESULT(WINAPI* tDwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT(WINAPI* tDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT(WINAPI* tDwmFlush)(void);
typedef HRESULT(WINAPI* tDwmEnableBlurBehindWindow)(HWND, const DWM_BLURBEHIND*);
typedef BOOL   (WINAPI* tDwmDefWindowProc)(HWND, UINT, WPARAM, LPARAM, LRESULT*);
typedef HRESULT(WINAPI* tDwmEnableMMCSS)(BOOL);
typedef HRESULT(WINAPI* tDwmGetCompositionTimingInfo)(HWND, DWM_TIMING_INFO*);

static tDwmIsCompositionEnabled      fp_DwmIsCompositionEnabled      = NULL;
static tDwmEnableComposition         fp_DwmEnableComposition         = NULL;
static tDwmExtendFrameIntoClientArea fp_DwmExtendFrameIntoClientArea = NULL;
static tDwmGetWindowAttribute        fp_DwmGetWindowAttribute        = NULL;
static tDwmSetWindowAttribute        fp_DwmSetWindowAttribute        = NULL;
static tDwmFlush                     fp_DwmFlush                     = NULL;
static tDwmEnableBlurBehindWindow    fp_DwmEnableBlurBehindWindow    = NULL;
static tDwmDefWindowProc             fp_DwmDefWindowProc             = NULL;
static tDwmEnableMMCSS               fp_DwmEnableMMCSS               = NULL;
static tDwmGetCompositionTimingInfo  fp_DwmGetCompositionTimingInfo  = NULL;

// ──────────────────────────────────────────────────────────
//  크래시 로그 핸들러
// ──────────────────────────────────────────────────────────
static LONG WINAPI CheatUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    char buf[512]{};
    unsigned long long rip = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rip : 0ULL;
    int n = wsprintfA(buf,
        "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=%p RIP=0x%016llX\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        code, addr, rip);
    HANDLE h = CreateFileA("S8RPK_cheat_crash.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (n > 0) WriteFile(h, buf, (DWORD)n, &written, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// ──────────────────────────────────────────────────────────
//  원본 dwmapi.dll 로드
// ──────────────────────────────────────────────────────────
static void LoadOriginalDwm() {
    if (g_hOriginalDwm) return;

    char systemPath[MAX_PATH];
    GetSystemDirectoryA(systemPath, MAX_PATH);
    strcat_s(systemPath, "\\dwmapi.dll");
    g_hOriginalDwm = LoadLibraryA(systemPath);
    if (!g_hOriginalDwm) {
        MessageBoxA(0, "Failed to load original dwmapi.dll", "Error", MB_ICONERROR);
        return;
    }

#define LOAD_DWM(name) fp_##name = (t##name)GetProcAddress(g_hOriginalDwm, #name)
    LOAD_DWM(DwmIsCompositionEnabled);
    LOAD_DWM(DwmEnableComposition);
    LOAD_DWM(DwmExtendFrameIntoClientArea);
    LOAD_DWM(DwmGetWindowAttribute);
    LOAD_DWM(DwmSetWindowAttribute);
    LOAD_DWM(DwmFlush);
    LOAD_DWM(DwmEnableBlurBehindWindow);
    LOAD_DWM(DwmDefWindowProc);
    LOAD_DWM(DwmEnableMMCSS);
    LOAD_DWM(DwmGetCompositionTimingInfo);
#undef LOAD_DWM
}

// ──────────────────────────────────────────────────────────
//  초기화 (한 번만)
// ──────────────────────────────────────────────────────────
static void InitializeOnce() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    SetUnhandledExceptionFilter(CheatUnhandledExceptionFilter);

    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buf[256]{};
        int n = wsprintfA(buf,
            "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [System] dwmapi Proxy InitializeOnce triggered\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        HANDLE h = CreateFileA("S8RPK_cheat.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            if (n > 0) WriteFile(h, buf, (DWORD)n, &written, nullptr);
            CloseHandle(h);
        }
    }

    HANDLE hThread = CreateThread(nullptr, 0, MainThread_Initialize, DX11Base::g_hModule, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// ──────────────────────────────────────────────────────────
//  프록시 익스포트 함수들
// ──────────────────────────────────────────────────────────

// 창 생성 직후 가장 먼저 호출 → 여기서 치트 초기화 트리거
extern "C" __declspec(dllexport) HRESULT WINAPI DwmIsCompositionEnabled(BOOL* pfEnabled) {
    LoadOriginalDwm();
    InitializeOnce();
    if (!fp_DwmIsCompositionEnabled) { if (pfEnabled) *pfEnabled = TRUE; return S_OK; }
    return fp_DwmIsCompositionEnabled(pfEnabled);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmEnableComposition(UINT uCompositionAction) {
    LoadOriginalDwm(); InitializeOnce();
    if (!fp_DwmEnableComposition) return S_OK;
    return fp_DwmEnableComposition(uCompositionAction);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND hWnd, const MARGINS* pMarInset) {
    LoadOriginalDwm(); InitializeOnce();
    if (!fp_DwmExtendFrameIntoClientArea) return S_OK;
    return fp_DwmExtendFrameIntoClientArea(hWnd, pMarInset);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute) {
    LoadOriginalDwm(); InitializeOnce();
    if (!fp_DwmGetWindowAttribute) return E_FAIL;
    return fp_DwmGetWindowAttribute(hwnd, dwAttribute, pvAttribute, cbAttribute);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute) {
    LoadOriginalDwm(); InitializeOnce();
    if (!fp_DwmSetWindowAttribute) return E_FAIL;
    return fp_DwmSetWindowAttribute(hwnd, dwAttribute, pvAttribute, cbAttribute);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmFlush(void) {
    LoadOriginalDwm();
    if (!fp_DwmFlush) return S_OK;
    return fp_DwmFlush();
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hWnd, const DWM_BLURBEHIND* pBlurBehind) {
    LoadOriginalDwm();
    if (!fp_DwmEnableBlurBehindWindow) return S_OK;
    return fp_DwmEnableBlurBehindWindow(hWnd, pBlurBehind);
}

extern "C" __declspec(dllexport) BOOL WINAPI DwmDefWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) {
    LoadOriginalDwm();
    if (!fp_DwmDefWindowProc) return FALSE;
    return fp_DwmDefWindowProc(hWnd, msg, wParam, lParam, plResult);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmEnableMMCSS(BOOL fEnableMMCSS) {
    LoadOriginalDwm();
    if (!fp_DwmEnableMMCSS) return S_OK;
    return fp_DwmEnableMMCSS(fEnableMMCSS);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DwmGetCompositionTimingInfo(HWND hwnd, DWM_TIMING_INFO* pTimingInfo) {
    LoadOriginalDwm();
    if (!fp_DwmGetCompositionTimingInfo) return E_FAIL;
    return fp_DwmGetCompositionTimingInfo(hwnd, pTimingInfo);
}

// ──────────────────────────────────────────────────────────
//  DllMain
// ──────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DX11Base::g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        DX11Base::Shutdown(lpReserved != NULL);
        if (g_hOriginalDwm)
            FreeLibrary(g_hOriginalDwm);
        break;
    }
    return TRUE;
}
