#include "SpeedHack.h"
#include "../../Hooking/MinHook.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../../PerformanceDiagnostics.h"
#include <atomic>
#include <intrin.h>
#include <windows.h>

#pragma comment(lib, "winmm.lib")

namespace DX11Base {

  float g_speedMultiplier = 1.0f;
  bool bSpeedHack = false;

  namespace {
    static bool s_installed = false;
    static std::atomic<bool> s_enabled{false};

    typedef DWORD(WINAPI *PtimeGetTime)();
    static PtimeGetTime otimeGetTime = nullptr;

    typedef DWORD(WINAPI *PGetTickCount)();
    static PGetTickCount oGetTickCount = nullptr;

    typedef ULONGLONG(WINAPI *PGetTickCount64)();
    static PGetTickCount64 oGetTickCount64 = nullptr;

    typedef BOOL(WINAPI *PQueryPerformanceCounter)(LARGE_INTEGER *);
    static PQueryPerformanceCounter oQueryPerformanceCounter = nullptr;

    static LPVOID s_targetTimeGetTime = nullptr;
    static LPVOID s_targetGetTickCount = nullptr;
    static LPVOID s_targetGetTickCount64 = nullptr;
    static LPVOID s_targetQpc = nullptr;

    static uintptr_t s_selfBase = 0;
    static uintptr_t s_selfEnd = 0;

    static std::atomic<DWORD> s_tgtReal{0};
    static std::atomic<DWORD> s_tgtFake{0};

    static std::atomic<DWORD> s_gtcReal{0};
    static std::atomic<DWORD> s_gtcFake{0};

    static std::atomic<ULONGLONG> s_gtc64Real{0};
    static std::atomic<ULONGLONG> s_gtc64Fake{0};

    static std::atomic<LONGLONG> s_qpcReal{0};
    static std::atomic<LONGLONG> s_qpcFake{0};

    static std::atomic<float> s_multiplier{1.0f};

    static constexpr float MIN_MULTIPLIER = 1.0f;
    static constexpr float MAX_MULTIPLIER = 5.0f;
    static constexpr float MULTIPLIER_STEP = 0.5f;
    static LONGLONG s_maxDeltaQpc = 33000;
    static LONGLONG s_safeDeltaQpc = 50000;

    float NormalizeMultiplier(float value) {
      if (value < MIN_MULTIPLIER)
        value = MIN_MULTIPLIER;
      if (value > MAX_MULTIPLIER)
        value = MAX_MULTIPLIER;

      const int stepIndex = static_cast<int>(
          ((value - MIN_MULTIPLIER) / MULTIPLIER_STEP) + 0.5f);
      return MIN_MULTIPLIER + (stepIndex * MULTIPLIER_STEP);
    }

    void ResetBases(float desiredMul) {
      s_multiplier.store(NormalizeMultiplier(desiredMul),
                         std::memory_order_release);
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
        s_maxDeltaQpc = (freq.QuadPart * 33) / 1000;
        s_safeDeltaQpc = (freq.QuadPart * 50) / 1000;
      }
    }

