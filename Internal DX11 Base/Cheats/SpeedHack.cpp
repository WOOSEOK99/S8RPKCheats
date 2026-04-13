// =============================================================================
// SpeedHack.cpp – RTSS 충돌 방지 안정화 버전
// 핵심:
//  - QPC Hook 제거
//  - Tick 기반만 사용
//  - 시간 연속성 유지
// =============================================================================

#include "SpeedHack.h"
#include "../Hooking/MinHook.h"
#include "../MenuState.h"
#include "../pch.h"
#include <atomic>
#include <windows.h>

namespace DX11Base {

  namespace {

    typedef DWORD(WINAPI *PGetTickCount)();
    typedef ULONGLONG(WINAPI *PGetTickCount64)();

    static PGetTickCount oGetTickCount = nullptr;
    static PGetTickCount64 oGetTickCount64 = nullptr;

    static bool s_installed = false;

    // 시간 기준 (연속성 유지)
    static std::atomic<ULONGLONG> s_startReal{0};
    static std::atomic<ULONGLONG> s_startFake{0};

    static std::atomic<float> s_multiplier{1.0f};
  } // namespace

  // ---------------------------------------------------------------------------
  // Hook 함수 (Tick 기반)
  // ---------------------------------------------------------------------------
  static DWORD WINAPI hkGetTickCount() {
    DWORD real = oGetTickCount();

    float mul = s_multiplier.load(std::memory_order_relaxed);

    ULONGLONG startReal = s_startReal.load(std::memory_order_relaxed);
    ULONGLONG startFake = s_startFake.load(std::memory_order_relaxed);

    ULONGLONG delta = real - startReal;
    ULONGLONG scaled = (ULONGLONG)(delta * mul);

    return (DWORD)(startFake + scaled);
  }

  static ULONGLONG WINAPI hkGetTickCount64() {
    ULONGLONG real = oGetTickCount64();

    float mul = s_multiplier.load(std::memory_order_relaxed);

    ULONGLONG startReal = s_startReal.load(std::memory_order_relaxed);
    ULONGLONG startFake = s_startFake.load(std::memory_order_relaxed);

    ULONGLONG delta = real - startReal;
    ULONGLONG scaled = (ULONGLONG)(delta * mul);

    return startFake + scaled;
  }

  // ---------------------------------------------------------------------------
  // 설치 (1회)
  // ---------------------------------------------------------------------------
  void SpeedHack_Install() {
    if (s_installed)
      return;

    ULONGLONG now = GetTickCount64();

    s_startReal.store(now);
    s_startFake.store(now);

    if (MH_CreateHookApi(L"kernel32.dll", "GetTickCount", &hkGetTickCount, (LPVOID *)&oGetTickCount) == MH_OK &&
        MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", &hkGetTickCount64, (LPVOID *)&oGetTickCount64) == MH_OK) {
      MH_EnableHook(MH_ALL_HOOKS);
      s_installed = true;
    }
  }

  // ---------------------------------------------------------------------------
  // 업데이트 (배속 변경)
  // ---------------------------------------------------------------------------
  void SpeedHack_Update() {

    if (bSpeedHack && !s_installed)
      SpeedHack_Install();

    if (!s_installed)
      return;

    float desired = bSpeedHack ? g_speedMultiplier : 1.0f;
    float current = s_multiplier.load(std::memory_order_relaxed);

    if (desired != current) {

      // 현재 시점 기준으로 "연속성 유지"
      ULONGLONG real = oGetTickCount64();

      ULONGLONG startReal = s_startReal.load();
      ULONGLONG startFake = s_startFake.load();

      ULONGLONG delta = real - startReal;
      ULONGLONG currentFake = startFake + (ULONGLONG)(delta * current);

      // 기준 재설정 (끊김 없이)
      s_startReal.store(real);
      s_startFake.store(currentFake);

      s_multiplier.store(desired);
    }
  }

} // namespace DX11Base