#include "SpeedHack.h"
#include "../../Hooking/MinHook.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include <atomic>
#include <intrin.h>
#include <windows.h>

#pragma intrinsic(_ReturnAddress)
#pragma comment(lib, "winmm.lib")

namespace DX11Base {

  float g_speedMultiplier = 1.0f;
  bool bSpeedHack = false;

  namespace {
    static bool s_installed = false;

    typedef DWORD(WINAPI *PtimeGetTime)();
    static PtimeGetTime otimeGetTime = nullptr;

    typedef DWORD(WINAPI *PGetTickCount)();
    static PGetTickCount oGetTickCount = nullptr;

    typedef ULONGLONG(WINAPI *PGetTickCount64)();
    static PGetTickCount64 oGetTickCount64 = nullptr;

    typedef BOOL(WINAPI *PQueryPerformanceCounter)(LARGE_INTEGER *);
    static PQueryPerformanceCounter oQueryPerformanceCounter = nullptr;

    static std::atomic<DWORD> s_tgtReal{0};
    static std::atomic<DWORD> s_tgtFake{0};

    static std::atomic<DWORD> s_gtcReal{0};
    static std::atomic<DWORD> s_gtcFake{0};

    static std::atomic<ULONGLONG> s_gtc64Real{0};
    static std::atomic<ULONGLONG> s_gtc64Fake{0};

    static std::atomic<LONGLONG> s_qpcReal{0};
    static std::atomic<LONGLONG> s_qpcFake{0};

    static std::atomic<float> s_multiplier{1.0f};

    // [MOD BEGIN] 안정화 파라미터
    static constexpr LONGLONG MAX_DELTA_QPC = 33000;        // 약 33ms
    static constexpr float MAX_MULTIPLIER = 3.0f;           // 최대 배속
    static constexpr LONGLONG SAFE_DELTA_THRESHOLD = 50000; // 50ms 이상이면 보호
    // [MOD END]

    void ResetBases(float currentMul, float desiredMul) {
      if (oQueryPerformanceCounter) {
        LARGE_INTEGER li;
        oQueryPerformanceCounter(&li);
        s_qpcReal.store(li.QuadPart);
        s_qpcFake.store(li.QuadPart);
      }
      if (oGetTickCount) {
        DWORD t = oGetTickCount();
        s_gtcReal.store(t);
        s_gtcFake.store(t);
      }
      if (oGetTickCount64) {
        ULONGLONG t = oGetTickCount64();
        s_gtc64Real.store(t);
        s_gtc64Fake.store(t);
      }
      if (otimeGetTime) {
        DWORD t = otimeGetTime();
        s_tgtReal.store(t);
        s_tgtFake.store(t);
      }
      s_multiplier.store(desiredMul);
    }

