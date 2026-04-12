#include "pch.h"
#include "helper.h"
#include <windows.h>

extern DWORD WINAPI MainThread_Initialize(LPVOID dwModule);

// 원본 dinput8.dll 핸들
static HMODULE g_hOriginalDll = NULL;

// 원본 함수 포인터
typedef HRESULT(WINAPI *tDirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut,
                                             LPUNKNOWN punkOuter);

static tDirectInput8Create g_OriginalDirectInput8Create = NULL;

// ------------------------------
// 크래시 로그 핸들러
// ------------------------------
static LONG WINAPI CheatUnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
  SYSTEMTIME st{};
  GetLocalTime(&st);

  DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
  void *addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;

  char buf[512]{};
#if defined(_M_X64) || defined(__x86_64__)
  unsigned long long rip = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rip : 0ULL;
  unsigned long long rsp = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rsp : 0ULL;
  unsigned long long rbp = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rbp : 0ULL;
  int n = wsprintfA(
      buf,
      "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=%p RIP=0x%016llX RSP=0x%016llX RBP=0x%016llX\r\n",
      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, code, addr, rip, rsp, rbp);
#else
  int n = wsprintfA(buf, "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=%p\r\n", st.wYear, st.wMonth,
                    st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, code, addr);
#endif
  HANDLE h = CreateFileA("S8RPK_cheat_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (n > 0)
      WriteFile(h, buf, (DWORD)n, &written, nullptr);
    CloseHandle(h);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// ------------------------------
// 원본 dinput8.dll 로드
// ------------------------------
static void LoadOriginalDll() {
  if (g_hOriginalDll)
    return;

  char systemPath[MAX_PATH];
  GetSystemDirectoryA(systemPath, MAX_PATH);
  strcat_s(systemPath, "\\dinput8.dll");

  g_hOriginalDll = LoadLibraryA(systemPath);

  if (!g_hOriginalDll) {
    MessageBoxA(0, "Failed to load original dinput8.dll", "Error", MB_ICONERROR);
    return;
  }

  g_OriginalDirectInput8Create = (tDirectInput8Create)GetProcAddress(g_hOriginalDll, "DirectInput8Create");
}

// ------------------------------
// 안전한 초기화 (한 번만 실행, DllMain 밖에서 호출)
// ------------------------------
static void InitializeOnce() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;

  // 크래시 핸들러 등록
  SetUnhandledExceptionFilter(CheatUnhandledExceptionFilter);

  // 초기화 로그
  {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[256]{};
    int n = wsprintfA(buf, "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [System] dinput8 Proxy InitializeOnce triggered\r\n",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    HANDLE h = CreateFileA("S8RPK_cheat.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      if (n > 0)
        WriteFile(h, buf, (DWORD)n, &written, nullptr);
      CloseHandle(h);
    }
  }

  // 메인 초기화 스레드 생성 (DllMain 밖이므로 안전)
  HANDLE hThread = CreateThread(nullptr, 0, MainThread_Initialize, DX11Base::g_hModule, 0, nullptr);
  if (hThread)
    CloseHandle(hThread);
}

// ------------------------------
// 프록시 함수
// ------------------------------
extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
                                                                   LPVOID *ppvOut, LPUNKNOWN punkOuter) {
  LoadOriginalDll();

  // DllMain 대신 여기서 초기화 (loader lock 이후 안전한 시점)
  InitializeOnce();

  if (!g_OriginalDirectInput8Create)
    return E_FAIL;

  return g_OriginalDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

// ------------------------------
// DllMain (최소화 - loader lock 내에서는 최소 작업만)
// ------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hModule);
    // g_hModule 저장 (InitializeOnce에서 MainThread_Initialize로 전달됨)
    // LoadLibraryA는 loader lock 위험이 있으므로 DirectInput8Create 시점으로 연기
    DX11Base::g_hModule = hModule;
    break;
  case DLL_PROCESS_DETACH:
    if (g_hOriginalDll)
      FreeLibrary(g_hOriginalDll);
    break;
  }
  return TRUE;
}

#if false
#include "pch.h"
#include "helper.h"
#include <windows.h>

// --- dinput8.dll 프록시 익스포트 연동 (시스템 dinput8.dll로 포워딩) ---
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
// ------------------------------------

static LONG WINAPI CheatUnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
    // __try/__except 는 C++ 소멸자(unwind)와 같이 쓰면 C2712가 나므로
    // WinAPI + POD만 사용해서 안전하게 최소 정보만 기록합니다.
    SYSTEMTIME st{};
    GetLocalTime(&st);

    DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    void *addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    char buf[512]{};
#if defined(_M_X64) || defined(__x86_64__)
    unsigned long long rip = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rip : 0ULL;
    unsigned long long rsp = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rsp : 0ULL;
    unsigned long long rbp = (ep && ep->ContextRecord) ? (unsigned long long)ep->ContextRecord->Rbp : 0ULL;
    int n = wsprintfA(buf,
                      "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=%p RIP=0x%016llX RSP=0x%016llX RBP=0x%016llX\r\n",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, code, addr, rip, rsp,
                      rbp);
#else
    int n = wsprintfA(buf,
                      "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=%p\r\n",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, code, addr);
#endif

    HANDLE h = CreateFileA("S8RPK_cheat_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (n > 0)
            WriteFile(h, buf, (DWORD)n, &written, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  dwCallReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);
    if (dwCallReason == DLL_PROCESS_ATTACH)
    {
        DX11Base::g_hModule = hModule;
        SetUnhandledExceptionFilter(CheatUnhandledExceptionFilter);

        // --- 아주 초기 로딩 확인 로그 (데이터 포함) ---
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            char buf[256]{};
            int n = wsprintfA(buf,
                              "\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u] [System] dinput8.dll Proxy Loaded (DLL_PROCESS_ATTACH)\r\n",
                              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            HANDLE h = CreateFileA("S8RPK_cheat.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                if (n > 0)
                    WriteFile(h, buf, (DWORD)n, &written, nullptr);
                CloseHandle(h);
            }
        }

        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(0, 0, MainThread_Initialize, DX11Base::g_hModule, 0, 0);

        if (hThread)
            CloseHandle(hThread);
    }

    return TRUE;
}

#endif