#include "SpeedHack.h"
#include "../../Hooking/MinHook.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include <atomic>
#include <windows.h>

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
        // QPC는 PC마다 주파수가 다르므로 실제 주파수 기준으로 33ms/50ms를 계산합니다.
        s_maxDeltaQpc = (freq.QuadPart * 33) / 1000;
        s_safeDeltaQpc = (freq.QuadPart * 50) / 1000;
      }
    }
  } // namespace

  BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    BOOL ret = oQueryPerformanceCounter(lpPerformanceCount);

    // 훅이 한 번 설치된 이후 배속이 OFF라면 원본 값을 그대로 반환합니다.
    if (!ret || !bSpeedHack)
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
    DWORD real = oGetTickCount();
    if (!bSpeedHack)
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
    ULONGLONG real = oGetTickCount64();
    if (!bSpeedHack)
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
    DWORD real = otimeGetTime();
    if (!bSpeedHack)
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
    // Engine::HookD3D()에서도 이 함수가 호출되지만 배속이 꺼져 있으면 아무 훅도 만들지 않습니다.
    // 실제 첫 설치는 사용자가 배속을 ON한 뒤 SpeedHack_Update()가 다시 호출하는 시점입니다.
    if (s_installed || !bSpeedHack)
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
      // OFF 동안 원본 시간이 진행된 뒤 다시 ON할 때 오래된 fake 기준을 재사용하지 않습니다.
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

  float SpeedHack_GetRealDeltaTime() {
    static LARGE_INTEGER s_freq{};
    static LONGLONG s_prev = 0;

    if (s_freq.QuadPart <= 0) {
      if (!QueryPerformanceFrequency(&s_freq) || s_freq.QuadPart <= 0)
        return 1.0f / 60.0f;
    }

    LARGE_INTEGER now{};
    BOOL ok = FALSE;

    // 훅 설치 전에는 일반 QPC를 써도 실제 시간입니다.
    // 훅 설치 후에는 MinHook이 넘겨준 원본 트램펄린을 직접 호출해 SpeedHack 가짜 시간을 우회합니다.
    if (s_installed && oQueryPerformanceCounter)
      ok = oQueryPerformanceCounter(&now);
    else
      ok = QueryPerformanceCounter(&now);

    if (!ok)
      return 1.0f / 60.0f;

    if (s_prev == 0) {
      s_prev = now.QuadPart;
      return 1.0f / 60.0f;
    }

    LONGLONG delta = now.QuadPart - s_prev;
    s_prev = now.QuadPart;
    if (delta <= 0)
      return 1.0f / 60.0f;

    float dt = (float)((double)delta / (double)s_freq.QuadPart);
    if (dt > 0.1f)
      dt = 0.1f;
    return dt;
  }

} // namespace DX11Base