    bool __forceinline IsCallerGame(void *caller) {
      static uintptr_t s_exeBase = 0;
      static uintptr_t s_exeEnd = 0;
      if (s_exeBase == 0) {
        HMODULE hExe = GetModuleHandle(NULL);
        if (hExe) {
          PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
          PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uint8_t *)hExe + dosHeader->e_lfanew);
          s_exeBase = (uintptr_t)hExe;
          s_exeEnd = s_exeBase + ntHeaders->OptionalHeader.SizeOfImage;
        }
      }
      uintptr_t addr = (uintptr_t)caller;
      return (addr >= s_exeBase && addr < s_exeEnd);
    }
  } // namespace

  BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    void *caller = _ReturnAddress();
    BOOL ret = oQueryPerformanceCounter(lpPerformanceCount);

    if (!ret)
      return ret;

    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    if (!bSpeedHack || mul == 1.0f)
      return ret;

    // [MOD BEGIN] 안정화된 QPC 처리 핵심
    LONGLONG real = lpPerformanceCount->QuadPart;
    LONGLONG prevReal = s_qpcReal.load(std::memory_order_relaxed);

    LONGLONG delta = real - prevReal;

    // 비정상 값 방지
    if (delta < 0)
      delta = 0;

    // 큰 끊김 보호
    if (delta > SAFE_DELTA_THRESHOLD)
      mul = 1.0f;

    delta = (LONGLONG)(delta * mul);

    // delta 상한 제한
    if (delta > MAX_DELTA_QPC)
      delta = MAX_DELTA_QPC;

    // 🔥 핵심: 누적 방식
    LONGLONG fake = s_qpcFake.load(std::memory_order_relaxed) + delta;

    // 상태 업데이트
    s_qpcReal.store(real, std::memory_order_relaxed);
    s_qpcFake.store(fake, std::memory_order_relaxed);

    lpPerformanceCount->QuadPart = fake;
    // [MOD END]

    return ret;
  }

  DWORD WINAPI hkGetTickCount() {
    DWORD real = oGetTickCount();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f) return real;

    DWORD prevReal = s_gtcReal.load(std::memory_order_relaxed);
    DWORD delta = real - prevReal;
    if ((int)delta < 0) delta = 0;
    if (delta > 50) mul = 1.0f; // 급격한 스파이크 방지
    delta = (DWORD)((float)delta * mul);

    DWORD fake = s_gtcFake.load(std::memory_order_relaxed) + delta;
    s_gtcReal.store(real, std::memory_order_relaxed);
    s_gtcFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  ULONGLONG WINAPI hkGetTickCount64() {
    ULONGLONG real = oGetTickCount64();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f) return real;

    ULONGLONG prevReal = s_gtc64Real.load(std::memory_order_relaxed);
    ULONGLONG delta = real - prevReal;
    if (delta > 50) mul = 1.0f;
    delta = (ULONGLONG)((float)delta * mul);

    ULONGLONG fake = s_gtc64Fake.load(std::memory_order_relaxed) + delta;
    s_gtc64Real.store(real, std::memory_order_relaxed);
    s_gtc64Fake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  DWORD WINAPI hktimeGetTime() {
    DWORD real = otimeGetTime();
    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (!bSpeedHack || mul == 1.0f) return real;

    DWORD prevReal = s_tgtReal.load(std::memory_order_relaxed);
    DWORD delta = real - prevReal;
    if ((int)delta < 0) delta = 0;
    if (delta > 50) mul = 1.0f;
    delta = (DWORD)((float)delta * mul);

    DWORD fake = s_tgtFake.load(std::memory_order_relaxed) + delta;
    s_tgtReal.store(real, std::memory_order_relaxed);
    s_tgtFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  void SpeedHack_Sleep_Install() {
    if (s_installed)
      return;

    MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", &hkQueryPerformanceCounter, (LPVOID *)&oQueryPerformanceCounter);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount", &hkGetTickCount, (LPVOID *)&oGetTickCount);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", &hkGetTickCount64, (LPVOID *)&oGetTickCount64);
    MH_CreateHookApi(L"winmm.dll", "timeGetTime", &hktimeGetTime, (LPVOID *)&otimeGetTime);
    
    MH_EnableHook(MH_ALL_HOOKS);

    ResetBases(1.0f, g_speedMultiplier);

    s_installed = true;
  }

  void SpeedHack_Init() { SpeedHack_Sleep_Install(); }

  void SpeedHack_Update(uintptr_t p1) {
    if (!s_installed) {
      SpeedHack_Init();
    }

    // 전투 중에는 주인공 주소(p1)가 0으로 풀리므로 조건에서 p1 검사를 해제하여 배속 유지
    float desired = bSpeedHack ? g_speedMultiplier : 1.0f;

    // [MOD BEGIN] 배율 제한
    if (desired > MAX_MULTIPLIER)
      desired = MAX_MULTIPLIER;
    // [MOD END]

    float current = s_multiplier.load();

    if (current != desired) {
      AddLog(u8"[SpeedHack] 배율 변경: %.1fx -> %.1fx", current, desired);
      ResetBases(current, desired);
    }
  }

} // namespace DX11Base