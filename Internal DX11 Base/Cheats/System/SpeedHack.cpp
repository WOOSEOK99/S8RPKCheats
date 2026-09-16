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

    static constexpr float MAX_MULTIPLIER = 3.0f;
    static LONGLONG s_maxDeltaQpc = 33000;
    static LONGLONG s_safeDeltaQpc = 50000;

    void ResetBases(float desiredMul) {
      s_multiplier.store(desiredMul, std::memory_order_relaxed);
    }

    void ResetClockState() {
      s_tgtReal.store(0, std::memory_order_relaxed);
      s_tgtFake.store(0, std::memory_order_relaxed);
      s_gtcReal.store(0, std::memory_order_relaxed);
      s_gtcFake.store(0, std::memory_order_relaxed);
      s_gtc64Real.store(0, std::memory_order_relaxed);
      s_gtc64Fake.store(0, std::memory_order_relaxed);
      s_qpcReal.store(0, std::memory_order_relaxed);
      s_qpcFake.store(0, std::memory_order_relaxed);
    }

    void InitializeQpcThresholds() {
      LARGE_INTEGER freq{};
      if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
        // QPC의 단위는 PC마다 다르므로 실제 주파수 기준으로 33ms/50ms를 계산합니다.
        s_maxDeltaQpc = (freq.QuadPart * 33) / 1000;
        s_safeDeltaQpc = (freq.QuadPart * 50) / 1000;
      }
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

    // 배속 OFF 또는 게임 EXE 외부 호출(hid.dll 내부 타이머 포함)은 원본 값을 그대로 반환합니다.
    // 치트 자체의 감시 타이머가 배속에 따라 빨라지는 현상도 함께 차단합니다.
    if (!ret || !bSpeedHack || !IsCallerGame(caller))
      return ret;

    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    LONGLONG real = lpPerformanceCount->QuadPart;
    LONGLONG prevReal = s_qpcReal.load(std::memory_order_relaxed);

    if (prevReal == 0) {
      s_qpcReal.store(real, std::memory_order_relaxed);
      s_qpcFake.store(real, std::memory_order_relaxed);
      return ret;
    }

    LONGLONG delta = real - prevReal;
    if (delta < 0)
      delta = 0;

    // 큰 끊김은 배속하지 않고, 한 번의 가상 시간 증가량도 33ms까지만 허용합니다.
    if (delta > s_safeDeltaQpc)
      mul = 1.0f;

    delta = (LONGLONG)(delta * mul);
    if (delta > s_maxDeltaQpc)
      delta = s_maxDeltaQpc;

    LONGLONG fake = s_qpcFake.load(std::memory_order_relaxed) + delta;
    s_qpcReal.store(real, std::memory_order_relaxed);
    s_qpcFake.store(fake, std::memory_order_relaxed);

    lpPerformanceCount->QuadPart = fake;
    return ret;
  }

  DWORD WINAPI hkGetTickCount() {
    void *caller = _ReturnAddress();
    DWORD real = oGetTickCount();
    if (!bSpeedHack || !IsCallerGame(caller))
      return real;

    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    DWORD prevReal = s_gtcReal.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_gtcReal.store(real, std::memory_order_relaxed);
      s_gtcFake.store(real, std::memory_order_relaxed);
      return real;
    }

    DWORD delta = real - prevReal;
    if ((int)delta < 0)
      delta = 0;
    if (delta > 50)
      mul = 1.0f;
    delta = (DWORD)((float)delta * mul);

    DWORD fake = s_gtcFake.load(std::memory_order_relaxed) + delta;
    s_gtcReal.store(real, std::memory_order_relaxed);
    s_gtcFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  ULONGLONG WINAPI hkGetTickCount64() {
    void *caller = _ReturnAddress();
    ULONGLONG real = oGetTickCount64();
    if (!bSpeedHack || !IsCallerGame(caller))
      return real;

    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    ULONGLONG prevReal = s_gtc64Real.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_gtc64Real.store(real, std::memory_order_relaxed);
      s_gtc64Fake.store(real, std::memory_order_relaxed);
      return real;
    }

    ULONGLONG delta = real - prevReal;
    if (delta > 50)
      mul = 1.0f;
    delta = (ULONGLONG)((float)delta * mul);

    ULONGLONG fake = s_gtc64Fake.load(std::memory_order_relaxed) + delta;
    s_gtc64Real.store(real, std::memory_order_relaxed);
    s_gtc64Fake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  DWORD WINAPI hktimeGetTime() {
    void *caller = _ReturnAddress();
    DWORD real = otimeGetTime();
    if (!bSpeedHack || !IsCallerGame(caller))
      return real;

    float mul = s_multiplier.load(std::memory_order_relaxed);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    DWORD prevReal = s_tgtReal.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_tgtReal.store(real, std::memory_order_relaxed);
      s_tgtFake.store(real, std::memory_order_relaxed);
      return real;
    }

    DWORD delta = real - prevReal;
    if ((int)delta < 0)
      delta = 0;
    if (delta > 50)
      mul = 1.0f;
    delta = (DWORD)((float)delta * mul);

    DWORD fake = s_tgtFake.load(std::memory_order_relaxed) + delta;
    s_tgtReal.store(real, std::memory_order_relaxed);
    s_tgtFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  void SpeedHack_Sleep_Install() {
    if (s_installed)
      return;

    InitializeQpcThresholds();

    MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", &hkQueryPerformanceCounter, (LPVOID *)&oQueryPerformanceCounter);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount", &hkGetTickCount, (LPVOID *)&oGetTickCount);
    MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", &hkGetTickCount64, (LPVOID *)&oGetTickCount64);
    MH_CreateHookApi(L"winmm.dll", "timeGetTime", &hktimeGetTime, (LPVOID *)&otimeGetTime);

    MH_EnableHook(MH_ALL_HOOKS);

    ResetClockState();
    ResetBases(g_speedMultiplier);
    s_installed = true;
  }

  void SpeedHack_Init() { SpeedHack_Sleep_Install(); }

  void SpeedHack_Update(uintptr_t p1) {
    // 배속을 한 번도 켜지 않았다면 시간 API 훅 자체를 설치하지 않습니다.
    if (!s_installed) {
      if (!bSpeedHack)
        return;
      SpeedHack_Init();
    }

    static bool s_lastEnabled = false;
    if (s_lastEnabled != bSpeedHack) {
      // OFF 동안 원본 시간이 진행된 뒤 다시 ON할 때 오래된 fake 기준값을 재사용하지 않도록 초기화합니다.
      ResetClockState();
      s_lastEnabled = bSpeedHack;
    }

    // 전투 중에는 주인공 주소(p1)가 0으로 풀리므로 p1과 무관하게 배속 상태를 유지합니다.
    float desired = bSpeedHack ? g_speedMultiplier : 1.0f;
    if (desired > MAX_MULTIPLIER)
      desired = MAX_MULTIPLIER;

    float current = s_multiplier.load(std::memory_order_relaxed);
    if (current != desired) {
      AddLog(u8"[SpeedHack] 배율 변경: %.1fx -> %.1fx", current, desired);
      ResetBases(desired);
    }
  }

} // namespace DX11Base