    void InitializeSelfModuleRange() {
      HMODULE self = nullptr;
      const auto selfAddress = reinterpret_cast<LPCWSTR>(
          reinterpret_cast<uintptr_t>(&SpeedHack_Update));
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              selfAddress, &self) || !self) {
        return;
      }

      const uintptr_t base = reinterpret_cast<uintptr_t>(self);
      const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
      if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;

      const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
      if (!nt || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage == 0)
        return;

      s_selfBase = base;
      s_selfEnd = base + static_cast<uintptr_t>(nt->OptionalHeader.SizeOfImage);
    }

    bool IsCheatCaller(const void *returnAddress) {
      if (s_selfBase == 0 || s_selfEnd <= s_selfBase)
        return false;
      const uintptr_t caller = reinterpret_cast<uintptr_t>(returnAddress);
      return caller >= s_selfBase && caller < s_selfEnd;
    }

    bool HookCreated(MH_STATUS status) {
      return status == MH_OK;
    }

    bool EnableCreatedHook(LPVOID target) {
      return target && MH_EnableHook(target) == MH_OK;
    }

    void RollbackSpeedHooks() {
      s_enabled.store(false, std::memory_order_release);

      LPVOID targets[] = {
          s_targetQpc,
          s_targetGetTickCount,
          s_targetGetTickCount64,
          s_targetTimeGetTime,
      };
      for (LPVOID target : targets) {
        if (!target)
          continue;
        MH_DisableHook(target);
        MH_RemoveHook(target);
      }

      s_targetQpc = nullptr;
      s_targetGetTickCount = nullptr;
      s_targetGetTickCount64 = nullptr;
      s_targetTimeGetTime = nullptr;
      oQueryPerformanceCounter = nullptr;
      oGetTickCount = nullptr;
      oGetTickCount64 = nullptr;
      otimeGetTime = nullptr;
      s_installed = false;
    }
  } // namespace

  BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    BOOL ret = oQueryPerformanceCounter(lpPerformanceCount);

    if (!ret || IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire))
      return ret;

    float mul = s_multiplier.load(std::memory_order_acquire);
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

    delta = static_cast<LONGLONG>(delta * mul);
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
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire))
      return real;

    float mul = s_multiplier.load(std::memory_order_acquire);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    DWORD prevReal = s_gtcReal.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_gtcReal.store(real, std::memory_order_relaxed);
      s_gtcFake.store(real, std::memory_order_relaxed);
      return real;
    }

    DWORD delta = real - prevReal;
    if (static_cast<int32_t>(delta) < 0)
      delta = 0;
    if (delta > 50)
      mul = 1.0f;
    delta = static_cast<DWORD>(static_cast<float>(delta) * mul);

    DWORD fake = s_gtcFake.load(std::memory_order_relaxed) + delta;
    s_gtcReal.store(real, std::memory_order_relaxed);
    s_gtcFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  ULONGLONG WINAPI hkGetTickCount64() {
    ULONGLONG real = oGetTickCount64();
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire))
      return real;

    float mul = s_multiplier.load(std::memory_order_acquire);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    ULONGLONG prevReal = s_gtc64Real.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_gtc64Real.store(real, std::memory_order_relaxed);
      s_gtc64Fake.store(real, std::memory_order_relaxed);
      return real;
    }

    ULONGLONG delta = 0;
    if (real >= prevReal)
      delta = real - prevReal;
    if (delta > 50)
      mul = 1.0f;
    delta = static_cast<ULONGLONG>(static_cast<double>(delta) * mul);

    ULONGLONG fake = s_gtc64Fake.load(std::memory_order_relaxed) + delta;
    s_gtc64Real.store(real, std::memory_order_relaxed);
    s_gtc64Fake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  DWORD WINAPI hktimeGetTime() {
    DWORD real = otimeGetTime();
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire))
      return real;

    float mul = s_multiplier.load(std::memory_order_acquire);
    if (mul > MAX_MULTIPLIER)
      mul = MAX_MULTIPLIER;

    DWORD prevReal = s_tgtReal.load(std::memory_order_relaxed);
    if (prevReal == 0) {
      s_tgtReal.store(real, std::memory_order_relaxed);
      s_tgtFake.store(real, std::memory_order_relaxed);
      return real;
    }

    DWORD delta = real - prevReal;
    if (static_cast<int32_t>(delta) < 0)
      delta = 0;
    if (delta > 50)
      mul = 1.0f;
    delta = static_cast<DWORD>(static_cast<float>(delta) * mul);

    DWORD fake = s_tgtFake.load(std::memory_order_relaxed) + delta;
    s_tgtReal.store(real, std::memory_order_relaxed);
    s_tgtFake.store(fake, std::memory_order_relaxed);
    return fake;
  }

  void SpeedHack_Sleep_Install() {
    if (s_installed || !bSpeedHack)
      return;

    InitializeQpcThresholds();
    InitializeSelfModuleRange();

    if (!HookCreated(MH_CreateHookApiEx(L"kernel32.dll", "QueryPerformanceCounter",
                                        &hkQueryPerformanceCounter,
                                        reinterpret_cast<LPVOID *>(&oQueryPerformanceCounter),
                                        &s_targetQpc)) ||
        !HookCreated(MH_CreateHookApiEx(L"kernel32.dll", "GetTickCount",
                                        &hkGetTickCount,
                                        reinterpret_cast<LPVOID *>(&oGetTickCount),
                                        &s_targetGetTickCount)) ||
        !HookCreated(MH_CreateHookApiEx(L"kernel32.dll", "GetTickCount64",
                                        &hkGetTickCount64,
                                        reinterpret_cast<LPVOID *>(&oGetTickCount64),
                                        &s_targetGetTickCount64)) ||
        !HookCreated(MH_CreateHookApiEx(L"winmm.dll", "timeGetTime",
                                        &hktimeGetTime,
                                        reinterpret_cast<LPVOID *>(&otimeGetTime),
                                        &s_targetTimeGetTime))) {
      AddLog(u8"[SpeedHack] 시간 API 훅 생성 실패. 배속 훅을 롤백합니다.");
      RollbackSpeedHooks();
      return;
    }

    if (!EnableCreatedHook(s_targetQpc) ||
        !EnableCreatedHook(s_targetGetTickCount) ||
        !EnableCreatedHook(s_targetGetTickCount64) ||
        !EnableCreatedHook(s_targetTimeGetTime)) {
      AddLog(u8"[SpeedHack] 시간 API 훅 활성화 실패. 배속 훅을 롤백합니다.");
      RollbackSpeedHooks();
      return;
    }

    ResetClockState();
    ResetBases(g_speedMultiplier);
    s_enabled.store(bSpeedHack, std::memory_order_release);
    s_installed = true;
  }

  void SpeedHack_Init() { SpeedHack_Sleep_Install(); }

  float SpeedHack_NormalizeMultiplier(float value) {
    return NormalizeMultiplier(value);
  }

  void SpeedHack_Update(uintptr_t p1) {
    PerfScope perfScope(PerfMetric::SpeedHackUpdate);

    if (!s_installed) {
      if (!bSpeedHack) {
        s_enabled.store(false, std::memory_order_release);
        PerfSetSpeedState(false, 1.0f);
        return;
      }
      SpeedHack_Init();
      if (!s_installed) {
        bSpeedHack = false;
        PerfSetSpeedState(false, 1.0f);
        return;
      }
    }

    static bool s_lastEnabled = false;
    if (s_lastEnabled != bSpeedHack) {
      s_enabled.store(false, std::memory_order_release);
      ResetClockState();
      s_lastEnabled = bSpeedHack;
    }

    float desired = bSpeedHack ? NormalizeMultiplier(g_speedMultiplier) : 1.0f;
    if (bSpeedHack && g_speedMultiplier != desired)
      g_speedMultiplier = desired;

    float current = s_multiplier.load(std::memory_order_acquire);
    if (current != desired) {
      AddLog(u8"[SpeedHack] 배율 변경: %.1fx -> %.1fx", current, desired);
      ResetBases(desired);
      ResetClockState();
    }

    s_enabled.store(bSpeedHack, std::memory_order_release);
    PerfSetSpeedState(bSpeedHack, desired);
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

    float dt = static_cast<float>(static_cast<double>(delta) /
                                  static_cast<double>(s_freq.QuadPart));
    if (dt > 0.1f)
      dt = 0.1f;
    return dt;
  }

} // namespace DX11Base
