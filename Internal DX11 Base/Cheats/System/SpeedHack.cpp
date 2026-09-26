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

    // All four virtual clocks share one generation. Hooks only publish a
    // snapshot when the generation is even and unchanged, so a multiplier
    // change can never expose mixed old/new anchors to another thread.
    static std::atomic<uint32_t> s_clockGeneration{0};
    static std::atomic_flag s_clockWriter = ATOMIC_FLAG_INIT;

    static std::atomic<DWORD> s_tgtRealAnchor{0};
    static std::atomic<DWORD> s_tgtFakeAnchor{0};

    static std::atomic<DWORD> s_gtcRealAnchor{0};
    static std::atomic<DWORD> s_gtcFakeAnchor{0};

    static std::atomic<ULONGLONG> s_gtc64RealAnchor{0};
    static std::atomic<ULONGLONG> s_gtc64FakeAnchor{0};

    static std::atomic<LONGLONG> s_qpcRealAnchor{0};
    static std::atomic<LONGLONG> s_qpcFakeAnchor{0};

    static std::atomic<float> s_multiplier{1.0f};

    static constexpr float MIN_MULTIPLIER = 1.0f;
    static constexpr float MAX_MULTIPLIER = 5.0f;
    static constexpr float MULTIPLIER_STEP = 0.5f;

    float NormalizeMultiplier(float value) {
      if (value < MIN_MULTIPLIER)
        value = MIN_MULTIPLIER;
      if (value > MAX_MULTIPLIER)
        value = MAX_MULTIPLIER;

      const int stepIndex = static_cast<int>(
          ((value - MIN_MULTIPLIER) / MULTIPLIER_STEP) + 0.5f);
      return MIN_MULTIPLIER + (stepIndex * MULTIPLIER_STEP);
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

    void LockClockWriter() {
      while (s_clockWriter.test_and_set(std::memory_order_acquire))
        YieldProcessor();
    }

    void UnlockClockWriter() {
      s_clockWriter.clear(std::memory_order_release);
    }

    template <typename T>
    struct ClockSnapshot {
      T realAnchor{};
      T fakeAnchor{};
      float multiplier{1.0f};
    };

    template <typename T>
    ClockSnapshot<T> ReadClockSnapshot(const std::atomic<T> &realAnchor,
                                       const std::atomic<T> &fakeAnchor) {
      for (;;) {
        const uint32_t before = s_clockGeneration.load(std::memory_order_acquire);
        if (before & 1u) {
          YieldProcessor();
          continue;
        }

        ClockSnapshot<T> snapshot{};
        snapshot.realAnchor = realAnchor.load(std::memory_order_relaxed);
        snapshot.fakeAnchor = fakeAnchor.load(std::memory_order_relaxed);
        snapshot.multiplier = s_multiplier.load(std::memory_order_relaxed);

        const uint32_t after = s_clockGeneration.load(std::memory_order_acquire);
        if (before == after)
          return snapshot;
      }
    }

    DWORD Scale32(DWORD real, DWORD realAnchor, DWORD fakeAnchor, float multiplier) {
      // DWORD subtraction intentionally keeps Win32 tick wrap semantics.
      const DWORD delta = real - realAnchor;
      const DWORD scaled = static_cast<DWORD>(static_cast<double>(delta) * multiplier);
      return fakeAnchor + scaled;
    }

    ULONGLONG Scale64(ULONGLONG real, ULONGLONG realAnchor,
                      ULONGLONG fakeAnchor, float multiplier) {
      const ULONGLONG delta = (real >= realAnchor) ? (real - realAnchor) : 0;
      const ULONGLONG scaled =
          static_cast<ULONGLONG>(static_cast<long double>(delta) * multiplier);
      return fakeAnchor + scaled;
    }

    LONGLONG ScaleQpc(LONGLONG real, LONGLONG realAnchor,
                      LONGLONG fakeAnchor, float multiplier) {
      const LONGLONG delta = (real >= realAnchor) ? (real - realAnchor) : 0;
      const LONGLONG scaled =
          static_cast<LONGLONG>(static_cast<long double>(delta) * multiplier);
      return fakeAnchor + scaled;
    }

    void ResetClockAnchors(bool preserveVirtualTime, float previousMultiplier,
                           float newMultiplier) {
      if (!oQueryPerformanceCounter || !oGetTickCount ||
          !oGetTickCount64 || !otimeGetTime) {
        return;
      }

      LARGE_INTEGER qpcNow{};
      if (!oQueryPerformanceCounter(&qpcNow))
        return;

      const DWORD gtcNow = oGetTickCount();
      const ULONGLONG gtc64Now = oGetTickCount64();
      const DWORD tgtNow = otimeGetTime();

      LockClockWriter();
      s_clockGeneration.fetch_add(1u, std::memory_order_acq_rel); // odd: writer active

      LONGLONG qpcFakeNow = qpcNow.QuadPart;
      DWORD gtcFakeNow = gtcNow;
      ULONGLONG gtc64FakeNow = gtc64Now;
      DWORD tgtFakeNow = tgtNow;

      if (preserveVirtualTime) {
        const LONGLONG oldQpcReal = s_qpcRealAnchor.load(std::memory_order_relaxed);
        const LONGLONG oldQpcFake = s_qpcFakeAnchor.load(std::memory_order_relaxed);
        const DWORD oldGtcReal = s_gtcRealAnchor.load(std::memory_order_relaxed);
        const DWORD oldGtcFake = s_gtcFakeAnchor.load(std::memory_order_relaxed);
        const ULONGLONG oldGtc64Real = s_gtc64RealAnchor.load(std::memory_order_relaxed);
        const ULONGLONG oldGtc64Fake = s_gtc64FakeAnchor.load(std::memory_order_relaxed);
        const DWORD oldTgtReal = s_tgtRealAnchor.load(std::memory_order_relaxed);
        const DWORD oldTgtFake = s_tgtFakeAnchor.load(std::memory_order_relaxed);

        if (oldQpcReal != 0)
          qpcFakeNow = ScaleQpc(qpcNow.QuadPart, oldQpcReal, oldQpcFake, previousMultiplier);
        if (oldGtcReal != 0)
          gtcFakeNow = Scale32(gtcNow, oldGtcReal, oldGtcFake, previousMultiplier);
        if (oldGtc64Real != 0)
          gtc64FakeNow = Scale64(gtc64Now, oldGtc64Real, oldGtc64Fake, previousMultiplier);
        if (oldTgtReal != 0)
          tgtFakeNow = Scale32(tgtNow, oldTgtReal, oldTgtFake, previousMultiplier);
      }

      s_qpcRealAnchor.store(qpcNow.QuadPart, std::memory_order_relaxed);
      s_qpcFakeAnchor.store(qpcFakeNow, std::memory_order_relaxed);
      s_gtcRealAnchor.store(gtcNow, std::memory_order_relaxed);
      s_gtcFakeAnchor.store(gtcFakeNow, std::memory_order_relaxed);
      s_gtc64RealAnchor.store(gtc64Now, std::memory_order_relaxed);
      s_gtc64FakeAnchor.store(gtc64FakeNow, std::memory_order_relaxed);
      s_tgtRealAnchor.store(tgtNow, std::memory_order_relaxed);
      s_tgtFakeAnchor.store(tgtFakeNow, std::memory_order_relaxed);
      s_multiplier.store(NormalizeMultiplier(newMultiplier), std::memory_order_relaxed);

      s_clockGeneration.fetch_add(1u, std::memory_order_release); // even: publish snapshot
      UnlockClockWriter();
    }

    void ClearClockAnchors() {
      LockClockWriter();
      s_clockGeneration.fetch_add(1u, std::memory_order_acq_rel);
      s_qpcRealAnchor.store(0, std::memory_order_relaxed);
      s_qpcFakeAnchor.store(0, std::memory_order_relaxed);
      s_gtcRealAnchor.store(0, std::memory_order_relaxed);
      s_gtcFakeAnchor.store(0, std::memory_order_relaxed);
      s_gtc64RealAnchor.store(0, std::memory_order_relaxed);
      s_gtc64FakeAnchor.store(0, std::memory_order_relaxed);
      s_tgtRealAnchor.store(0, std::memory_order_relaxed);
      s_tgtFakeAnchor.store(0, std::memory_order_relaxed);
      s_multiplier.store(1.0f, std::memory_order_relaxed);
      s_clockGeneration.fetch_add(1u, std::memory_order_release);
      UnlockClockWriter();
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
      ClearClockAnchors();
      s_installed = false;
    }
  } // namespace

  BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    const BOOL ret = oQueryPerformanceCounter(lpPerformanceCount);
    if (!ret || IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire)) {
      return ret;
    }

    const auto snapshot = ReadClockSnapshot(s_qpcRealAnchor, s_qpcFakeAnchor);
    if (snapshot.realAnchor == 0)
      return ret;

    lpPerformanceCount->QuadPart =
        ScaleQpc(lpPerformanceCount->QuadPart, snapshot.realAnchor,
                 snapshot.fakeAnchor, snapshot.multiplier);
    return ret;
  }

  DWORD WINAPI hkGetTickCount() {
    const DWORD real = oGetTickCount();
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire)) {
      return real;
    }

    const auto snapshot = ReadClockSnapshot(s_gtcRealAnchor, s_gtcFakeAnchor);
    if (snapshot.realAnchor == 0)
      return real;
    return Scale32(real, snapshot.realAnchor, snapshot.fakeAnchor,
                   snapshot.multiplier);
  }

  ULONGLONG WINAPI hkGetTickCount64() {
    const ULONGLONG real = oGetTickCount64();
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire)) {
      return real;
    }

    const auto snapshot = ReadClockSnapshot(s_gtc64RealAnchor, s_gtc64FakeAnchor);
    if (snapshot.realAnchor == 0)
      return real;
    return Scale64(real, snapshot.realAnchor, snapshot.fakeAnchor,
                   snapshot.multiplier);
  }

  DWORD WINAPI hktimeGetTime() {
    const DWORD real = otimeGetTime();
    if (IsCheatCaller(_ReturnAddress()) ||
        !s_enabled.load(std::memory_order_acquire)) {
      return real;
    }

    const auto snapshot = ReadClockSnapshot(s_tgtRealAnchor, s_tgtFakeAnchor);
    if (snapshot.realAnchor == 0)
      return real;
    return Scale32(real, snapshot.realAnchor, snapshot.fakeAnchor,
                   snapshot.multiplier);
  }

  void SpeedHack_Sleep_Install() {
    if (s_installed || !bSpeedHack)
      return;

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

    const float desired = NormalizeMultiplier(g_speedMultiplier);
    ResetClockAnchors(false, 1.0f, desired);
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
    const bool enabledNow = bSpeedHack;
    const float desired = enabledNow ? NormalizeMultiplier(g_speedMultiplier) : 1.0f;
    if (enabledNow && g_speedMultiplier != desired)
      g_speedMultiplier = desired;

    const float current = s_multiplier.load(std::memory_order_acquire);

    if (s_lastEnabled != enabledNow) {
      if (!enabledNow) {
        // OFF means the game immediately returns to the original wall clock.
        s_enabled.store(false, std::memory_order_release);
      } else {
        // After any OFF interval, restart virtual time from the current real
        // clock so the first ON sample cannot reuse an old accelerated anchor.
        ResetClockAnchors(false, 1.0f, desired);
        s_enabled.store(true, std::memory_order_release);
      }
      s_lastEnabled = enabledNow;
    } else if (enabledNow && current != desired) {
      AddLog(u8"[SpeedHack] 배율 변경: %.1fx -> %.1fx", current, desired);
      // Preserve the currently visible virtual time while changing only its
      // slope. Hooks see the old or new complete snapshot, never a mixture.
      ResetClockAnchors(true, current, desired);
    }

    PerfSetSpeedState(enabledNow, desired);
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

    const LONGLONG delta = now.QuadPart - s_prev;
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