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

