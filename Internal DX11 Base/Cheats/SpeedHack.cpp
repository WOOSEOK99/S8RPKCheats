#include "SpeedHack.h"
#include "../Hooking/MinHook.h"
#include "../MenuState.h"
#include <atomic>
#include <windows.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)
#pragma comment(lib, "winmm.lib")

namespace DX11Base {

  float g_speedMultiplier = 1.0f;
  bool bSpeedHack = false;

  namespace {
    static bool s_installed = false;

    // 원본 API 포인터들
    typedef DWORD(WINAPI *PtimeGetTime)();
    static PtimeGetTime otimeGetTime = nullptr;

    typedef DWORD(WINAPI *PGetTickCount)();
    static PGetTickCount oGetTickCount = nullptr;

    typedef ULONGLONG(WINAPI *PGetTickCount64)();
    static PGetTickCount64 oGetTickCount64 = nullptr;

    typedef BOOL(WINAPI *PQueryPerformanceCounter)(LARGE_INTEGER *);
    static PQueryPerformanceCounter oQueryPerformanceCounter = nullptr;

    // 베이스 시간 기록용
    static std::atomic<DWORD> s_tgtReal{0};
    static std::atomic<DWORD> s_tgtFake{0};

    static std::atomic<DWORD> s_gtcReal{0};
    static std::atomic<DWORD> s_gtcFake{0};

    static std::atomic<ULONGLONG> s_gtc64Real{0};
    static std::atomic<ULONGLONG> s_gtc64Fake{0};

    static std::atomic<LONGLONG> s_qpcReal{0};
    static std::atomic<LONGLONG> s_qpcFake{0};
    
    static std::atomic<float> s_multiplier{1.0f};

    // 속도 변경 시 끊김(점프) 방지를 위해 기준시간을 현재 시간으로 재설정
    void ResetBases(float currentMul, float desiredMul) {
      if (otimeGetTime) {
        DWORD real = otimeGetTime();
        DWORD fake = s_tgtFake.load() + (DWORD)((real - s_tgtReal.load()) * currentMul);
        s_tgtReal.store(real);
        s_tgtFake.store(fake);
      }
      if (oGetTickCount) {
        DWORD real = oGetTickCount();
        DWORD fake = s_gtcFake.load() + (DWORD)((real - s_gtcReal.load()) * currentMul);
        s_gtcReal.store(real);
        s_gtcFake.store(fake);
      }
      if (oGetTickCount64) {
        ULONGLONG real = oGetTickCount64();
        ULONGLONG fake = s_gtc64Fake.load() + (ULONGLONG)((real - s_gtc64Real.load()) * currentMul);
        s_gtc64Real.store(real);
        s_gtc64Fake.store(fake);
      }
      if (oQueryPerformanceCounter) {
        LARGE_INTEGER li;
        oQueryPerformanceCounter(&li);
        LONGLONG fake = s_qpcFake.load() + (LONGLONG)((li.QuadPart - s_qpcReal.load()) * currentMul);
        s_qpcReal.store(li.QuadPart);
        s_qpcFake.store(fake);
      }
      
      s_multiplier.store(desiredMul);
    }

    // 호출자가 게임 실행 파일 내부인지 확인 (RTSS, 스팀 오버레이 등과의 충돌 방지)
    bool __forceinline IsCallerGame(void* caller) {
      static uintptr_t s_exeBase = 0;
      static uintptr_t s_exeEnd = 0;
      if (s_exeBase == 0) {
        HMODULE hExe = GetModuleHandle(NULL);
        if (hExe) {
          PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
          PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uint8_t*)hExe + dosHeader->e_lfanew);
          s_exeBase = (uintptr_t)hExe;
          s_exeEnd = s_exeBase + ntHeaders->OptionalHeader.SizeOfImage;
        }
      }
      uintptr_t addr = (uintptr_t)caller;
      return (addr >= s_exeBase && addr < s_exeEnd);
    }
  } // namespace

  // -----------------------------------------
  // Time API Hooks
  // -----------------------------------------
  DWORD WINAPI hktimeGetTime() {
    void* caller = _ReturnAddress();
    DWORD real = otimeGetTime();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f || !IsCallerGame(caller)) 
      return real;
    return s_tgtFake.load(std::memory_order_relaxed) + (DWORD)((real - s_tgtReal.load(std::memory_order_relaxed)) * mul);
  }

  DWORD WINAPI hkGetTickCount() {
    void* caller = _ReturnAddress();
    DWORD real = oGetTickCount();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f || !IsCallerGame(caller)) 
      return real;
    return s_gtcFake.load(std::memory_order_relaxed) + (DWORD)((real - s_gtcReal.load(std::memory_order_relaxed)) * mul);
  }

  ULONGLONG WINAPI hkGetTickCount64() {
    void* caller = _ReturnAddress();
    ULONGLONG real = oGetTickCount64();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f || !IsCallerGame(caller)) 
      return real;
    return s_gtc64Fake.load(std::memory_order_relaxed) + (ULONGLONG)((real - s_gtc64Real.load(std::memory_order_relaxed)) * mul);
  }

  BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    void* caller = _ReturnAddress();
    BOOL ret = oQueryPerformanceCounter(lpPerformanceCount);
    if (ret) {
      float mul = s_multiplier.load(std::memory_order_relaxed);
      if (bSpeedHack && mul != 1.0f && IsCallerGame(caller)) {
        LONGLONG real = lpPerformanceCount->QuadPart;
        LONGLONG delta = real - s_qpcReal.load(std::memory_order_relaxed);
        lpPerformanceCount->QuadPart = s_qpcFake.load(std::memory_order_relaxed) + (LONGLONG)(delta * mul);
      }
    }
    return ret;
  }

  // -----------------------------------------
  // 설치 함수 (Engine.cpp 에서 호출)
  // -----------------------------------------
  void SpeedHack_Sleep_Install() {
    if (s_installed)
      return;

    HMODULE hWinmm = GetModuleHandle(L"winmm.dll");
    if (!hWinmm) hWinmm = LoadLibrary(L"winmm.dll");
    
    // Create Hooks (QueryPerformanceCounter가 현대 게임 속도의 핵심)
    MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", &hkQueryPerformanceCounter, (LPVOID *)&oQueryPerformanceCounter);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount", &hkGetTickCount, (LPVOID *)&oGetTickCount);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", &hkGetTickCount64, (LPVOID *)&oGetTickCount64);
    if (hWinmm) {
      MH_CreateHookApi(L"winmm.dll", "timeGetTime", &hktimeGetTime, (LPVOID *)&otimeGetTime);
    }

    MH_EnableHook(MH_ALL_HOOKS);

    // 초기 기준값 캡처 (배율 1.0f인 상태로)
    ResetBases(1.0f, g_speedMultiplier);

    s_installed = true;
  }

  void SpeedHack_Init() {
    SpeedHack_Sleep_Install();
  }
  
  void SpeedHack_Update() {
    if (!s_installed) {
      SpeedHack_Init();
    }
    
    float desired = bSpeedHack ? g_speedMultiplier : 1.0f;
    float current = s_multiplier.load();
    
    if (current != desired) {
      // 속도가 바뀔 때 시공간의 끊김을 막기 위해 기준시를 현재시로 갱신
      ResetBases(current, desired);
    }
  }

} // namespace DX11Base